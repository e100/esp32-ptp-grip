# Wiring

How to wire a grip that matches the default configuration in [`include/grip_config.h`](../include/grip_config.h). The pin numbers in this guide match that file. If you change a pin there, change it here too.

Built and tested on a Lonely Binary ESP32-S3 N16R8 dev board (a DevKitC-style board with two USB-C ports: "USB", the S3's native port, and "COM", a CH340 USB-to-serial chip). Any ESP32-S3 board with a separate UART/COM port works the same way.

## Pin summary

| GPIO | Connects to | In `grip_config.h` |
|---|---|---|
| 4 | Thumb wheel A | `ENCODERS[0]` "wheel" |
| 5 | Thumb wheel B | `ENCODERS[0]` |
| 6 | ISO dial A | `ENCODERS[1]` "iso" |
| 7 | ISO dial B | `ENCODERS[1]` |
| 8 | OLED SDA | `OLED` |
| 9 | OLED SCL | `OLED` |
| 10 | Fn button (optional, not fitted on the prototype) | `BUTTONS[4]` "fn" |
| 15 | Shutter half-press switch | `BUTTONS[0]` "half" |
| 16 | Shutter full-press switch | `BUTTONS[1]` "full" |
| 17 | REC button | `BUTTONS[2]` "rec" |
| 18 | Thumb wheel push switch | `BUTTONS[3]` "push" |
| 19 | Camera USB port D− | fixed: the S3's USB pins |
| 20 | Camera USB port D+ | fixed |
| 21 | ISO dial push switch | `BUTTONS[5]` "isopush" |
| 43 / 44 | UART TX / RX to the COM port | fixed: on the board already |

Every input's other side goes to **GND**. The firmware turns on the internal pull-ups, so no resistors are needed for switches and encoders.

## Power

- During development, power the board from its **COM** USB-C port. That port is also the serial console and the upload port.
- **Don't connect a computer to the native "USB" port.** In this build that port's pins (GPIO 19/20) belong to the camera, and the board's 5 V feeds the camera port.
- The board's **5V** pin supplies the camera port's VBUS. Check that it reads close to 5 V when powered from COM: some boards drop it through a diode.
- **3V3** supplies the OLED.

A finished grip would run from a battery through a 5 V boost converter into the 5V pin. See [Camera USB port](#camera-usb-port) for what the camera needs.

## OLED (SSD1306, 128×32, I2C)

| OLED pin | ESP32-S3 |
|---|---|
| GND | GND |
| VCC | 3V3 |
| SDA | GPIO 8 |
| SCL | GPIO 9 |

Most of these modules include I2C pull-up resistors and accept 3.3 V or 5 V; 3.3 V keeps the I2C lines at the ESP32's logic level. Set `OLED.enabled` to `false` in `grip_config.h` to run without a display.

## Encoders (thumb wheel and ISO dial)

Each rotary encoder has three pins for rotation: A, B and a common pin. Encoders with a push switch have two more pins for it.

| Encoder pin | Thumb wheel | ISO dial |
|---|---|---|
| A | GPIO 4 | GPIO 6 |
| B | GPIO 5 | GPIO 7 |
| Common (C, usually the middle pin) | GND | GND |
| Switch (one side) | GPIO 18 | GPIO 21 |
| Switch (other side) | GND | GND |

Bare EC11-style encoders and thumb wheels need nothing else. Encoder modules with a "+" pin (KY-040 style) have their own pull-ups: connect "+" to 3V3, never 5V.

Two encoder settings in `grip_config.h` depend on the part:

- **counts per click:** 2 for the prototype's encoders. If only every other click registers, halve it; if one click moves two steps, double it.
- **reverse:** flips the direction.

## Buttons and the two-stage shutter

Each button is a switch between its GPIO and GND (`activeLow` = true).

| Button | GPIO |
|---|---|
| Shutter half-press | 15 |
| Shutter full-press | 16 |
| REC | 17 |
| Fn (optional) | 10 |

A two-stage shutter switch has two contacts: half-press closes the first (GPIO 15), and pressing further also closes the second (GPIO 16). Wire both contacts' common side to GND. Two separate buttons work too.

## Camera USB port

The grip is the USB host. The port must supply 5 V to the camera and look like an ordinary 500 mA computer port, so the camera doesn't try to charge from it. A Nikon ZR draws about 25 mA from a port like this, and up to 1.5 A from one that advertises charging.

**USB-C socket (recommended):**

| USB-C socket | ESP32-S3 |
|---|---|
| D− (A7/B7) | GPIO 19 |
| D+ (A6/B6) | GPIO 20 |
| VBUS | 5V |
| GND | GND |
| CC1 (A5) | 47–56 kΩ to 5V |
| CC2 (B5) | 47–56 kΩ to 5V |

- Fit a resistor on **both** CC pins. With only one, the cable works in one orientation only (the prototype breakout has only A5 brought out, so it has this limitation).
- **Never use 22 kΩ or 10 kΩ:** those advertise 1.5 A and 3 A, and the camera would start charging.
- Remove any 5.1 kΩ resistors a breakout board has on CC. Those mark a device port, not a host port.
- Leave the SuperSpeed pins (A2/A3/A10/A11, B2/B3/B10/B11) and SBU (A8/B8) unconnected.

**USB-A socket (alternative):** the same four wires (D−, D+, VBUS, GND) and no resistors. Use a USB-C to USB-A cable to the camera: the resistor is inside the cable.

**For a finished grip,** add a current-limited load switch (about 0.5 A, e.g. AP2552 or TPS2553) between 5V and VBUS, so a cable fault or a camera that tries to charge can't pull the grip's supply down.

On the camera, set USB to **MTP/PTP**.

## Pins to avoid on the ESP32-S3

- **GPIO 0, 3, 45, 46:** strapping pins, read at boot.
- **GPIO 19, 20:** USB, used for the camera.
- **GPIO 26–32:** flash.
- **GPIO 33–37:** octal PSRAM on N16R8 and other R8 modules.
- **GPIO 43, 44:** UART to the COM port.

Free for more inputs: GPIO 1, 2, 11–14, 38–42, 47, 48. Many DevKitC-style boards drive an RGB LED from GPIO 48 or 38, so check your board before using those two.
