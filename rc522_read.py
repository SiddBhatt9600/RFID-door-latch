#!/usr/bin/env python3

import time
import spidev
import gpiod


# =========================
# RC522 registers
# =========================

CommandReg     = 0x01
ComIEnReg      = 0x02
ComIrqReg      = 0x04
ErrorReg       = 0x06
FIFODataReg    = 0x09
FIFOLevelReg   = 0x0A
ControlReg     = 0x0C
BitFramingReg  = 0x0D
ModeReg        = 0x11
TxModeReg      = 0x12
RxModeReg      = 0x13
TxControlReg   = 0x14
TxASKReg       = 0x15
CRCResultRegH  = 0x21
CRCResultRegL  = 0x22
TModeReg       = 0x2A
TPrescalerReg  = 0x2B
TReloadRegH    = 0x2C
TReloadRegL    = 0x2D
VersionReg     = 0x37


# =========================
# RC522 commands
# =========================

PCD_IDLE       = 0x00
PCD_CALCCRC    = 0x03
PCD_TRANSCEIVE = 0x0C

PICC_REQIDL    = 0x26
PICC_ANTICOLL  = 0x93


# =========================
# Hardware configuration
# =========================

# RC522 SPI
SPI_BUS = 1
SPI_CS = 0

# RC522 Reset
# P8.7 = gpiochip1 line 2
GPIO_CHIP = "/dev/gpiochip1"
RST_LINE = 2

# Relay
# P9.23 = gpiochip0 line 17
RELAY_GPIO_CHIP = "/dev/gpiochip0"
RELAY_LINE = 17


# =========================
# SPI
# =========================

spi = spidev.SpiDev()


def write_reg(reg, value):
    address = (reg << 1) & 0x7E
    spi.xfer2([address, value])


def read_reg(reg):
    address = ((reg << 1) & 0x7E) | 0x80
    response = spi.xfer2([address, 0x00])
    return response[1]


def set_bit_mask(reg, mask):
    write_reg(reg, read_reg(reg) | mask)


def clear_bit_mask(reg, mask):
    write_reg(reg, read_reg(reg) & (~mask & 0xFF))


# =========================
# RC522 GPIO / Reset
# =========================

gpio_request = None


def reset_pin_init():
    global gpio_request

    chip = gpiod.Chip(GPIO_CHIP)

    settings = gpiod.LineSettings(
        direction=gpiod.line.Direction.OUTPUT,
        output_value=gpiod.line.Value.ACTIVE
    )

    gpio_request = chip.request_lines(
        consumer="rc522",
        config={
            RST_LINE: settings
        }
    )


def reset_rc522():
    gpio_request.set_value(
        RST_LINE,
        gpiod.line.Value.INACTIVE
    )

    time.sleep(0.05)

    gpio_request.set_value(
        RST_LINE,
        gpiod.line.Value.ACTIVE
    )

    time.sleep(0.05)


# =========================
# Relay GPIO
# =========================

relay_request = None


def relay_init():
    global relay_request

    chip = gpiod.Chip(RELAY_GPIO_CHIP)

    settings = gpiod.LineSettings(
        direction=gpiod.line.Direction.OUTPUT,
        output_value=gpiod.line.Value.ACTIVE,
        active_low=True
    )

    relay_request = chip.request_lines(
        consumer="rc522-relay",
        config={
            RELAY_LINE: settings
        }
    )


def relay_on():
    relay_request.set_value(
        RELAY_LINE,
        gpiod.line.Value.INACTIVE
    )


def relay_off():
    relay_request.set_value(
        RELAY_LINE,
        gpiod.line.Value.ACTIVE
    )


# =========================
# RC522 initialization
# =========================

def antenna_on():
    value = read_reg(TxControlReg)

    if not (value & 0x03):
        set_bit_mask(TxControlReg, 0x03)


def init_rc522():

    reset_rc522()

    write_reg(TModeReg, 0x8D)
    write_reg(TPrescalerReg, 0x3E)

    write_reg(TReloadRegL, 30)
    write_reg(TReloadRegH, 0)

    write_reg(TxASKReg, 0x40)
    write_reg(ModeReg, 0x3D)

    antenna_on()


# =========================
# RFID request
# =========================

def request():

    # Tell RC522 that we are sending 7 bits
    write_reg(BitFramingReg, 0x07)

    write_reg(CommandReg, PCD_IDLE)
    write_reg(ComIrqReg, 0x7F)

    # Clear FIFO
    write_reg(FIFOLevelReg, 0x80)

    # REQA command
    write_reg(FIFODataReg, PICC_REQIDL)

    # Start transceive
    write_reg(CommandReg, PCD_TRANSCEIVE)

    # Start transmission
    set_bit_mask(BitFramingReg, 0x80)

    for _ in range(100):

        irq = read_reg(ComIrqReg)

        if irq & 0x30:
            break

        if irq & 0x01:
            return False

        time.sleep(0.001)

    clear_bit_mask(BitFramingReg, 0x80)

    error = read_reg(ErrorReg)

    if error & 0x1B:
        return False

    return True


# =========================
# UID anti-collision
# =========================

def anticoll():

    write_reg(BitFramingReg, 0x00)

    write_reg(CommandReg, PCD_IDLE)
    write_reg(ComIrqReg, 0x7F)

    # Clear FIFO
    write_reg(FIFOLevelReg, 0x80)

    # Anti-collision command
    write_reg(FIFODataReg, PICC_ANTICOLL)

    # Cascade level 1
    write_reg(FIFODataReg, 0x20)

    # Transceive
    write_reg(CommandReg, PCD_TRANSCEIVE)

    set_bit_mask(BitFramingReg, 0x80)

    for _ in range(100):

        irq = read_reg(ComIrqReg)

        if irq & 0x30:
            break

        if irq & 0x01:
            return None

        time.sleep(0.001)

    clear_bit_mask(BitFramingReg, 0x80)

    error = read_reg(ErrorReg)

    if error & 0x1B:
        return None

    length = read_reg(FIFOLevelReg)

    if length != 5:
        return None

    uid = []

    for _ in range(5):
        uid.append(read_reg(FIFODataReg))

    # Last byte is BCC
    calculated_bcc = (
        uid[0] ^
        uid[1] ^
        uid[2] ^
        uid[3]
    )

    if calculated_bcc != uid[4]:
        print("Warning: UID BCC mismatch")

    return uid[:4]


# =========================
# Main
# =========================

try:

    print("Opening SPI...")

    spi.open(SPI_BUS, SPI_CS)

    spi.max_speed_hz = 1_000_000
    spi.mode = 0

    print("SPI opened")

    print("Initializing RC522 GPIO...")
    reset_pin_init()

    print("Initializing relay...")
    relay_init()

    # Relay is active-low, therefore start OFF
    relay_off()

    print("Resetting RC522...")
    reset_rc522()

    version = read_reg(VersionReg)

    print(f"RC522 VersionReg = 0x{version:02X}")

    if version not in (0x91, 0x92):
        print("WARNING: Unexpected RC522 version")

    init_rc522()

    print("RC522 initialized")
    print("Relay initialized on P9.23")
    print("Place RFID tag on the reader...")
    print()

    last_uid = None

    while True:

        if request():

            uid = anticoll()

            if uid:

                uid_string = " ".join(
                    f"{byte:02X}" for byte in uid
                )

                if uid != last_uid:

                    print(f"UID: {uid_string}")
                    print("RFID detected - opening relay | UNLOCKED")

                    # Relay is active-low
                    relay_on()

                    # Keep relay energized for 5 seconds
                    time.sleep(5)

                    relay_off()

                    print("Relay released | Locked")
                    print()

                    last_uid = uid

        else:
            last_uid = None

        time.sleep(0.2)


except KeyboardInterrupt:

    print("\nStopping...")


finally:

    # Always make sure the relay is OFF
    if relay_request is not None:
        try:
            relay_off()
            relay_request.release()
        except Exception:
            pass

    if gpio_request is not None:
        try:
            gpio_request.release()
        except Exception:
            pass

    spi.close()
