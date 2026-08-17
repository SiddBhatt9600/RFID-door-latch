# RC522 RFID Reader for BeagleBone Black

A register-level RC522 RFID reader implementation for the BeagleBone Black.

This repository contains two working implementations:

- `rc522_read.py` - Python implementation
- `rc522.cpp` - C++17 implementation

The reader communicates with the RC522 over SPI and controls the RC522 reset line through GPIO using libgpiod.

## Hardware

### BeagleBone Black to RC522

| RC522 Pin | BeagleBone Black Pin |
|---|---|
| SDA / SS | P9.28 |
| SCK | P9.31 |
| MOSI | P9.30 |
| MISO | P9.29 |
| RST | P8.7 |
| 3.3V | P9.3 / P9.4 |
| GND | P9.1 / P9.2 |

The RC522 is powered from 3.3V.

## Software Configuration

| Parameter | Value |
|---|---|
| SPI bus | SPI1 |
| Chip select | CS0 |
| SPI device | `/dev/spidev1.0` |
| SPI mode | 0 |
| SPI speed | 1 MHz |
| Reset GPIO | P8.7 |
| GPIO chip | `/dev/gpiochip1` |
| GPIO line | 2 |
| RC522 VersionReg | `0x92` |

The GPIO mapping is:

```text
P8.7 -> gpiochip1 line 2
```

## Python

### Dependencies

```bash
sudo apt update
sudo apt install python3-spidev python3-libgpiod
```

### Run

```bash
sudo python3 rc522_read.py
```

The Python implementation opens SPI1 CS0, initializes the RC522, performs a hardware reset through P8.7, and continuously polls for RFID tags.

Example output:

```text
Opening SPI...
SPI opened
Initializing GPIO...
Resetting RC522...
RC522 VersionReg = 0x92
RC522 initialized
Place RFID tag on the reader...

UID: B6 CB 31 02
```

## C++

### Dependencies

```bash
sudo apt update
sudo apt install g++ libgpiod-dev
```

The C++ implementation uses the libgpiod C++ API.

### Build

```bash
g++ -std=c++17 rc522.cpp -o rc522 -lgpiodcxx -lgpiod
```

### Run

```bash
sudo ./rc522
```

## RC522 Operation

The implementations communicate directly with the RC522 registers over SPI.

The working initialization sequence includes:

```text
TModeReg       = 0x8D
TPrescalerReg  = 0x3E
TReloadRegL    = 30
TReloadRegH    = 0
TxASKReg       = 0x40
ModeReg        = 0x3D
```

The C++ implementation also configures the receiver gain through `RFCfgReg`.

The antenna is enabled through `TxControlReg`. The working configuration results in:

```text
TxControlReg = 0x83
```

## RFID Detection Flow

```text
Initialize SPI
      |
      v
Initialize GPIO
      |
      v
Reset RC522
      |
      v
Read VersionReg
      |
      v
Initialize RC522
      |
      v
Enable antenna
      |
      v
Send REQA
      |
      v
Anti-collision
      |
      v
Read UID
      |
      v
Validate BCC
      |
      v
Print UID
      |
      v
Continue polling
```

The REQA command is:

```text
0x26
```

For ISO14443A anti-collision, the implementation uses:

```text
0x93 0x20
```

The anti-collision response contains four UID bytes followed by the BCC. The BCC is validated by XORing the four UID bytes.

## Project Structure

```text
rfid_scanner/
├── README.md
├── rc522_read.py
└── rc522.cpp
```

### `rc522_read.py`

Python implementation using:

```text
spidev
libgpiod
```

### `rc522.cpp`

C++17 implementation using:

```text
Linux SPI
libgpiod C++ API
```

## Known-Good Configuration

This repository is based on the working BeagleBone Black + RC522 configuration.

| Parameter | Value |
|---|---|
| Board | BeagleBone Black |
| OS | Debian GNU/Linux 13 |
| Kernel | 6.18.39-bone44 |
| SPI | SPI1 CS0 |
| SPI device | `/dev/spidev1.0` |
| SPI mode | 0 |
| SPI speed | 1 MHz |
| RC522 RST | P8.7 |
| GPIO chip | `/dev/gpiochip1` |
| GPIO line | 2 |
| VersionReg | `0x92` |
| Antenna | `TxControlReg = 0x83` |

Both `rc522_read.py` and `rc522.cpp` should be treated as the reference implementations for this hardware configuration.
