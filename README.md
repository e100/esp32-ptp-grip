# esp32-ptp-grip

A DIY remote grip for cameras, built on an ESP32-S3. It controls the camera over PTP (WiFi now, USB later) using the [PtpIpCamera](https://github.com/e100/PtpIpCamera) library, so any camera that library supports can use the grip.

Goals: two-stage shutter, record, and quick control of ISO, shutter, aperture, exposure compensation and white balance, with behaviour that adapts to photo vs. video mode. A small OLED shows pairing codes and current settings.

**Status:** early development. Working on a Nikon ZR over WiFi: pairing, exposure settings in photo and video (including shutter angle and dual base ISO), shutter and movie recording. Input handling (encoders, buttons, bindings, layers, lock) is tested on the host. USB control and a fuller display UI are next.

## Hardware

- ESP32-S3 board with a separate UART/COM USB port (USB camera control needs the S3's native USB port)
- 0.91" 128x32 SSD1306 I2C OLED
- Rotary encoders / thumb wheels, push buttons, an optional two-stage shutter switch
- A USB-C (or USB-A) socket for the camera

Pin-by-pin wiring, power and the camera USB port: [docs/WIRING.md](docs/WIRING.md).

## Configuring your grip

Everything about your build lives in [include/grip_config.h](include/grip_config.h):

- **Cameras (WiFi):** list each camera's name, WiFi name (SSID), key and IP in `include/camera_wifi.h`: copy `camera_wifi.example.h` and fill it in. Git ignores that file, so the keys stay out of the repo. The grip joins whichever listed camera is in range. Cameras on USB need no configuration.
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
pio run -t upload && pio device monitor   # build, flash, open serial monitor (through the COM port)
make -C test/host test                    # run the input-logic tests on your computer
```

The current firmware pairs with the camera and controls it: the encoders change exposure settings, the shutter button focuses (half-press) and takes a picture (full press, saved to the card), and REC starts and stops movie recording. In video mode a full shutter press also starts/stops recording, like NX Tether's shutter button (`FULL_PRESS_RECORDS_IN_VIDEO` in `grip_config.h`). While recording, the OLED status line shows `*REC*`. The grip follows the camera's recording state, so recording started or stopped on the camera body is shown too, and the next REC press does the right thing.

Photo/video mode follows the camera's switch, and the encoders adjust the video exposure settings in video mode.

**Shutter mode (ZR, video):** the `shutterModeToggle()` action, or `h` over Serial, switches the video shutter between speed and angle. In angle mode the shutter control steps through the camera's angles (5.6 to 360 degrees), shown as `180 deg`.

**Dual base ISO (ZR, video):** pressing the ISO dial (`baseIsoToggle()`, or `b` over Serial) switches base ISO between Low and High. With `BASE_ISO_SET_NATIVE` (in `grip_config.h`) the grip also sets ISO to that base's native value, 800 for Low or 6400 for High by default. In video mode the ISO display shows the base (`L` or `H`) and marks the native ISO with `*`, for example `ISO 6400 H*`.

Serial commands: `f` autofocuses, `t` presses the shutter (a picture in photo mode, record start/stop in video), `R` starts/stops recording, `v` toggles photo/video mode when no camera is connected, `h` switches the video shutter between speed and angle, `b` switches base ISO (video), `s` shows the link status, `p` starts pairing, `q` disconnects from the camera cleanly (send it before reflashing) and `c` reconnects, `y`/`n` accept or cancel a pairing code, `o` lists the values the camera offers for each setting, `a` dumps the camera's properties (`A` scans every property code), `i` shows what the camera says it supports, `g` lists Nikon's vendor property codes, `w` shows which properties changed since the last `w`, `e` shows the camera's events, `x` writes a property by its code (for experiments), `r` sends any PTP operation and prints the reply, `k` polls Nikon's GetEventEx event queue and prints each event, `d` dumps camera diagnostics, `l` toggles library debug logging.

`tools/ptpdecode.py capture.pcapng` decodes PTP/IP traffic from a Wireshark capture (e.g. of NX Tether talking to the camera), to find which operations and properties another app uses.

## WiFi or USB: automatic

The grip uses whichever connection is available, with no reflashing:

- **Camera plugged into the grip's USB port:** the grip turns its WiFi off and controls the camera over USB. No pairing is needed.
- **Nothing on USB:** WiFi comes on and the grip joins a camera from the list in `camera_wifi.h` (the strongest signal if several are in range), pairing the first time.
- **Unplugging (or switching off) the USB camera** brings WiFi back within a couple of seconds.

The OLED shows the connected camera's model, with "USB" when it's on the cable.

Because the S3's native USB pins (GPIO 19/20) belong to the camera, the serial console and uploads use the UART port ("COM" on most boards). Boards without a separate COM port can use the `esp32s3-wifi` environment instead: WiFi only, console on the native USB port.

### Wiring the camera port

The port must supply 5 V and look like an ordinary 500 mA host port, so the camera doesn't try to charge from it (a ZR draws about 25 mA from such a port):

- **USB-C socket:** GPIO 19 → D−, GPIO 20 → D+, 5V → VBUS, GND → GND, and a 56 kΩ resistor (47–56 kΩ) from **each** CC pin (A5 and B5) to 5 V. With a resistor on only one CC pin, the cable works in one orientation only. Remove any 5.1 kΩ resistors a breakout has on CC.
- **USB-A socket:** the same four wires, no resistors; connect the camera with a USB-C to USB-A cable.
- A current-limited load switch (about 0.5 A) on VBUS is a good idea in a finished grip.

On the camera, set USB to MTP/PTP.

## Pairing with the camera (Nikon ZR)

1. On the camera: Network → Connect to computer, connection type **Camera Control**. Create a profile with **Direct connection to computer**. Each profile can be paired to one device, so use a new profile if this one is already paired to something else.
2. Add the camera to `CAMERA_WIFI_LIST` in `include/camera_wifi.h` (copied from `camera_wifi.example.h`): a name, the SSID and key the camera shows, and `192.168.1.1`. Then build and flash.
3. The grip joins the camera's WiFi and shows a pairing code. If it matches the code on the camera, press any grip button.
4. Confirm the grip on the camera. The grip reconnects and shows **Connected**.

The grip keeps a random ID (GUID) in flash, and the camera remembers that ID, so pairing happens once. Erasing the whole flash makes a new ID, and then you need to pair again with a fresh profile.

`platformio.ini` currently points at a local checkout of PtpIpCamera (`../PtpIpCamera`) while Nikon support is being developed.
