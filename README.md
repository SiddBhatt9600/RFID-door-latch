# RC522 RFID Reader and Access Control for BeagleBone Black

A register-level RC522 RFID reader and access-control implementation for
the BeagleBone Black.

The project communicates with the RC522 over SPI, controls the RC522
reset line through GPIO using libgpiod, and controls an electronic lock
through a 5 V relay module.

## SPI Device Tree Overlay Configuration

The RC522 uses SPI1 on the BeagleBone Black.

The BeagleBone Black does not expose SPI pins by default. The SPI
controller and header pin multiplexing must be enabled through a Device
Tree Overlay (`.dtbo`).

Known-good configuration:

  Parameter     Value
  ------------- ------------------------
  SPI bus       SPI1
  SPI device    `/dev/spidev1.0`
  Chip select   CS0
  Overlay       `BB-SPIDEV1-00A0.dtbo`

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

    sudo cp BB-SPIDEV1-00A0.dtbo /lib/firmware/

Enable in `/boot/uEnv.txt`:

    uboot_overlay_addr4=/lib/firmware/BB-SPIDEV1-00A0.dtbo

Reboot:

    sudo reboot

Verify:

    ls /dev/spidev*

Expected:

    /dev/spidev1.0

## HDMI / Device Tree Overlay Warning

The BeagleBone Black HDMI cape functionality can consume pins required
by other peripherals.

If SPI, UART, PWM, or other header peripherals do not appear after
enabling an overlay, check for HDMI overlays claiming those pins.

Unused HDMI functionality can be disabled in `/boot/uEnv.txt`:

    disable_uboot_overlay_video=1
    disable_uboot_overlay_audio=1

For this project SPI1 uses:

  Signal           Pin
  ---------------- -------
  SPI1_SCLK        P9.31
  SPI1_D0 (MOSI)   P9.30
  SPI1_D1 (MISO)   P9.29
  SPI1_CS0         P9.28

These pins must not be assigned to another overlay.

## Hardware

The RC522 wiring:

  RC522 Pin   BeagleBone Black Pin
  ----------- ----------------------
  SDA / SS    P9.28
  SCK         P9.31
  MOSI        P9.30
  MISO        P9.29
  RST         P8.7
  3.3V        P9.3 / P9.4
  GND         P9.1 / P9.2

The RC522 is powered from 3.3 V.

## Software Configuration

  Parameter     Value
  ------------- ------------------
  SPI bus       SPI1
  Chip select   CS0
  SPI device    `/dev/spidev1.0`
  SPI mode      0
  SPI speed     1 MHz
  Reset GPIO    P8.7
  Relay GPIO    P9.23

## Known-Good Configuration

  Parameter              Value
  ---------------------- ------------------------
  Board                  BeagleBone Black
  OS                     Debian GNU/Linux 13
  SPI                    SPI1 CS0
  SPI device             `/dev/spidev1.0`
  SPI overlay            `BB-SPIDEV1-00A0.dtbo`
  SPI overlay location   `/lib/firmware`
  SPI mode               0
  SPI speed              1 MHz
  RC522 RST              P8.7
  Relay GPIO             P9.23

Both `rc522_read.py` and `rc522.cpp` should be treated as reference
implementations for this hardware configuration.
