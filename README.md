# RC522 RFID Reader and Access Control for BeagleBone Black

A register-level RC522 RFID reader and access-control implementation for
the BeagleBone Black.

The project contains two working implementations:

- `rc522_read.py` - Python implementation
- `rc522.cpp` - C++17 implementation

The project communicates with the RC522 over SPI, controls the RC522
reset line through GPIO using libgpiod, and controls an electronic lock
through a 5 V relay module.

## SPI Device Tree Overlay Configuration

The RC522 uses SPI1 on the BeagleBone Black.

The BeagleBone Black does not expose SPI pins by default. The SPI
controller and header pin multiplexing must be enabled through a Device
Tree Overlay (`.dtbo`).

Known-good configuration:

| Parameter | Value
|  ---|---|
| SPI bus | SPI1
| SPI device | `/dev/spidev1.0`
| Chip select | CS0
| Overlay | `BB-SPIDEV1-00A0.dtbo`

## Generating the SPI overlay

There are two common ways to obtain the required `.dtbo` file.

### Option 1: BeagleBoard DeviceTrees

Repository:

https://github.com/beagleboard/BeagleBoard-DeviceTrees

This builds the complete BeagleBoard device tree collection.

Generated overlays can be installed into:

    /boot/dtbs/<kernel-version>/

Example:

    /boot/dtbs/5.10.168-ti-r84/

Installing many unused overlays can significantly slow boot because
U-Boot processes a larger device-tree collection.

### Option 2: Build only overlays (recommended)

Repository:

https://github.com/beagleboard/bb.org-overlays

Build only overlays:

    make src/arm/BB-*.dtbo

For this project:

    BB-SPIDEV1-00A0.dtbo

Install:

    sudo cp /path/to/BB-SPIDEV1-00A0.dtbo /lib/firmware/

Enable in `/boot/uEnv.txt`:

    uboot_overlay_addr4=/lib/firmware/BB-SPIDEV1-00A0.dtbo

## HDMI / Device Tree Overlay Warning

The BeagleBone Black HDMI cape functionality can consume pins required
by other peripherals.

If SPI, UART, PWM, or other header peripherals do not appear after
enabling an overlay, check for HDMI overlays claiming those pins.

Unused HDMI functionality can be disabled in `/boot/uEnv.txt`:

    disable_uboot_overlay_video=1
    disable_uboot_overlay_audio=1

Reboot:

    sudo reboot

Verify:

    ls /dev/spidev*

Expected:

    /dev/spidev1.0

For this project SPI1 uses:

| Signal | Pin |
|---|---|
| SPI1_SCLK | P9.31 |
| SPI1_D0 (MOSI) | P9.30 |
| SPI1_D1 (MISO) | P9.29 |
| SPI1_CS0 | P9.28 |

These pins must not be assigned to another overlay.

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

The 10 kΩ resistor is a pull-up resistor. When the BD139 is OFF, the relay `IN` signal is pulled to approximately 5 V. When the BD139 is ON, it pulls the relay `IN` signal to GND.

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

### Speaker Module

The project uses an external speaker module. The BeagleBone does not directly power the speaker from the GPIO pin. The GPIO/PWM pin is connected to the module's signal/input pin.

The final working connection is:

```text
BeagleBone Black P9.14  ---- Speaker module IN / signal
BeagleBone Black GND     ---- Speaker module GND
Speaker module VCC      ---- Speaker module power supply
```

The important distinction is that **P9.14 is the signal connection**. It must not be connected to the speaker module's VCC pin.

The speaker module ground is connected to BeagleBone GND so the PWM signal and module share the same reference.

The same speaker module was previously verified using an ESP32 GPIO output. On the BeagleBone, the working signal path is:

```text
P9.14
   |
   v
EHRPWM1A
   |
   v
/dev/bone/pwm/1/a
   |
   v
Speaker module IN
```

The access-granted sound is generated as a 1 kHz, 50% duty-cycle PWM signal for 1 second.

```text
Frequency:  1 kHz
Duty cycle: 50%
Duration:   1 second
```

The beep is triggered when an authorized RFID UID is detected and the electronic lock is unlocked.

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

The BeagleBone GPIO never receives the 5 V relay signal. The BD139 provides the interface between the 3.3 V GPIO and the 5 V relay input.

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
| Speaker PWM | `/dev/bone/pwm/1/a` |
| Speaker pin | P9.14 |
| PWM frequency | 1 kHz |
| PWM duty cycle | 50% |
| Access-granted beep | 1 second |
| RC522 VersionReg | `0x92` |

GPIO mappings:

```text
P8.7  -> gpiochip1 line 2
P9.23 -> gpiochip0 line 17
```

## Debian 13 + Linux 6.18 SPI Setup

The RC522 uses SPI1 CS0. On the Debian 13 + Linux 6.x setup, SPI1 is enabled through the U-Boot device-tree overlay mechanism.

The important entries in `/boot/uEnv.txt` are:

```text
enable_uboot_overlays=1
uboot_overlay_addr0=BB-SPIDEV1-00A0.dtbo
```

The `BB-SPIDEV1-00A0.dtbo` overlay enables SPI1 and exposes the SPI devices used by the RC522. The project uses CS0, resulting in:

```text
/dev/spidev1.0
```

After changing `/boot/uEnv.txt`, reboot the BeagleBone:

```bash
sudo reboot
```

Verify that the overlay was actually loaded:

```bash
ls -la /proc/device-tree/chosen/overlays/
```

The output should contain:

```text
BB-SPIDEV1-00A0.kernel
```

Then verify the SPI device:

```bash
ls -l /dev/spidev*
```

The RC522 device used by this project should be present:

```text
/dev/spidev1.0
```

If `spidev` is not loaded as a module on a particular installation, verify/load it with:

```bash
lsmod | grep spidev
sudo modprobe spidev
```

The application itself does not need a special SPI setup beyond opening `/dev/spidev1.0`. The C++ implementation configures SPI mode 0, 8 bits per word, and 1 MHz; the Python implementation uses the same bus, chip-select, mode, and speed.

## Debian 13 + Linux 6.18 PWM Setup

The working speaker setup depends on enabling the EHRPWM1 pinmux/PWM configuration on P9.14.

With the working Debian 13 + 6.18 configuration, the PWM interface is:

```text
/dev/bone/pwm/1/a
```

Enable/configure the 1 kHz beep with:

```bash
cd /dev/bone/pwm/1/a

sudo sh -c 'echo 1000000 > period'
sudo sh -c 'echo 500000 > duty_cycle'
sudo sh -c 'echo 1 > enable'
```

Disable it with:

```bash
sudo sh -c 'echo 0 > enable'
```

Make sure U-Boot overlays are enabled in `/boot/uEnv.txt` and that the EHRPWM1 overlay is selected:

```text
enable_uboot_overlays=1
uboot_overlay_addr1=BB-EHRPWM1-P9_14-P9_16.dtbo
```

After reboot, verify that the BeagleBone PWM interface exists:

```bash
ls -l /dev/bone/pwm/1/a
```

The working PWM output is then configured directly through that interface:

```bash
cd /dev/bone/pwm/1/a

sudo sh -c 'echo 1000000 > period'
sudo sh -c 'echo 500000 > duty_cycle'
sudo sh -c 'echo 1 > enable'
```

This produces a 1 kHz, 50% duty-cycle tone.

Stop the speaker with:

```bash
sudo sh -c 'echo 0 > enable'
```

For this project, `/dev/bone/pwm/1/a` is the known-good speaker PWM path. If this path is missing after a kernel/device-tree change, verify the EHRPWM1 overlay before changing the application code.

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

The Python implementation uses the libgpiod 2.x Python API used by the known-good Debian 13 application, including `gpiod.LineSettings` and `request_lines()`.

### Run

```bash
sudo python3 rc522_read.py
```

The Python implementation opens SPI1 CS0, initializes the RC522, performs a hardware reset through P8.7, initializes the relay on P9.23, polls for RFID tags, and generates the access-granted PWM beep.

## C++

### Dependencies

```bash
sudo apt update
sudo apt install g++ libgpiod-dev
```

The C++ implementation uses the libgpiod API, Linux SPI, and the BeagleBone PWM sysfs interface.

### Build

```bash
g++ -std=c++17 rc522.cpp -o rc522 -lgpiodcxx -lgpiod -pthread
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
Check access
      |
      +------------------+
      |                  |
 Access granted      Access denied
      |
      v
Relay ON + 1 kHz beep
      |
      v
Lock powered
      |
      v
Keep relay energized for 5 seconds
      |
      v
Relay OFF
      |
      v
Lock closes
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

## Access-Granted Beep

The speaker is driven when a new RFID UID is detected and the access-control sequence begins.

The PWM configuration is:

```text
period     = 1,000,000 ns
frequency  = 1 kHz
duty_cycle = 500,000 ns
duty        = 50%
duration   = 1 second
```

The sequence is:

```text
New RFID UID
     |
     v
Relay ON
     |
     +---- Speaker ON
     |       1 kHz, 50% duty
     |       for 1 second
     |
     v
Continue unlock interval
     |
     v
After 5 seconds
     |
     v
Relay OFF
```

The speaker PWM is implemented by writing directly to:

```text
/dev/bone/pwm/1/a/period
/dev/bone/pwm/1/a/duty_cycle
/dev/bone/pwm/1/a/enable
```

No WAV playback or PRU audio path is part of the current checkpoint.

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
BeagleBone PWM interface
```

### `rc522.cpp`

C++17 implementation using:

```text
Linux SPI
libgpiod
BeagleBone PWM interface
```

## Known-Good Checkpoint

This repository is based on the working BeagleBone Black + RC522 + relay + electronic lock + speaker checkpoint.

| Parameter | Value |
|---|---|
| Board | BeagleBone Black |
| OS | Debian GNU/Linux 13 |
| Kernel | 6.18.x-bone |
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
| Speaker pin | P9.14 |
| Speaker PWM path | `/dev/bone/pwm/1/a` |
| Speaker tone | 1 kHz, 50% duty |
| Beep duration | 1 second |
| RC522 VersionReg | `0x92` |
| Antenna | `TxControlReg = 0x83` |
