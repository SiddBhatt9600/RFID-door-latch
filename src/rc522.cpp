// rc522_read.cpp
//
// RC522 RFID reader over SPI, reset via gpiod, for BeagleBone Black.
//
// Build:
//   g++ -std=c++17 rc522_read.cpp -o rc522_read -lgpiod
//
// Run:
//   sudo ./rc522_read

#include "rc522.hpp"
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

RC522::RC522(Spi& spi)
    : spi_(spi)
{
}

void RC522::write_reg(uint8_t reg, uint8_t value) {
    uint8_t address = (reg << 1) & 0x7E;
    uint8_t rx[2];
    spi_.xfer2(address, value, rx);
}

uint8_t RC522::read_reg(uint8_t reg) {
    uint8_t address = ((reg << 1) & 0x7E) | 0x80;
    uint8_t rx[2];
    spi_.xfer2(address, 0x00, rx);
    return rx[1];
}

void RC522::set_bit_mask(uint8_t reg, uint8_t mask) {
    write_reg(reg, read_reg(reg) | mask);
}

void RC522::clear_bit_mask(uint8_t reg, uint8_t mask) {
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

void RC522::reset() {
    g_rst_request->set_value(RST_LINE, gpiod::line::value::INACTIVE);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    g_rst_request->set_value(RST_LINE, gpiod::line::value::ACTIVE);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
}

// =========================
// RC522 initialization
// =========================

void RC522::antenna_on() {
    uint8_t value = read_reg(TxControlReg);
    if (!(value & 0x03)) {
        set_bit_mask(TxControlReg, 0x03);
    }
}

void init() {
    reset_rc522();

    write_reg(TModeReg, 0x8D);
    write_reg(TPrescalerReg, 0x3E);

    write_reg(TReloadRegL, 30);
    write_reg(TReloadRegH, 0);

    write_reg(TxASKReg, 0x40);
    write_reg(ModeReg, 0x3D);

    // Max receiver gain � helps with marginal antenna gain issues.
    set_bit_mask(RFCfgReg, 0x70);

    antenna_on();
}

// =========================
// RFID request
// =========================

bool RC522::request() {
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

std::optional<std::vector<uint8_t>> RC522::anticoll() {
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
