#ifndef SPI_MANAGER_HPP
#define SPI_MANAGER_HPP

#include <cstdint>

class Spi {
public:
    bool open_dev(const char* path, uint32_t speed_hz);

    // Two-byte transfer, mirrors spidev.xfer2([a, b])
    void xfer2(uint8_t a, uint8_t b, uint8_t out[2]);

    void close_dev() ;

    ~Spi() ;

private:
    int fd_ = -1;
    uint32_t speed_ = 1'000'000;
};

#endif // SPI_MANAGER_HPP
