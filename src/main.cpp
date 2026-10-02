// esp32-ptp-grip — input bring-up.
//
// Reads the encoders and buttons from include/grip_config.h, runs them through
// the binding engine, and shows the resulting commands on Serial and the OLED.
// No camera yet. Send 'v' over Serial to toggle photo/video mode.

#include <Arduino.h>
#include <ESP32Encoder.h>
#include <U8g2lib.h>
#include <PtpIpCamera.h>
#include "BindingEngine.h"
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

// Camera objects are not used yet; constructing them checks the library builds for the S3.
static PtpIpTransport transport;
static PtpIpSession   session(transport);
static NikonCamera    camera(session);

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

static void redraw() {
    if (!config::OLED.enabled) return;
    char status[32];
    snprintf(status, sizeof(status), "%s L%u  %s%s",
             engine.cameraMode() == CameraMode::Photo ? "PHOTO" : "VIDEO",
             engine.activeLayer(), settingName(engine.selected()),
             engine.locked() ? "  LOCK" : "");
    display.clearBuffer();
    display.setFont(u8g2_font_9x15B_tr);
    display.drawStr(0, 14, lastLine);
    display.setFont(u8g2_font_6x10_tr);
    display.drawStr(0, 30, status);
    display.sendBuffer();
}

static void handle(const Command* cmds, size_t n) {
    for (size_t i = 0; i < n; i++) {
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
        for (uint8_t k = 0; k < n; k++) handle(cmds, engine.onButton((uint8_t)i, events[k], cmds));
    }

    if (Serial.available() && Serial.read() == 'v') {
        engine.setCameraMode(engine.cameraMode() == CameraMode::Photo ? CameraMode::Video : CameraMode::Photo);
        snprintf(lastLine, sizeof(lastLine), "mode %s", engine.cameraMode() == CameraMode::Photo ? "photo" : "video");
        Serial.println(lastLine);
        redraw();
    }

    delay(2);
}
