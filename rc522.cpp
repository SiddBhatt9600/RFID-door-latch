// rc522_read.cpp
//
// RC522 RFID reader over SPI, reset via gpiod, for BeagleBone Black.
//
// Build:
//   g++ -std=c++17 rc522_read.cpp -o rc522_read -lgpiod
//
// Run:
//   sudo ./rc522_read

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <thread>
#include <chrono>
#include <vector>
#include <optional>
#include <csignal>

#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/spi/spidev.h>

#include <gpiod.hpp>

// =========================
// RC522 registers
// =========================

static constexpr uint8_t CommandReg    = 0x01;
static constexpr uint8_t ComIrqReg     = 0x04;
static constexpr uint8_t ErrorReg      = 0x06;
static constexpr uint8_t FIFODataReg   = 0x09;
static constexpr uint8_t FIFOLevelReg  = 0x0A;
static constexpr uint8_t BitFramingReg = 0x0D;
static constexpr uint8_t ModeReg       = 0x11;
static constexpr uint8_t TxControlReg  = 0x14;
static constexpr uint8_t TxASKReg      = 0x15;
static constexpr uint8_t RFCfgReg      = 0x26; // antenna gain
static constexpr uint8_t TModeReg      = 0x2A;
static constexpr uint8_t TPrescalerReg = 0x2B;
static constexpr uint8_t TReloadRegH   = 0x2C;
static constexpr uint8_t TReloadRegL   = 0x2D;
static constexpr uint8_t VersionReg    = 0x37;

// =========================
// RC522 commands
// =========================

static constexpr uint8_t PCD_IDLE       = 0x00;
static constexpr uint8_t PCD_TRANSCEIVE = 0x0C;

static constexpr uint8_t PICC_REQIDL    = 0x26;
static constexpr uint8_t PICC_ANTICOLL  = 0x93;

// =========================
// Hardware configuration
// =========================

static constexpr const char* SPI_DEV   = "/dev/spidev1.0"; // bus 1, CS 0
static constexpr uint32_t    SPI_SPEED = 1'000'000;

static constexpr const char* GPIO_CHIP = "/dev/gpiochip1";
static constexpr unsigned int RST_LINE = 2; // P8.7

static volatile std::sig_atomic_t g_stop = 0;
void handle_sigint(int) { g_stop = 1; }

// =========================
// SPI
// =========================

class Spi {
public:
    bool open_dev(const char* path, uint32_t speed_hz) {
        fd_ = ::open(path, O_RDWR);
        if (fd_ < 0) return false;

        uint8_t mode = SPI_MODE_0;
        uint8_t bits = 8;

        if (ioctl(fd_, SPI_IOC_WR_MODE, &mode) < 0) return false;
        if (ioctl(fd_, SPI_IOC_WR_BITS_PER_WORD, &bits) < 0) return false;
        if (ioctl(fd_, SPI_IOC_WR_MAX_SPEED_HZ, &speed_hz) < 0) return false;

        speed_ = speed_hz;
        return true;
    }

    // Two-byte transfer, mirrors spidev.xfer2([a, b])
    void xfer2(uint8_t a, uint8_t b, uint8_t out[2]) {
        uint8_t tx[2] = {a, b};
        uint8_t rx[2] = {0, 0};

        spi_ioc_transfer tr{};
        tr.tx_buf = reinterpret_cast<uintptr_t>(tx);
        tr.rx_buf = reinterpret_cast<uintptr_t>(rx);
        tr.len = 2;
        tr.speed_hz = speed_;
        tr.bits_per_word = 8;

        ioctl(fd_, SPI_IOC_MESSAGE(1), &tr);
        out[0] = rx[0];
        out[1] = rx[1];
    }

    void close_dev() {
        if (fd_ >= 0) { ::close(fd_); fd_ = -1; }
    }

    ~Spi() { close_dev(); }

private:
    int fd_ = -1;
    uint32_t speed_ = 1'000'000;
};

static Spi spi;

void write_reg(uint8_t reg, uint8_t value) {
    uint8_t address = (reg << 1) & 0x7E;
    uint8_t rx[2];
    spi.xfer2(address, value, rx);
}

uint8_t read_reg(uint8_t reg) {
    uint8_t address = ((reg << 1) & 0x7E) | 0x80;
    uint8_t rx[2];
    spi.xfer2(address, 0x00, rx);
    return rx[1];
}

void set_bit_mask(uint8_t reg, uint8_t mask) {
    write_reg(reg, read_reg(reg) | mask);
}

void clear_bit_mask(uint8_t reg, uint8_t mask) {
    write_reg(reg, read_reg(reg) & (~mask & 0xFF));
}

// =========================
// GPIO / Reset
// =========================

std::optional<gpiod::line_request> g_rst_request;

void reset_pin_init() {
    gpiod::chip chip(GPIO_CHIP);

    gpiod::line_settings settings;
    settings.set_direction(gpiod::line::direction::OUTPUT);
    settings.set_output_value(gpiod::line::value::ACTIVE);

    gpiod::line_config line_cfg;
    line_cfg.add_line_settings(
        gpiod::line::offsets{RST_LINE},
        settings
    );

    g_rst_request = chip.prepare_request()
                        .set_consumer("rc522")
                        .set_line_config(line_cfg)
                        .do_request();
}

void reset_rc522() {
    g_rst_request->set_value(RST_LINE, gpiod::line::value::INACTIVE);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    g_rst_request->set_value(RST_LINE, gpiod::line::value::ACTIVE);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
}

// =========================
// RC522 initialization
// =========================

void antenna_on() {
    uint8_t value = read_reg(TxControlReg);
    if (!(value & 0x03)) {
        set_bit_mask(TxControlReg, 0x03);
    }
}

void init_rc522() {
    reset_rc522();

    write_reg(TModeReg, 0x8D);
    write_reg(TPrescalerReg, 0x3E);

    write_reg(TReloadRegL, 30);
    write_reg(TReloadRegH, 0);

    write_reg(TxASKReg, 0x40);
    write_reg(ModeReg, 0x3D);

    // Max receiver gain — helps with marginal antenna gain issues.
    set_bit_mask(RFCfgReg, 0x70);

    antenna_on();
}

// =========================
// RFID request
// =========================

bool request() {
    write_reg(BitFramingReg, 0x07); // 7 bits for REQA

    write_reg(CommandReg, PCD_IDLE);
    write_reg(ComIrqReg, 0x7F);

    write_reg(FIFOLevelReg, 0x80); // clear FIFO
    write_reg(FIFODataReg, PICC_REQIDL);

    write_reg(CommandReg, PCD_TRANSCEIVE);
    set_bit_mask(BitFramingReg, 0x80);

    for (int i = 0; i < 100; ++i) {
        uint8_t irq = read_reg(ComIrqReg);

        if (irq & 0x30) break;
        if (irq & 0x01) return false;

        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    clear_bit_mask(BitFramingReg, 0x80);

    uint8_t error = read_reg(ErrorReg);
    if (error & 0x1B) return false;

    return true;
}

// =========================
// UID anti-collision
// =========================

std::optional<std::vector<uint8_t>> anticoll() {
    write_reg(BitFramingReg, 0x00);

    // --- fix: reset command state and clear stale IRQ flags before
    // starting a fresh transceive, exactly like request() does.
    // Without this, leftover ComIrqReg bits from the preceding
    // request() call can make the polling loop below exit early,
    // before the tag's UID response has actually arrived.
    write_reg(CommandReg, PCD_IDLE);
    write_reg(ComIrqReg, 0x7F);

    write_reg(FIFOLevelReg, 0x80); // clear FIFO

    write_reg(FIFODataReg, PICC_ANTICOLL);
    write_reg(FIFODataReg, 0x20); // cascade level 1

    write_reg(CommandReg, PCD_TRANSCEIVE);
    set_bit_mask(BitFramingReg, 0x80);

    for (int i = 0; i < 100; ++i) {
        uint8_t irq = read_reg(ComIrqReg);

        if (irq & 0x30) break;
        if (irq & 0x01) return std::nullopt;

        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    clear_bit_mask(BitFramingReg, 0x80);

    uint8_t error = read_reg(ErrorReg);
    if (error & 0x1B) return std::nullopt;

    uint8_t length = read_reg(FIFOLevelReg);
    if (length != 5) return std::nullopt;

    std::vector<uint8_t> uid;
    for (int i = 0; i < 5; ++i) {
        uid.push_back(read_reg(FIFODataReg));
    }

    uint8_t calculated_bcc = uid[0] ^ uid[1] ^ uid[2] ^ uid[3];
    if (calculated_bcc != uid[4]) {
        std::printf("Warning: UID BCC mismatch\n");
    }

    return std::vector<uint8_t>(uid.begin(), uid.begin() + 4);
}

// =========================
// Main
// =========================

int main() {
    std::signal(SIGINT, handle_sigint);

    std::printf("Opening SPI...\n");
    if (!spi.open_dev(SPI_DEV, SPI_SPEED)) {
        std::fprintf(stderr, "Failed to open %s\n", SPI_DEV);
        return 1;
    }
    std::printf("SPI opened\n");

    std::printf("Initializing GPIO...\n");
    reset_pin_init();

    std::printf("Resetting RC522...\n");
    reset_rc522();

    uint8_t version = read_reg(VersionReg);
    std::printf("RC522 VersionReg = 0x%02X\n", version);

    if (version != 0x91 && version != 0x92) {
        std::printf("WARNING: Unexpected RC522 version\n");
    }

    init_rc522();

    std::printf("RC522 initialized\n");
    std::printf("Place RFID tag on the reader...\n\n");

    std::vector<uint8_t> last_uid;

    while (!g_stop) {
        if (request()) {
            auto uid = anticoll();

            if (uid && *uid != last_uid) {
                std::printf("UID: ");
                for (uint8_t b : *uid) std::printf("%02X ", b);
                std::printf("\n");
                last_uid = *uid;
            } else if (!uid) {
                last_uid.clear();
            }
        } else {
            last_uid.clear();
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }

    std::printf("\nStopping...\n");

    if (g_rst_request) {
        g_rst_request->release();
    }
    spi.close_dev();

    return 0;
}