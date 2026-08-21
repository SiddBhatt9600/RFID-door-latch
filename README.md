# RC522 RFID Reader and Access Control for BeagleBone Black

A register-level RC522 RFID reader and access-control implementation for the BeagleBone Black.

The project contains two working implementations:

- `rc522_read.py` - Python implementation
- `rc522.cpp` - C++17 implementation

The system communicates with the RC522 over SPI, controls the RC522 reset line through GPIO using libgpiod, and controls an electronic lock through a 5 V relay module.

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

The RC522 is powered from 3.3 V.

### BeagleBone Black to Relay

The relay module is powered from a separate 5 V supply.

P9.23 is used as the relay control GPIO. Because the relay module requires a 5 V trigger while the BeagleBone GPIO operates at approximately 3.3 V, the GPIO is not connected directly to the relay input.

A BD139 NPN transistor is used as a low-side switching stage.

| Connection | Description |
|---|---|
| P9.23 | BeagleBone GPIO control |
| P9.23 -> 1 kΩ | BD139 base resistor |
| BD139 emitter | Common GND |
| BD139 collector | Relay IN / 10 kΩ pull-up node |
| 10 kΩ | Pull-up from Relay IN to +5 V |
| Relay VCC | +5 V |
| Relay GND | Common GND |
| Relay IN | BD139 collector |

The control circuit is:

```text
                         +5V
                          |
                         10kΩ
                          |
                          +──────── Relay IN
                          |
                          C
                       BD139
                          E
                          |
                         GND

BBB P9.23 ───── 1kΩ ───── B
```

The 10 kΩ resistor is a pull-up resistor. When the BD139 is OFF, it pulls the relay `IN` signal to approximately 5 V. When the BD139 is ON, it pulls the relay `IN` signal to GND.

The relay module is active-low:

```text
Relay IN ≈ 5V  -> Relay OFF
Relay IN ≈ 0V  -> Relay ON
```

Therefore the overall GPIO behavior is:

```text
P9.23 LOW  -> BD139 OFF -> Relay IN HIGH -> Relay OFF
P9.23 HIGH -> BD139 ON  -> Relay IN LOW  -> Relay ON
```

The BeagleBone GPIO and the relay power supply share a common GND.

### Relay to Electronic Lock

The relay provides electrical switching between the lock's power supply and the lock.

The lock is a two-wire load:

- `+` / live
- `GND`

The relay's `COM` and `NO` contacts are used so that the lock is normally unpowered.

```text
Lock power supply +
        |
       COM
        |
      Relay
        |
       NO
        |
        +──────── Lock +

Lock GND ───────── Lock power supply GND
```

The `NC` terminal is left unused.

Operation:

```text
Relay OFF -> COM disconnected from NO -> Lock unpowered -> Lock closed
Relay ON  -> COM connected to NO       -> Lock powered   -> Lock opens
```

The lock may use a different supply voltage from the 5 V relay control supply, provided the relay contact voltage and current ratings are not exceeded.

The 5 V supply powers the relay module's control side. The lock power supply is connected to the relay's switching contacts.

## Power Distribution

The project uses separate 5 V and 3.3 V rails.

```text
Power Module
|
+-- 5V
|    +-- Relay VCC
|    +-- 10kΩ pull-up -> Relay IN / BD139 collector
|
+-- 3.3V
|    +-- RC522 VCC
|
+-- GND
     +-- BeagleBone Black GND
     +-- RC522 GND
     +-- Relay GND
     +-- BD139 emitter
```

The RC522 is powered from 3.3 V.

The relay module is powered from 5 V.

The BeagleBone GPIO never receives the 5 V relay signal. The BD139 provides the required interface between the 3.3 V GPIO and the 5 V relay input.

## Software Configuration

| Parameter | Value |
|---|---|
| SPI bus | SPI1 |
| Chip select | CS0 |
| SPI device | `/dev/spidev1.0` |
| SPI mode | 0 |
| SPI speed | 1 MHz |
| Reset GPIO | P8.7 |
| Reset GPIO chip | `/dev/gpiochip1` |
| Reset GPIO line | 2 |
| Relay GPIO | P9.23 |
| Relay GPIO chip | `/dev/gpiochip0` |
| Relay GPIO line | 17 |
| RC522 VersionReg | `0x92` |

GPIO mappings:

```text
P8.7  -> gpiochip1 line 2
P9.23 -> gpiochip0 line 17
```

## Relay GPIO Control

The relay is controlled using libgpiod.

Set P9.23 LOW:

```bash
gpioset -c gpiochip0 17=0
```

This results in:

```text
P9.23 LOW
    |
    v
BD139 OFF
    |
    v
Relay IN pulled to +5V
    |
    v
Relay OFF
    |
    v
Lock remains closed
```

Set P9.23 HIGH:

```bash
gpioset -c gpiochip0 17=1
```

This results in:

```text
P9.23 HIGH
    |
    v
BD139 ON
    |
    v
Relay IN pulled to GND
    |
    v
Relay ON
    |
    v
COM connects to NO
    |
    v
Lock receives power
    |
    v
Lock opens
```

The GPIO output was verified with a multimeter:

```text
P9.23 LOW  -> approximately 0 V
P9.23 HIGH -> approximately 3.25 V
```

The BD139 stage converts this 3.3 V GPIO control into the required relay input levels.

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
Check authorized UID
      |
      v
Access granted
      |
      +------------------+
      |                  |
      v                  v
Activate relay       Access denied
      |
      v
Power electronic lock
      |
      v
Lock opens
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

## Access Control

The intended access-control behavior is:

```text
RFID card detected
       |
       v
Read UID
       |
       v
Compare UID against authorized UID
       |
       +------------------+
       |                  |
   Authorized        Unauthorized
       |                  |
       v                  v
Relay ON              Relay OFF
       |
       v
Lock powered
       |
       v
Lock opens
```

The relay is used as the interface between the low-voltage BeagleBone control system and the electronic lock.

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

This repository is based on the working BeagleBone Black + RC522 + relay + electronic lock configuration.

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
| RC522 reset GPIO chip | `/dev/gpiochip1` |
| RC522 reset GPIO line | 2 |
| Relay GPIO | P9.23 |
| Relay GPIO chip | `/dev/gpiochip0` |
| Relay GPIO line | 17 |
| Relay control transistor | BD139 |
| Base resistor | 1 kΩ |
| Relay IN pull-up | 10 kΩ to +5 V |
| RC522 VersionReg | `0x92` |
| Antenna | `TxControlReg = 0x83` |

Both `rc522_read.py` and `rc522.cpp` should be treated as the reference implementations for this hardware configuration.
