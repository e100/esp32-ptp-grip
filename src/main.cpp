// esp32-ptp-grip — input bring-up and camera pairing.
//
// Reads the encoders and buttons from include/grip_config.h, runs them through
// the binding engine, and shows the resulting commands on Serial and the OLED.
// Joins the camera's WiFi and pairs with it (see CameraLink.h). Encoder steps
// change the camera's settings (CameraControl.h); the shutter button focuses
// and shoots, REC starts/stops movies. Photo/video mode follows the camera's switch.
//
// Serial: 'v' toggle photo/video mode (no camera only), 's' link status,
//         'p' pair, 'q' disconnect cleanly (before reflashing), 'c' reconnect,
//         'b' toggle base ISO (video),
//         'h' toggle shutter speed/angle (video), 'f' autofocus, 't' shutter
//         press, 'R' record start/stop, 'y' accept pairing code, 'n' cancel pairing, 'o' list the
//         values the camera offers, 'a' dump the listed camera properties,
//         'A' scan every property code, 'w' show properties changed since
//         the last 'w', 'x' write a property by code, 'r' send any PTP
//         operation and show the reply, 'k' poll Nikon GetEventEx, 'e' toggle showing
//         camera events, 'i' DeviceInfo, 'g' Nikon vendor property codes,
//         'd' camera diagnostics, 'l' toggle library debug logging.

#include <Arduino.h>
#include <ESP32Encoder.h>
#include <U8g2lib.h>
#include <PtpIpCamera.h>
#include "BindingEngine.h"
#include "CameraLink.h"
#include "CameraControl.h"
#include "PropDump.h"
#include "InputFilters.h"
#include "grip_config.h"

using namespace grip;

#define COUNT(a) (sizeof(a) / sizeof((a)[0]))
static constexpr size_t NUM_ENCODERS = COUNT(config::ENCODERS);
static constexpr size_t NUM_BUTTONS  = COUNT(config::BUTTONS);

static ESP32Encoder  encoders[NUM_ENCODERS];
static EncoderFilter encoderFilters[NUM_ENCODERS];
static ButtonFilter  buttonFilters[NUM_BUTTONS];

static BindingEngine engine(config::BINDINGS, COUNT(config::BINDINGS),
                            { config::PHOTO_SETTINGS, (uint8_t)COUNT(config::PHOTO_SETTINGS) },
                            { config::VIDEO_SETTINGS, (uint8_t)COUNT(config::VIDEO_SETTINGS) },
                            config::LAYER_COUNT);

static U8G2_SSD1306_128X32_UNIVISION_F_HW_I2C display(U8G2_R0, U8X8_PIN_NONE,
                                                       config::OLED.sclPin, config::OLED.sdaPin);

static PtpIpTransport transport;
static PtpIpSession   session(transport);
static NikonCamera    camera(session);
static CameraLink     camLink(camera, session);
static CameraControl  control(camera);
static bool           debugLog = false;
static bool           showEvents = false;
static bool           pollEventEx = false;   // 'k': poll Nikon GetEventEx and print what it returns
static uint32_t       lastEventEx = 0;

static char lastLine[24] = "ready";

static void describe(const Command& c, char* buf, size_t len) {
    switch (c.type) {
        case CommandType::StepSetting:      snprintf(buf, len, "%s %+d", settingName(c.setting), c.delta); break;
        case CommandType::ShutterHalf:      snprintf(buf, len, "half %s", c.on ? "on" : "off"); break;
        case CommandType::ShutterFull:      snprintf(buf, len, "full %s", c.on ? "on" : "off"); break;
        case CommandType::RecordToggle:     snprintf(buf, len, "record"); break;
        case CommandType::SelectionChanged: snprintf(buf, len, "sel %s", settingName(c.setting)); break;
        case CommandType::LayerChanged:     snprintf(buf, len, "layer %d", c.delta); break;
        case CommandType::LockChanged:      snprintf(buf, len, "%s", c.on ? "LOCKED" : "unlocked"); break;
        case CommandType::BaseIsoToggle:    snprintf(buf, len, "base ISO"); break;
        case CommandType::ShutterModeToggle: snprintf(buf, len, "shutter mode"); break;
    }
}

// Until the camera is connected the display shows the link (pairing code etc.);
// after that, the last control used and the grip's state.
static void redraw() {
    if (!config::OLED.enabled) return;
    const char* top = lastLine;
    char status[32];
    if (camLink.connected() || camLink.state() == CameraLink::State::Disabled) {
        snprintf(status, sizeof(status), "%s L%u  %s%s",
                 control.recording() ? "*REC*" : engine.cameraMode() == CameraMode::Photo ? "PHOTO" : "VIDEO",
                 engine.activeLayer(), settingName(engine.selected()),
                 engine.locked() ? "  LOCK" : "");
    } else {
        top = camLink.headline();
        snprintf(status, sizeof(status), "%s", camLink.detail());
    }
    display.clearBuffer();
    display.setFont(u8g2_font_9x15B_tr);
    display.drawStr(0, 14, top);
    display.setFont(u8g2_font_6x10_tr);
    display.drawStr(0, 30, status);
    display.sendBuffer();
}

static void onLibraryLog(uint8_t level, const char* msg) {
    if (level >= PTPIP_LOG_INFO || debugLog || (showEvents && strstr(msg, "] event "))) Serial.println(msg);
}

// A setting changed on the camera itself (dial, menu) — show it.
static void onPropChanged(CameraProperty prop, uint32_t value, void*) {
    Setting s;
    if (!CameraControl::settingFor(prop, s)) return;
    int32_t v = prop == CameraProperty::EXPOSURE_COMPENSATION ? (int16_t)value : (int32_t)value;
    CameraControl::format(s, v, camera, lastLine, sizeof(lastLine));
    Serial.printf("camera: %s\n", lastLine);
    redraw();
}

static void printOptions() {
    static NikonPropOptions o;
    static const CameraProperty props[] = { CameraProperty::ISO, CameraProperty::SHUTTER_SPEED,
        CameraProperty::APERTURE, CameraProperty::EXPOSURE_COMPENSATION, CameraProperty::WHITE_BALANCE };
    for (CameraProperty p : props) {
        Setting s;
        CameraControl::settingFor(p, s);
        CameraResult r = camera.getPropOptions(p, o);
        if (r != CAM_OK) {
            Serial.printf("%-8s %s\n", settingName(s), cameraResultStr(r));
            continue;
        }
        char cur[24];
        CameraControl::format(s, o.current, camera, cur, sizeof(cur));
        Serial.printf("%-8s now %s (%ld), %s", settingName(s), cur, (long)o.current,
                      o.writable ? "writable" : "read-only");
        if (o.form == NikonPropOptions::Form::Range) {
            Serial.printf(", range %ld..%ld step %ld\n", (long)o.min, (long)o.max, (long)o.step);
        } else {
            Serial.printf(", %u values%s:", (unsigned)o.count, o.truncated ? " (truncated)" : "");
            for (uint16_t i = 0; i < o.count; i++) Serial.printf(" %ld", (long)o.values[i]);
            Serial.println();
        }
    }
}

// Read one non-empty line from Serial, waiting up to 30 s. Skips the line
// ending left over from the Enter pressed after the command letter.
static String readLine() {
    Serial.setTimeout(30000);
    String line;
    uint32_t start = millis();
    while (line.length() == 0 && millis() - start < 30000) {
        line = Serial.readStringUntil('\n');
        line.trim();
    }
    return line;
}

// "x D0B5 6400": write a property by hex code and decimal value, then read it back.
static void writeProperty() {
    Serial.println("write: enter <hex code> <decimal value>, e.g. D0B5 6400");
    String line = readLine();
    char* end;
    unsigned long code = strtoul(line.c_str(), &end, 16);
    if (end == line.c_str() || code == 0) {
        Serial.printf("write: can't read a property code from '%s'\n", line.c_str());
        return;
    }
    char* end2;
    long value = strtol(end, &end2, 10);
    if (end2 == end) {
        Serial.printf("write: can't read a value from '%s'\n", line.c_str());
        return;
    }
    Serial.print("before: ");
    dumpCameraProp(camera, (uint32_t)code, Serial);
    CameraResult r = camera.setPropertyRaw((uint32_t)code, (uint32_t)value);
    Serial.printf("write %04lX = %ld: %s\n", code, value, cameraResultStr(r));
    delay(300);
    Serial.print("after:  ");
    dumpCameraProp(camera, (uint32_t)code, Serial);
}

// "r 9435 1": send any PTP operation (hex opcode, up to 5 hex parameters) as a
// data-in transaction and print the response code and any data received.
static void rawOperation() {
    Serial.println("raw: enter <hex opcode> [hex params...], e.g. 1014 1D0A5");
    String line = readLine();
    const char* p = line.c_str();
    char* end;
    unsigned long op = strtoul(p, &end, 16);
    if (end == p || op == 0 || op > 0xFFFF) {
        Serial.printf("raw: can't read an opcode from '%s'\n", line.c_str());
        return;
    }
    uint32_t params[5];
    uint8_t n = 0;
    while (n < 5) {
        p = end;
        unsigned long v = strtoul(p, &end, 16);
        if (end == p) break;
        params[n++] = (uint32_t)v;
    }
    if (!camLink.connected()) {
        Serial.println("raw: camera not connected");
        return;
    }
    static uint8_t buf[4096];
    size_t len = 0;
    uint16_t response = 0;
    bool ok = session.dataIn((uint16_t)op, params, n, buf, sizeof(buf), len, response);
    Serial.printf("raw %04lX", op);
    for (uint8_t i = 0; i < n; i++) Serial.printf(" %lX", (unsigned long)params[i]);
    if (!ok) {
        Serial.println(": transaction failed");
        return;
    }
    Serial.printf(": response 0x%04X, %u data bytes\n", response, (unsigned)len);
    size_t shown = len < 512 ? len : 512;
    for (size_t i = 0; i < shown; i++) {
        if (i % 16 == 0) Serial.printf("%s  %04X:", i ? "\n" : "", (unsigned)i);
        Serial.printf(" %02X", buf[i]);
    }
    if (shown) Serial.println(len > shown ? "\n  ..." : "");
}

// Nikon GetEventEx (0x941C), polled as NX Tether does. Unlike the PTP/IP event
// channel it also reports changes to 32-bit properties. Data: UINT32 count,
// then per event UINT16 code, UINT16 parameter count, UINT32 parameters.
static void pollNikonEvents() {
    static uint8_t buf[2048];
    size_t len = 0;
    uint16_t response = 0;
    if (!session.dataIn(0x941C, nullptr, 0, buf, sizeof(buf), len, response)) {
        Serial.println("GetEventEx: transaction failed, polling stopped");
        pollEventEx = false;
        return;
    }
    if (response != 0x2001) {
        Serial.printf("GetEventEx: response 0x%04X, polling stopped\n", response);
        pollEventEx = false;
        return;
    }
    if (len < 4) return;
    uint32_t n = (uint32_t)buf[0] | ((uint32_t)buf[1] << 8) | ((uint32_t)buf[2] << 16) | ((uint32_t)buf[3] << 24);
    size_t pos = 4;
    for (uint32_t i = 0; i < n && pos + 4 <= len; i++) {
        uint16_t code = (uint16_t)(buf[pos] | (buf[pos + 1] << 8));
        uint16_t np   = (uint16_t)(buf[pos + 2] | (buf[pos + 3] << 8));
        pos += 4;
        Serial.printf("eventEx %04X", code);
        for (uint16_t k = 0; k < np && pos + 4 <= len; k++, pos += 4) {
            uint32_t p = (uint32_t)buf[pos] | ((uint32_t)buf[pos + 1] << 8) |
                         ((uint32_t)buf[pos + 2] << 16) | ((uint32_t)buf[pos + 3] << 24);
            Serial.printf(" %lX", (unsigned long)p);
        }
        Serial.println(code == 0x4006 ? "   (property changed)" : (code & 0xF000) == 0xC000 ? "   (Nikon event)" : "");
    }
}

static void onSerial(char c) {
    switch (c) {
        case 'v':
            if (camLink.connected()) {
                Serial.println("mode follows the camera's photo/video switch");
                break;
            }
            engine.setCameraMode(engine.cameraMode() == CameraMode::Photo ? CameraMode::Video : CameraMode::Photo);
            snprintf(lastLine, sizeof(lastLine), "mode %s", engine.cameraMode() == CameraMode::Photo ? "photo" : "video");
            Serial.println(lastLine);
            redraw();
            break;
        case 's': camLink.printStatus(Serial); break;
        case 'p': camLink.forcePair(); break;
        case 'q': camLink.disconnect(); break;
        case 'c': camLink.connect(); break;
        case 'y': camLink.onButtonPress(); break;
        case 'n': camLink.cancel(); break;
        case 'o': printOptions(); break;
        case 'a': dumpCameraProps(camera, Serial, false); break;
        case 'A': dumpCameraProps(camera, Serial, true); break;
        case 'i': dumpDeviceInfo(camera, Serial); break;
        case 'g': dumpVendorCodes(camera, Serial); break;
        case 'w': watchCameraProps(camera, Serial); break;
        case 'x': writeProperty(); break;
        case 'b': control.toggleBaseIso(); break;
        case 'h': control.toggleShutterMode(); break;
        case 'f': control.shutterHalf(true); control.shutterHalf(false); break;   // autofocus once
        case 't': control.shutterFull(true); break;                              // shutter (photo) / record (video)
        case 'R': control.recordToggle(); break;
        case 'r': rawOperation(); break;
        case 'k':
            pollEventEx = !pollEventEx;
            Serial.printf("Nikon GetEventEx polling %s\n", pollEventEx ? "on" : "off");
            break;
        case 'e':
            showEvents = !showEvents;
            Serial.printf("camera events %s\n", showEvents ? "shown" : "hidden");
            break;
        case 'd': camera.dumpDiagnostics(Serial); break;
        case 'l':
            debugLog = !debugLog;
            Serial.printf("library debug logging %s\n", debugLog ? "on" : "off");
            break;
    }
}

static void handle(const Command* cmds, size_t n) {
    for (size_t i = 0; i < n; i++) {
        if (cmds[i].type == CommandType::StepSetting) control.step(cmds[i].setting, cmds[i].delta);
        if (cmds[i].type == CommandType::BaseIsoToggle) control.toggleBaseIso();
        if (cmds[i].type == CommandType::ShutterModeToggle) control.toggleShutterMode();
        if (cmds[i].type == CommandType::ShutterHalf)  control.shutterHalf(cmds[i].on);
        if (cmds[i].type == CommandType::ShutterFull)  control.shutterFull(cmds[i].on);
        if (cmds[i].type == CommandType::RecordToggle) control.recordToggle();
        describe(cmds[i], lastLine, sizeof(lastLine));
        Serial.println(lastLine);
    }
    if (n) redraw();
}

void setup() {
    Serial.begin(115200);
    delay(500);
    Serial.println("\n=== esp32-ptp-grip input bring-up ===");

    ESP32Encoder::useInternalWeakPullResistors = puType::up;
    for (size_t i = 0; i < NUM_ENCODERS; i++) {
        const EncoderConfig& e = config::ENCODERS[i];
        encoders[i].attachFullQuad(e.pinA, e.pinB);
        encoders[i].clearCount();
        encoderFilters[i] = EncoderFilter(e.countsPerDetent, e.reverse);
        encoderFilters[i].update(0);   // origin
        Serial.printf("encoder(%u) %-6s A=%u B=%u\n", (unsigned)i, e.label, e.pinA, e.pinB);
    }
    for (size_t i = 0; i < NUM_BUTTONS; i++) {
        const ButtonConfig& b = config::BUTTONS[i];
        pinMode(b.pin, b.activeLow ? INPUT_PULLUP : INPUT_PULLDOWN);
        Serial.printf("button(%u)  %-6s pin=%u\n", (unsigned)i, b.label, b.pin);
    }

    if (config::OLED.enabled) {
        display.begin();
        redraw();
    }

    PtpIpSetLogCallback(onLibraryLog);
    camera.setOnPropChanged(onPropChanged);
    // Needed on the ZR to record movies remotely, and to show the card's
    // remaining time while connected. The library leaves it again in end()
    // ('q'); send 'q' before reflashing.
    camera.setApplicationMode(true);
    camLink.begin(config::CAMERA);
}

void loop() {
    uint32_t now = millis();
    Command cmds[BindingEngine::MAX_COMMANDS];

    for (size_t i = 0; i < NUM_ENCODERS; i++) {
        int16_t detents = encoderFilters[i].update((int32_t)encoders[i].getCount());
        if (detents) handle(cmds, engine.onEncoder((uint8_t)i, detents, cmds));
    }

    for (size_t i = 0; i < NUM_BUTTONS; i++) {
        const ButtonConfig& b = config::BUTTONS[i];
        bool pressed = digitalRead(b.pin) == (b.activeLow ? LOW : HIGH);
        ButtonEvent events[2];
        uint8_t n = buttonFilters[i].update(pressed, now, events);
        for (uint8_t k = 0; k < n; k++) {
            // While pairing, a short press accepts the code instead of acting as a control
            if (events[k] == ButtonEvent::Short && camLink.onButtonPress()) continue;
            handle(cmds, engine.onButton((uint8_t)i, events[k], cmds));
        }
    }

    if (Serial.available()) onSerial((char)Serial.read());

    camLink.update(now);
    if (camLink.takeChanged()) redraw();

    // Follow the camera's photo/video switch (read by camera.poll())
    if (camLink.connected()) {
        CameraMode mode = camera.movieMode() ? CameraMode::Video : CameraMode::Photo;
        if (mode != engine.cameraMode()) {
            engine.setCameraMode(mode);
            snprintf(lastLine, sizeof(lastLine), "%s mode", mode == CameraMode::Video ? "VIDEO" : "PHOTO");
            Serial.println(lastLine);
            redraw();
        }
    }

    if (pollEventEx && camLink.connected() && now - lastEventEx >= 100) {
        lastEventEx = now;
        pollNikonEvents();
    }

    // Recording started or stopped on the camera body (or by us): update the display
    static bool wasRecording = false;
    if (control.recording() != wasRecording) {
        wasRecording = control.recording();
        snprintf(lastLine, sizeof(lastLine), "%s", wasRecording ? "REC" : "STOP");
        redraw();
    }

    control.update(camLink.connected());
    if (control.takeMessage(lastLine, sizeof(lastLine))) {
        Serial.println(lastLine);
        redraw();
    }

    delay(2);
}
