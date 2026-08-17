#include "rc522.hpp"
#include "spiManager.hpp"



// =========================
// Hardware configuration
// =========================

static constexpr const char* SPI_DEV   = "/dev/spidev1.0"; // bus 1, CS 0
static constexpr uint32_t    SPI_SPEED = 1'000'000;

static constexpr const char* GPIO_CHIP = "/dev/gpiochip1";
static constexpr unsigned int RST_LINE = 2; // P8.7

static volatile std::sig_atomic_t g_stop = 0;
void handle_sigint(int) { g_stop = 1; }


int main() {
    Spi spi;
    RC522 rc522(spi);
    
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