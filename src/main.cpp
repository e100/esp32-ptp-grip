// esp32-ptp-grip — input bring-up and camera pairing.
//
// Reads the encoders and buttons from include/grip_config.h, runs them through
// the binding engine, and shows the resulting commands on Serial and the OLED.
// Joins the camera's WiFi and pairs with it (see CameraLink.h). Encoder steps
// change the camera's settings (CameraControl.h); shutter and record don't
// reach the camera yet.
//
// Serial: 'v' toggle photo/video mode, 's' link status, 'p' pair,
//         'y' accept pairing code, 'n' cancel pairing, 'o' list the values
//         the camera offers, 'a' dump the listed camera properties, 'A' scan
//         every property code, 'i' DeviceInfo, 'd' camera diagnostics,
//         'l' toggle library debug logging (includes camera events).

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
                 engine.cameraMode() == CameraMode::Photo ? "PHOTO" : "VIDEO",
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
    if (level >= PTPIP_LOG_INFO || debugLog) Serial.println(msg);
}

// A setting changed on the camera itself (dial, menu) — show it.
static void onPropChanged(CameraProperty prop, uint32_t value, void*) {
    Setting s;
    if (!CameraControl::settingFor(prop, s)) return;
    int32_t v = prop == CameraProperty::EXPOSURE_COMPENSATION ? (int16_t)value : (int32_t)value;
    CameraControl::format(s, v, lastLine, sizeof(lastLine));
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
        CameraControl::format(s, o.current, cur, sizeof(cur));
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

static void onSerial(char c) {
    switch (c) {
        case 'v':
            engine.setCameraMode(engine.cameraMode() == CameraMode::Photo ? CameraMode::Video : CameraMode::Photo);
            snprintf(lastLine, sizeof(lastLine), "mode %s", engine.cameraMode() == CameraMode::Photo ? "photo" : "video");
            Serial.println(lastLine);
            redraw();
            break;
        case 's': camLink.printStatus(Serial); break;
        case 'p': camLink.forcePair(); break;
        case 'y': camLink.onButtonPress(); break;
        case 'n': camLink.cancel(); break;
        case 'o': printOptions(); break;
        case 'a': dumpCameraProps(camera, Serial, false); break;
        case 'A': dumpCameraProps(camera, Serial, true); break;
        case 'i': dumpDeviceInfo(camera, Serial); break;
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

    control.update(camLink.connected());
    if (control.takeMessage(lastLine, sizeof(lastLine))) {
        Serial.println(lastLine);
        redraw();
    }

    delay(2);
}
