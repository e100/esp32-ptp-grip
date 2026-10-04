# esp32-ptp-grip

A DIY remote grip for cameras, built on an ESP32-S3. It controls the camera over PTP (WiFi now, USB later) using the [PtpIpCamera](https://github.com/e100/PtpIpCamera) library, so any camera that library supports can use the grip.

Goals: two-stage shutter, record, and quick control of ISO, shutter, aperture, exposure compensation and white balance, with behaviour that adapts to photo vs. video mode. A small OLED shows pairing codes and current settings.

**Status:** early development. Working on a Nikon ZR over WiFi: pairing, exposure settings in photo and video (including shutter angle and dual base ISO), shutter and movie recording. Input handling (encoders, buttons, bindings, layers, lock) is tested on the host. USB control and a fuller display UI are next.

## Hardware

- ESP32-S3 board (needed for USB camera control later; WiFi works on any ESP32)
- 0.91" 128x32 SSD1306 I2C OLED
- Rotary encoders / thumb wheels, push buttons, an optional two-stage shutter switch, LEDs

## Configuring your grip

Everything about your build lives in [include/grip_config.h](include/grip_config.h):

- **`CAMERA`**: the camera's IP address. Its WiFi name (SSID) and key go in `include/camera_wifi.h`: copy `camera_wifi.example.h` and fill them in. Git ignores that file, so the key stays out of the repo. Leave the SSID empty to run without a camera.
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

Available actions: `adjust(setting)`, `adjustSelected()`, `selectNext()`, `selectPrev()`, `shutterHalf()`, `shutterFull()`, `recordToggle()`, `layerHold(n)`, `layerNext()`, `lock()`, `lockToggle()`, `baseIsoToggle()`, `shutterModeToggle()`.

While locked, only record and unlock work. Releasing a button that was already held still goes through, so a held shutter is never left pressed.

`PHOTO_SETTINGS[]` and `VIDEO_SETTINGS[]` set the order `selectNext()` steps through in each mode. The grip remembers the selection separately for each mode.

## Building

Install [PlatformIO](https://platformio.org/), then:

```sh
pio run -t upload && pio device monitor   # build, flash, open serial monitor
make -C test/host test                    # run the input-logic tests on your computer
```

The current firmware pairs with the camera and controls it: the encoders change exposure settings, the shutter button focuses (half-press) and takes a picture (full press, saved to the card), and REC starts and stops movie recording. In video mode a full shutter press also starts/stops recording, like NX Tether's shutter button (`FULL_PRESS_RECORDS_IN_VIDEO` in `grip_config.h`). While recording, the OLED status line shows `*REC*`. Recording started from the camera body isn't detected, so the grip's REC state can get out of step if both are used.

Photo/video mode follows the camera's switch, and the encoders adjust the video exposure settings in video mode.

**Shutter mode (ZR, video):** the `shutterModeToggle()` action, or `h` over Serial, switches the video shutter between speed and angle. In angle mode the shutter control steps through the camera's angles (5.6 to 360 degrees), shown as `180 deg`.

**Dual base ISO (ZR, video):** pressing the ISO dial (`baseIsoToggle()`, or `b` over Serial) switches base ISO between Low and High. With `BASE_ISO_SET_NATIVE` (in `grip_config.h`) the grip also sets ISO to that base's native value, 800 for Low or 6400 for High by default. In video mode the ISO display shows the base (`L` or `H`) and marks the native ISO with `*`, for example `ISO 6400 H*`.

Serial commands: `f` autofocuses, `t` presses the shutter (a picture in photo mode, record start/stop in video), `R` starts/stops recording, `v` toggles photo/video mode when no camera is connected, `h` switches the video shutter between speed and angle, `b` switches base ISO (video), `s` shows the link status, `p` starts pairing, `q` disconnects from the camera cleanly (send it before reflashing) and `c` reconnects, `y`/`n` accept or cancel a pairing code, `o` lists the values the camera offers for each setting, `a` dumps the camera's properties (`A` scans every property code), `i` shows what the camera says it supports, `g` lists Nikon's vendor property codes, `w` shows which properties changed since the last `w`, `e` shows the camera's events, `x` writes a property by its code (for experiments), `r` sends any PTP operation and prints the reply, `k` polls Nikon's GetEventEx event queue and prints each event, `d` dumps camera diagnostics, `l` toggles library debug logging.

`tools/ptpdecode.py capture.pcapng` decodes PTP/IP traffic from a Wireshark capture (e.g. of NX Tether talking to the camera), to find which operations and properties another app uses.

## Pairing with the camera (Nikon ZR)

1. On the camera: Network → Connect to computer, connection type **Camera Control**. Create a profile with **Direct connection to computer**. Each profile can be paired to one device, so use a new profile if this one is already paired to something else.
2. Put the SSID and key the camera shows into `include/camera_wifi.h` (copied from `camera_wifi.example.h`), then build and flash.
3. The grip joins the camera's WiFi and shows a pairing code. If it matches the code on the camera, press any grip button.
4. Confirm the grip on the camera. The grip reconnects and shows **Connected**.

The grip keeps a random ID (GUID) in flash, and the camera remembers that ID, so pairing happens once. Erasing the whole flash makes a new ID, and then you need to pair again with a fresh profile.

`platformio.ini` currently points at a local checkout of PtpIpCamera (`../PtpIpCamera`) while Nikon support is being developed.
