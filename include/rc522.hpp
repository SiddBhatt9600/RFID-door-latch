#ifndef RC522_HPP
#define RC522_HPP

#include <cstdint>
#include <array>
#include <vector>
#include <optional>

#include "spiManager.hpp"

class RC522
{
public:
    explicit RC522(Spi& spi);

    void reset();
    void init();

    bool request();
    std::optional<std::vector<uint8_t>> anticoll();

private:
    // RC522 registers
    static constexpr uint8_t CommandReg    = 0x01;
    static constexpr uint8_t ComIEnReg     = 0x02;
    static constexpr uint8_t ComIrqReg    = 0x04;
    static constexpr uint8_t ErrorReg     = 0x06;
    static constexpr uint8_t FIFODataReg  = 0x09;
    static constexpr uint8_t FIFOLevelReg = 0x0A;
    static constexpr uint8_t ControlReg   = 0x0C;
    static constexpr uint8_t BitFramingReg = 0x0D;
    static constexpr uint8_t CollReg      = 0x0E;

    static constexpr uint8_t ModeReg      = 0x11;
    static constexpr uint8_t TxControlReg = 0x14;
    static constexpr uint8_t TxASKReg     = 0x15;
    static constexpr uint8_t RFCfgReg     = 0x26;

    static constexpr uint8_t TModeReg      = 0x2A;
    static constexpr uint8_t TPrescalerReg = 0x2B;
    static constexpr uint8_t TReloadRegH   = 0x2C;
    static constexpr uint8_t TReloadRegL   = 0x2D;

    static constexpr uint8_t VersionReg = 0x37;

    // RC522 commands
    static constexpr uint8_t PCD_IDLE       = 0x00;
    static constexpr uint8_t PCD_AUTHENT    = 0x0E;
    static constexpr uint8_t PCD_TRANSCEIVE = 0x0C;
    static constexpr uint8_t PCD_RESETPHASE = 0x0F;

    // PICC commands
    static constexpr uint8_t PICC_REQIDL = 0x26;
    static constexpr uint8_t PICC_ANTICOLL = 0x93;

    Spi& spi_;

    void write_reg(uint8_t reg, uint8_t value);
    uint8_t read_reg(uint8_t reg);

    void set_bit_mask(uint8_t reg, uint8_t mask);
    void clear_bit_mask(uint8_t reg, uint8_t mask);

    void antenna_on();
};

#endif // RC522_HPP