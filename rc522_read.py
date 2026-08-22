#!/usr/bin/env python3

import time
import spidev
import gpiod

CommandReg=0x01
ComIEnReg=0x02
ComIrqReg=0x04
ErrorReg=0x06
FIFODataReg=0x09
FIFOLevelReg=0x0A
ControlReg=0x0C
BitFramingReg=0x0D
ModeReg=0x11
TxModeReg=0x12
RxModeReg=0x13
TxControlReg=0x14
TxASKReg=0x15
CRCResultRegH=0x21
CRCResultRegL=0x22
TModeReg=0x2A
TPrescalerReg=0x2B
TReloadRegH=0x2C
TReloadRegL=0x2D
VersionReg=0x37

PCD_IDLE=0x00
PCD_CALCCRC=0x03
PCD_TRANSCEIVE=0x0C
PICC_REQIDL=0x26
PICC_ANTICOLL=0x93

SPI_BUS=1
SPI_CS=0
GPIO_CHIP='/dev/gpiochip1'
RST_LINE=2
RELAY_GPIO_CHIP='/dev/gpiochip0'
RELAY_LINE=17
PWM_PATH='/dev/bone/pwm/1/a'

spi=spidev.SpiDev()
gpio_request=None
relay_request=None


def write_reg(reg,value):
    address=(reg<<1)&0x7E
    spi.xfer2([address,value])


def read_reg(reg):
    address=((reg<<1)&0x7E)|0x80
    return spi.xfer2([address,0])[1]


def set_bit_mask(reg,mask):
    write_reg(reg,read_reg(reg)|mask)


def clear_bit_mask(reg,mask):
    write_reg(reg,read_reg(reg)&(~mask&0xFF))


def write_pwm_file(name,value):
    try:
        with open(f'{PWM_PATH}/{name}','w') as f:
            f.write(str(value))
        return True
    except OSError:
        return False


def beep_access_granted():
    if not write_pwm_file('period',1000000):
        return
    if not write_pwm_file('duty_cycle',500000):
        return
    if not write_pwm_file('enable',1):
        return
    time.sleep(1.0)
    write_pwm_file('enable',0)


def reset_pin_init():
    global gpio_request
    chip=gpiod.Chip(GPIO_CHIP)
    settings=gpiod.LineSettings(
        direction=gpiod.line.Direction.OUTPUT,
        output_value=gpiod.line.Value.ACTIVE,
    )
    gpio_request=chip.request_lines(consumer='rc522',config={RST_LINE:settings})


def reset_rc522():
    gpio_request.set_value(RST_LINE,gpiod.line.Value.INACTIVE)
    time.sleep(0.05)
    gpio_request.set_value(RST_LINE,gpiod.line.Value.ACTIVE)
    time.sleep(0.05)


def relay_init():
    global relay_request
    chip=gpiod.Chip(RELAY_GPIO_CHIP)
    settings=gpiod.LineSettings(
        direction=gpiod.line.Direction.OUTPUT,
        output_value=gpiod.line.Value.ACTIVE,
        active_low=True,
    )
    relay_request=chip.request_lines(consumer='rc522-relay',config={RELAY_LINE:settings})


def relay_on():
    relay_request.set_value(RELAY_LINE,gpiod.line.Value.INACTIVE)


def relay_off():
    relay_request.set_value(RELAY_LINE,gpiod.line.Value.ACTIVE)


def antenna_on():
    value=read_reg(TxControlReg)
    if not (value&0x03):
        set_bit_mask(TxControlReg,0x03)


def init_rc522():
    reset_rc522()
    write_reg(TModeReg,0x8D)
    write_reg(TPrescalerReg,0x3E)
    write_reg(TReloadRegL,30)
    write_reg(TReloadRegH,0)
    write_reg(TxASKReg,0x40)
    write_reg(ModeReg,0x3D)
    antenna_on()


def request_card():
    write_reg(BitFramingReg,0x07)
    write_reg(CommandReg,PCD_IDLE)
    write_reg(ComIrqReg,0x7F)
    write_reg(FIFOLevelReg,0x80)
    write_reg(FIFODataReg,PICC_REQIDL)
    write_reg(CommandReg,PCD_TRANSCEIVE)
    set_bit_mask(BitFramingReg,0x80)
    for _ in range(100):
        irq=read_reg(ComIrqReg)
        if irq&0x30:
            break
        if irq&0x01:
            return False
        time.sleep(0.001)
    clear_bit_mask(BitFramingReg,0x80)
    return not (read_reg(ErrorReg)&0x1B)


def anticoll():
    write_reg(BitFramingReg,0x00)
    write_reg(CommandReg,PCD_IDLE)
    write_reg(ComIrqReg,0x7F)
    write_reg(FIFOLevelReg,0x80)
    write_reg(FIFODataReg,PICC_ANTICOLL)
    write_reg(FIFODataReg,0x20)
    write_reg(CommandReg,PCD_TRANSCEIVE)
    set_bit_mask(BitFramingReg,0x80)
    completed=False
    for _ in range(100):
        irq=read_reg(ComIrqReg)
        if irq&0x30:
            completed=True
            break
        if irq&0x01:
            return None
        time.sleep(0.001)
    clear_bit_mask(BitFramingReg,0x80)
    if not completed or (read_reg(ErrorReg)&0x1B):
        return None
    if read_reg(FIFOLevelReg)!=5:
        return None
    uid=[read_reg(FIFODataReg) for _ in range(5)]
    bcc=uid[0]^uid[1]^uid[2]^uid[3]
    if bcc!=uid[4]:
        print('Warning: UID BCC mismatch')
    return uid[:4]


try:
    print('Opening SPI...')
    spi.open(SPI_BUS,SPI_CS)
    spi.max_speed_hz=1_000_000
    spi.mode=0
    print('SPI opened')

    print('Initializing RC522 GPIO...')
    reset_pin_init()
    print('Initializing relay...')
    relay_init()
    relay_off()

    print('Resetting RC522...')
    reset_rc522()
    version=read_reg(VersionReg)
    print(f'RC522 VersionReg = 0x{version:02X}')
    if version not in (0x91,0x92):
        print('WARNING: Unexpected RC522 version')
    init_rc522()
    print('RC522 initialized')
    print('Relay initialized on P9.23')
    print('Speaker PWM path:',PWM_PATH)
    print('Place RFID tag on the reader...\n')

    last_uid=None
    while True:
        if request_card():
            uid=anticoll()
            if uid:
                uid_string=' '.join(f'{b:02X}' for b in uid)
                if uid!=last_uid:
                    print(f'UID: {uid_string}')
                    print('RFID detected - opening relay | UNLOCKED')
                    relay_on()
                    beep_access_granted()
                    time.sleep(5)
                    relay_off()
                    print('Relay released | Locked\n')
                    last_uid=uid
        else:
            last_uid=None
        time.sleep(0.2)

except KeyboardInterrupt:
    print('\nStopping...')
finally:
    if relay_request is not None:
        try:
            relay_off(); relay_request.release()
        except Exception: pass
    if gpio_request is not None:
        try: gpio_request.release()
        except Exception: pass
    spi.close()