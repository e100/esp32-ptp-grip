# esp32-ptp-grip

A DIY remote grip for cameras, built on an ESP32-S3. It controls the camera over PTP (WiFi now, USB later) using the [PtpIpCamera](https://github.com/e100/PtpIpCamera) library, so any camera that library supports can use the grip.

Goals: two-stage shutter, record, and quick control of ISO, shutter, aperture, exposure compensation and white balance, with behaviour that adapts to photo vs. video mode. A small OLED shows pairing codes and current settings.

**Status:** early development. Input handling (encoders, buttons, bindings, layers, lock) works and is tested. Camera control, the display UI and pairing screens are next. First target camera: Nikon ZR.

## Hardware

- ESP32-S3 board (needed for USB camera control later; WiFi works on any ESP32)
- 0.91" 128x32 SSD1306 I2C OLED
- Rotary encoders / thumb wheels, push buttons, an optional two-stage shutter switch, LEDs

## Configuring your grip

Everything about your build lives in [include/grip_config.h](include/grip_config.h):

- **`ENCODERS[]`**: the A/B pins of each encoder or thumb wheel. An encoder's push switch is configured as an ordinary button, so wheels with no switch work too.
- **`BUTTONS[]`**: one pin per button or switch.
- **`BINDINGS[]`**: what each input does. Each row says which input, what triggers it (`Turn`, `Hold`, `Short`, `Long`), in which camera mode (`MODE_PHOTO`, `MODE_VIDEO`, `MODE_ANY`) and on which layer. The first matching row wins.

Examples:

```cpp
// This encoder is always ISO
{ encoder(1), Trigger::Turn,  MODE_ANY,   LAYER_ANY, adjust(Setting::ISO) },

// The thumb wheel adjusts EC while Fn is held, otherwise whatever setting is selected
{ encoder(0), Trigger::Turn,  MODE_ANY,   layer(1),  adjust(Setting::ExposureComp) },
{ encoder(0), Trigger::Turn,  MODE_ANY,   LAYER_ANY, adjustSelected() },

// Fn held = layer 1; a slide switch can lock the controls
{ button(4),  Trigger::Hold,  MODE_ANY,   LAYER_ANY, layerHold(1) },
{ button(5),  Trigger::Hold,  MODE_ANY,   LAYER_ANY, lock() },
```

Available actions: `adjust(setting)`, `adjustSelected()`, `selectNext()`, `selectPrev()`, `shutterHalf()`, `shutterFull()`, `recordToggle()`, `layerHold(n)`, `layerNext()`, `lock()`, `lockToggle()`.

While locked, only record and unlock work. Releasing a button that was already held still goes through, so a held shutter is never left pressed.

`PHOTO_SETTINGS[]` and `VIDEO_SETTINGS[]` set the order `selectNext()` steps through in each mode. The grip remembers the selection separately for each mode.

## Building

Install [PlatformIO](https://platformio.org/), then:

```sh
pio run -t upload && pio device monitor   # build, flash, open serial monitor
make -C test/host test                    # run the input-logic tests on your computer
```

The current firmware is an input bring-up tool. It prints each control's action to Serial and the OLED. Send `v` over Serial to toggle photo/video mode.

`platformio.ini` currently points at a local checkout of PtpIpCamera (`../PtpIpCamera`) while Nikon support is being developed.
