#include <iostream>
#include <iomanip>
#include <sstream>
#include <vector>
#include <array>
#include <string>
#include <thread>
#include <chrono>
#include <stdexcept>
#include <csignal>
#include <cerrno>
#include <cstring>
#include <fstream>


#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/spi/spidev.h>

#include <gpiod.h>


// ============================================================
// RC522 registers
// ============================================================

constexpr uint8_t CommandReg     = 0x01;
constexpr uint8_t ComIEnReg      = 0x02;
constexpr uint8_t ComIrqReg      = 0x04;
constexpr uint8_t ErrorReg       = 0x06;
constexpr uint8_t FIFODataReg    = 0x09;
constexpr uint8_t FIFOLevelReg   = 0x0A;
constexpr uint8_t ControlReg     = 0x0C;
constexpr uint8_t BitFramingReg  = 0x0D;
constexpr uint8_t ModeReg        = 0x11;
constexpr uint8_t TxModeReg      = 0x12;
constexpr uint8_t RxModeReg      = 0x13;
constexpr uint8_t TxControlReg   = 0x14;
constexpr uint8_t TxASKReg       = 0x15;
constexpr uint8_t CRCResultRegH  = 0x21;
constexpr uint8_t CRCResultRegL  = 0x22;
constexpr uint8_t TModeReg       = 0x2A;
constexpr uint8_t TPrescalerReg  = 0x2B;
constexpr uint8_t TReloadRegH    = 0x2C;
constexpr uint8_t TReloadRegL    = 0x2D;
constexpr uint8_t VersionReg     = 0x37;


// ============================================================
// RC522 commands
// ============================================================

constexpr uint8_t PCD_IDLE       = 0x00;
constexpr uint8_t PCD_CALCCRC    = 0x03;
constexpr uint8_t PCD_TRANSCEIVE = 0x0C;

constexpr uint8_t PICC_REQIDL    = 0x26;
constexpr uint8_t PICC_ANTICOLL = 0x93;


// ============================================================
// Hardware configuration
// ============================================================

// RC522 SPI
constexpr int SPI_BUS = 1;
constexpr int SPI_CS  = 0;

// RC522 Reset
// P8.7 = gpiochip1 line 2
const char* GPIO_CHIP = "/dev/gpiochip1";
constexpr unsigned int RST_LINE = 2;

// Relay
// P9.23 = gpiochip0 line 17
const char* RELAY_GPIO_CHIP = "/dev/gpiochip0";
constexpr unsigned int RELAY_LINE = 17;

// Speaker
static constexpr const char* PWM_PATH = "/dev/bone/pwm/1/a";

bool writePwmFile(const std::string& file, const std::string& value)
{
    std::ofstream out(std::string(PWM_PATH) + "/" + file);
    if (!out.is_open()) {
        return false;
    }

    out << value;
    return out.good();
}

void beepAccessGranted()
{
    // 1 kHz, 50% duty cycle
    if (!writePwmFile("period", "1000000")) {
        return;
    }

    if (!writePwmFile("duty_cycle", "500000")) {
        return;
    }

    // Speaker ON
    if (!writePwmFile("enable", "1")) {
        return;
    }

    // Beep for 100 ms
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));

    // Speaker OFF
    writePwmFile("enable", "0");
}

// ============================================================
// Global handles
// ============================================================

int spi_fd = -1;

gpiod_chip* reset_chip = nullptr;
gpiod_line_request* reset_request = nullptr;

gpiod_chip* relay_chip = nullptr;
gpiod_line_request* relay_request = nullptr;

volatile sig_atomic_t running = 1;


// ============================================================
// Signal handler
// ============================================================

void signal_handler(int)
{
    running = 0;
}


// ============================================================
// Utility
// ============================================================

void sleep_ms(unsigned int ms)
{
    std::this_thread::sleep_for(
        std::chrono::milliseconds(ms)
    );
}


// ============================================================
// SPI
// ============================================================

void spi_init()
{
    std::string device =
        "/dev/spidev" +
        std::to_string(SPI_BUS) +
        "." +
        std::to_string(SPI_CS);

    spi_fd = open(device.c_str(), O_RDWR);

    if (spi_fd < 0)
    {
        throw std::runtime_error(
            "Failed to open " + device +
            ": " + std::strerror(errno)
        );
    }

    uint8_t mode = SPI_MODE_0;

    if (ioctl(spi_fd, SPI_IOC_WR_MODE, &mode) < 0)
    {
        throw std::runtime_error(
            "Failed to set SPI mode: " +
            std::string(std::strerror(errno))
        );
    }

    uint8_t bits = 8;

    if (ioctl(spi_fd, SPI_IOC_WR_BITS_PER_WORD, &bits) < 0)
    {
        throw std::runtime_error(
            "Failed to set SPI bits per word: " +
            std::string(std::strerror(errno))
        );
    }

    uint32_t speed = 1000000;

    if (ioctl(spi_fd, SPI_IOC_WR_MAX_SPEED_HZ, &speed) < 0)
    {
        throw std::runtime_error(
            "Failed to set SPI speed: " +
            std::string(std::strerror(errno))
        );
    }
}


uint8_t spi_transfer(uint8_t address, uint8_t value)
{
    uint8_t tx[2] = {address, value};
    uint8_t rx[2] = {0, 0};

    struct spi_ioc_transfer transfer {};

    transfer.tx_buf =
        reinterpret_cast<unsigned long>(tx);

    transfer.rx_buf =
        reinterpret_cast<unsigned long>(rx);

    transfer.len = 2;

    int ret = ioctl(
        spi_fd,
        SPI_IOC_MESSAGE(1),
        &transfer
    );

    if (ret < 0)
    {
        throw std::runtime_error(
            "SPI transfer failed: " +
            std::string(std::strerror(errno))
        );
    }

    return rx[1];
}


// ============================================================
// RC522 register access
// ============================================================

void write_reg(uint8_t reg, uint8_t value)
{
    uint8_t address =
        static_cast<uint8_t>((reg << 1) & 0x7E);

    spi_transfer(address, value);
}


uint8_t read_reg(uint8_t reg)
{
    uint8_t address =
        static_cast<uint8_t>(
            ((reg << 1) & 0x7E) | 0x80
        );

    return spi_transfer(address, 0x00);
}


void set_bit_mask(uint8_t reg, uint8_t mask)
{
    write_reg(
        reg,
        static_cast<uint8_t>(
            read_reg(reg) | mask
        )
    );
}


void clear_bit_mask(uint8_t reg, uint8_t mask)
{
    write_reg(
        reg,
        static_cast<uint8_t>(
            read_reg(reg) & (~mask & 0xFF)
        )
    );
}


// ============================================================
// RC522 GPIO / Reset
// ============================================================

void reset_pin_init()
{
    reset_chip = gpiod_chip_open(GPIO_CHIP);

    if (!reset_chip)
    {
        throw std::runtime_error(
            "Failed to open " +
            std::string(GPIO_CHIP) +
            ": " +
            std::string(std::strerror(errno))
        );
    }

    gpiod_line_settings* settings =
        gpiod_line_settings_new();

    if (!settings)
    {
        throw std::runtime_error(
            "Failed to create GPIO settings"
        );
    }

    gpiod_line_settings_set_direction(
        settings,
        GPIOD_LINE_DIRECTION_OUTPUT
    );

    gpiod_line_settings_set_output_value(
        settings,
        GPIOD_LINE_VALUE_ACTIVE
    );

    gpiod_line_config* line_config =
        gpiod_line_config_new();

    if (!line_config)
    {
        gpiod_line_settings_free(settings);

        throw std::runtime_error(
            "Failed to create GPIO line config"
        );
    }

    int ret = gpiod_line_config_add_line_settings(
        line_config,
        &RST_LINE,
        1,
        settings
    );

    if (ret < 0)
    {
        gpiod_line_config_free(line_config);
        gpiod_line_settings_free(settings);

        throw std::runtime_error(
            "Failed to configure RC522 reset GPIO"
        );
    }

    gpiod_request_config* request_config =
        gpiod_request_config_new();

    if (!request_config)
    {
        gpiod_line_config_free(line_config);
        gpiod_line_settings_free(settings);

        throw std::runtime_error(
            "Failed to create GPIO request config"
        );
    }

    gpiod_request_config_set_consumer(
        request_config,
        "rc522"
    );

    reset_request =
        gpiod_chip_request_lines(
            reset_chip,
            request_config,
            line_config
        );

    gpiod_request_config_free(request_config);
    gpiod_line_config_free(line_config);
    gpiod_line_settings_free(settings);

    if (!reset_request)
    {
        throw std::runtime_error(
            "Failed to request RC522 reset GPIO: " +
            std::string(std::strerror(errno))
        );
    }
}


void reset_rc522()
{
    if (!reset_request)
        return;

    gpiod_line_request_set_value(
        reset_request,
        RST_LINE,
        GPIOD_LINE_VALUE_INACTIVE
    );

    sleep_ms(50);

    gpiod_line_request_set_value(
        reset_request,
        RST_LINE,
        GPIOD_LINE_VALUE_ACTIVE
    );

    sleep_ms(50);
}


// ============================================================
// Relay GPIO
// ============================================================

void relay_init()
{
    relay_chip =
        gpiod_chip_open(RELAY_GPIO_CHIP);

    if (!relay_chip)
    {
        throw std::runtime_error(
            "Failed to open " +
            std::string(RELAY_GPIO_CHIP) +
            ": " +
            std::string(std::strerror(errno))
        );
    }

    gpiod_line_settings* settings =
        gpiod_line_settings_new();

    if (!settings)
    {
        throw std::runtime_error(
            "Failed to create relay GPIO settings"
        );
    }

    gpiod_line_settings_set_direction(
        settings,
        GPIOD_LINE_DIRECTION_OUTPUT
    );

    /*
     * Python:
     *
     * output_value=ACTIVE,
     * active_low=True
     *
     * We reproduce that behavior here.
     */
    gpiod_line_settings_set_output_value(
        settings,
        GPIOD_LINE_VALUE_ACTIVE
    );

    gpiod_line_settings_set_active_low(
        settings,
        true
    );

    gpiod_line_config* line_config =
        gpiod_line_config_new();

    if (!line_config)
    {
        gpiod_line_settings_free(settings);

        throw std::runtime_error(
            "Failed to create relay GPIO line config"
        );
    }

    int ret = gpiod_line_config_add_line_settings(
        line_config,
        &RELAY_LINE,
        1,
        settings
    );

    if (ret < 0)
    {
        gpiod_line_config_free(line_config);
        gpiod_line_settings_free(settings);

        throw std::runtime_error(
            "Failed to configure relay GPIO"
        );
    }

    gpiod_request_config* request_config =
        gpiod_request_config_new();

    if (!request_config)
    {
        gpiod_line_config_free(line_config);
        gpiod_line_settings_free(settings);

        throw std::runtime_error(
            "Failed to create relay request config"
        );
    }

    gpiod_request_config_set_consumer(
        request_config,
        "rc522-relay"
    );

    relay_request =
        gpiod_chip_request_lines(
            relay_chip,
            request_config,
            line_config
        );

    gpiod_request_config_free(request_config);
    gpiod_line_config_free(line_config);
    gpiod_line_settings_free(settings);

    if (!relay_request)
    {
        throw std::runtime_error(
            "Failed to request relay GPIO: " +
            std::string(std::strerror(errno))
        );
    }
}


// ============================================================
// Relay control
// ============================================================

void relay_on()
{
    /*
     * Active-low relay.
     *
     * Same behavior as Python:
     *
     * relay_request.set_value(
     *     RELAY_LINE,
     *     gpiod.line.Value.INACTIVE
     * )
     */

    gpiod_line_request_set_value(
        relay_request,
        RELAY_LINE,
        GPIOD_LINE_VALUE_INACTIVE
    );
}


void relay_off()
{
    /*
     * Active-low relay OFF.
     */

    gpiod_line_request_set_value(
        relay_request,
        RELAY_LINE,
        GPIOD_LINE_VALUE_ACTIVE
    );
}


// ============================================================
// RC522 initialization
// ============================================================

void antenna_on()
{
    uint8_t value =
        read_reg(TxControlReg);

    if (!(value & 0x03))
    {
        set_bit_mask(
            TxControlReg,
            0x03
        );
    }
}


void init_rc522()
{
    reset_rc522();

    write_reg(TModeReg, 0x8D);
    write_reg(TPrescalerReg, 0x3E);

    write_reg(TReloadRegL, 30);
    write_reg(TReloadRegH, 0);

    write_reg(TxASKReg, 0x40);
    write_reg(ModeReg, 0x3D);

    antenna_on();
}


// ============================================================
// RFID request
// ============================================================

bool request_card()
{
    // Tell RC522 that we are sending 7 bits
    write_reg(
        BitFramingReg,
        0x07
    );

    write_reg(
        CommandReg,
        PCD_IDLE
    );

    write_reg(
        ComIrqReg,
        0x7F
    );

    // Clear FIFO
    write_reg(
        FIFOLevelReg,
        0x80
    );

    // REQA command
    write_reg(
        FIFODataReg,
        PICC_REQIDL
    );

    // Start transceive
    write_reg(
        CommandReg,
        PCD_TRANSCEIVE
    );

    // Start transmission
    set_bit_mask(
        BitFramingReg,
        0x80
    );

    for (int i = 0; i < 100; i++)
    {
        uint8_t irq =
            read_reg(ComIrqReg);

        if (irq & 0x30)
        {
            break;
        }

        if (irq & 0x01)
        {
            return false;
        }

        sleep_ms(1);
    }

    clear_bit_mask(
        BitFramingReg,
        0x80
    );

    uint8_t error =
        read_reg(ErrorReg);

    if (error & 0x1B)
    {
        return false;
    }

    return true;
}


// ============================================================
// UID anti-collision
// ============================================================

std::array<uint8_t, 4> anticoll(bool& success)
{
    success = false;

    write_reg(
        BitFramingReg,
        0x00
    );

    write_reg(
        CommandReg,
        PCD_IDLE
    );

    write_reg(
        ComIrqReg,
        0x7F
    );

    // Clear FIFO
    write_reg(
        FIFOLevelReg,
        0x80
    );

    // Anti-collision command
    write_reg(
        FIFODataReg,
        PICC_ANTICOLL
    );

    // Cascade level 1
    write_reg(
        FIFODataReg,
        0x20
    );

    // Transceive
    write_reg(
        CommandReg,
        PCD_TRANSCEIVE
    );

    set_bit_mask(
        BitFramingReg,
        0x80
    );

    bool completed = false;

    for (int i = 0; i < 100; i++)
    {
        uint8_t irq =
            read_reg(ComIrqReg);

        if (irq & 0x30)
        {
            completed = true;
            break;
        }

        if (irq & 0x01)
        {
            return {};
        }

        sleep_ms(1);
    }

    clear_bit_mask(
        BitFramingReg,
        0x80
    );

    if (!completed)
    {
        return {};
    }

    uint8_t error =
        read_reg(ErrorReg);

    if (error & 0x1B)
    {
        return {};
    }

    uint8_t length =
        read_reg(FIFOLevelReg);

    if (length != 5)
    {
        return {};
    }

    std::array<uint8_t, 5> uid{};

    for (int i = 0; i < 5; i++)
    {
        uid[i] =
            read_reg(FIFODataReg);
    }

    // Last byte is BCC
    uint8_t calculated_bcc =
        uid[0] ^
        uid[1] ^
        uid[2] ^
        uid[3];

    if (calculated_bcc != uid[4])
    {
        std::cout
            << "Warning: UID BCC mismatch"
            << std::endl;
    }

    std::array<uint8_t, 4> result{
        uid[0],
        uid[1],
        uid[2],
        uid[3]
    };

    success = true;

    return result;
}


// ============================================================
// UID formatting
// ============================================================

std::string uid_to_string(
    const std::array<uint8_t, 4>& uid
)
{
    std::ostringstream oss;

    for (size_t i = 0; i < uid.size(); i++)
    {
        if (i > 0)
            oss << " ";

        oss
            << std::uppercase
            << std::hex
            << std::setw(2)
            << std::setfill('0')
            << static_cast<int>(uid[i]);
    }

    return oss.str();
}


// ============================================================
// Cleanup
// ============================================================

void cleanup()
{
    // Always turn relay OFF first
    if (relay_request)
    {
        try
        {
            relay_off();
        }
        catch (...)
        {
        }

        gpiod_line_request_release(
            relay_request
        );

        relay_request = nullptr;
    }

    if (relay_chip)
    {
        gpiod_chip_close(
            relay_chip
        );

        relay_chip = nullptr;
    }

    if (reset_request)
    {
        gpiod_line_request_release(
            reset_request
        );

        reset_request = nullptr;
    }

    if (reset_chip)
    {
        gpiod_chip_close(
            reset_chip
        );

        reset_chip = nullptr;
    }

    if (spi_fd >= 0)
    {
        close(spi_fd);
        spi_fd = -1;
    }
}


// ============================================================
// Main
// ============================================================

int main()
{
    signal(
        SIGINT,
        signal_handler
    );

    signal(
        SIGTERM,
        signal_handler
    );

    try
    {
        // ----------------------------------------------------
        // SPI
        // ----------------------------------------------------

        std::cout
            << "Opening SPI..."
            << std::endl;

        spi_init();

        std::cout
            << "SPI opened"
            << std::endl;


        // ----------------------------------------------------
        // RC522 GPIO
        // ----------------------------------------------------

        std::cout
            << "Initializing RC522 GPIO..."
            << std::endl;

        reset_pin_init();


        // ----------------------------------------------------
        // Relay
        // ----------------------------------------------------

        std::cout
            << "Initializing relay..."
            << std::endl;

        relay_init();


        // Relay is active-low, therefore start OFF
        relay_off();


        // ----------------------------------------------------
        // RC522 reset
        // ----------------------------------------------------

        std::cout
            << "Resetting RC522..."
            << std::endl;

        reset_rc522();


        // ----------------------------------------------------
        // Check RC522 version
        // ----------------------------------------------------

        uint8_t version =
            read_reg(VersionReg);

        std::cout
            << "RC522 VersionReg = 0x"
            << std::uppercase
            << std::hex
            << std::setw(2)
            << std::setfill('0')
            << static_cast<int>(version)
            << std::dec
            << std::endl;

        if (version != 0x91 &&
            version != 0x92)
        {
            std::cout
                << "WARNING: Unexpected RC522 version"
                << std::endl;
        }


        // ----------------------------------------------------
        // Initialize RC522
        // ----------------------------------------------------

        init_rc522();

        std::cout
            << "RC522 initialized"
            << std::endl;

        std::cout
            << "Relay initialized on P9.23"
            << std::endl;

        std::cout
            << "Place RFID tag on the reader..."
            << std::endl;

        std::cout << std::endl;


        // ----------------------------------------------------
        // Main RFID loop
        // ----------------------------------------------------

        std::array<uint8_t, 4> last_uid{};
        bool have_last_uid = false;

        while (running)
        {
            if (request_card())
            {
                bool success = false;

                auto uid =
                    anticoll(success);

                if (success)
                {
                    std::string uid_string =
                        uid_to_string(uid);

                    /*
                     * Only unlock if this is a new UID.
                     *
                     * Same behavior as:
                     *
                     * if uid != last_uid:
                     */
                    if (!have_last_uid ||
                        uid != last_uid)
                    {
                        std::cout
                            << "UID: "
                            << uid_string
                            << std::endl;

                        std::cout
                            << "RFID detected - opening relay | UNLOCKED"
                            << std::endl;


                        // ------------------------------------
                        // Unlock
                        // ------------------------------------

                        relay_on();
			beepAccessGranted();


                        // Keep relay energized for 5 seconds
                        for (int i = 0;
                             i < 50 && running;
                             i++)
                        {
                            sleep_ms(100);
                        }


                        // ------------------------------------
                        // Lock again
                        // ------------------------------------

                        relay_off();

                        std::cout
                            << "Relay released | Locked"
                            << std::endl;

                        std::cout
                            << std::endl;


                        last_uid = uid;
                        have_last_uid = true;
                    }
                }
            }
            else
            {
                /*
                 * No RFID tag detected.
                 *
                 * This allows the same tag to be
                 * detected again after being removed.
                 */
                have_last_uid = false;
            }

            sleep_ms(200);
        }
    }
    catch (const std::exception& e)
    {
        std::cerr
            << "ERROR: "
            << e.what()
            << std::endl;

        cleanup();

        return 1;
    }

    std::cout
        << "\nStopping..."
        << std::endl;

    cleanup();

    return 0;
}