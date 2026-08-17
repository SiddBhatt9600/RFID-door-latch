#include "spiManager.hpp"

#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>

#include <linux/spi/spidev.h>

bool Spi::open_dev(const char* path, uint32_t speed_hz)
{
    fd_ = ::open(path, O_RDWR);
    if (fd_ < 0)
        return false;

    uint8_t mode = SPI_MODE_0;
    uint8_t bits = 8;

    if (ioctl(fd_, SPI_IOC_WR_MODE, &mode) < 0)
        return false;

    if (ioctl(fd_, SPI_IOC_WR_BITS_PER_WORD, &bits) < 0)
        return false;

    if (ioctl(fd_, SPI_IOC_WR_MAX_SPEED_HZ, &speed_hz) < 0)
        return false;

    speed_ = speed_hz;

    return true;
}

void Spi::xfer2(uint8_t a, uint8_t b, uint8_t out[2]) {
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

void Spi::close_dev() {
    if (fd_ >= 0) { ::close(fd_); fd_ = -1; }
}

Spi::~Spi()
{
    close_dev();
}