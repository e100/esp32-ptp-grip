#pragma once
// ============================================================================
// Grip configuration — edit this file to match your build.
//
// 0. CAMERA: the camera's WiFi network (see the README for camera setup).
// 1. ENCODERS[] and BUTTONS[] describe the hardware. An encoder's push switch
//    is just another entry in BUTTONS[].
// 2. BINDINGS[] maps each input to an action. The first matching row wins,
//    so put rows limited to one mode or layer above general rows.
// 3. PHOTO_SETTINGS[] / VIDEO_SETTINGS[] are what SelectNext/SelectPrev cycle
//    through in each camera mode (used by adjustSelected()).
//
// ESP32-S3 pin notes: avoid GPIO 0, 3, 45, 46 (strapping), 19/20 (USB), and
// 26-37 (flash/PSRAM on most modules).
// ============================================================================
#include "GripConfig.h"
#if __has_include("camera_wifi.h")
#include "camera_wifi.h"
#else
#include "camera_wifi.example.h"
#endif

namespace grip {
namespace config {

// Camera WiFi. On the ZR: Network > Connect to computer > Camera Control, with a
// "Direct connection to computer" profile. The camera shows the SSID and key;
// put them in include/camera_wifi.h (copy camera_wifi.example.h), which git ignores.
static const CameraConfig CAMERA = {
    CAMERA_WIFI_SSID,
    CAMERA_WIFI_KEY,
    "192.168.1.1",   // camera IP; 192.168.1.1 in direct-connection mode
};

static const DisplayConfig OLED = { true, 8, 9 };   // SSD1306 128x32, I2C SDA, SCL

static const EncoderConfig ENCODERS[] = {
    //  A  B  counts/detent  reverse  label
    {   4, 5, 2,             true,    "wheel" },   // encoder(0): thumb wheel
    {   6, 7, 2,             true,    "iso"   },   // encoder(1): dedicated ISO dial
};

static const ButtonConfig BUTTONS[] = {
    //  pin  activeLow  label
    {   15,  true,      "half"  },   // button(0): shutter half-press (two-stage shutter, stage 1)
    {   16,  true,      "full"  },   // button(1): shutter full-press (stage 2)
    {   17,  true,      "rec"   },   // button(2): record start/stop
    {   18,  true,      "push"  },   // button(3): thumb wheel's push switch
    {   10,  true,      "fn"    },   // button(4): function / shift (not wired yet: GPIO 10 is free)
    {   21,  true,      "isopush"},  // button(5): ISO dial's push switch
};

// Number of user layers. Layer 0 is the default; the Fn button holds layer 1.
static const uint8_t LAYER_COUNT = 2;

static const Binding BINDINGS[] = {
    // input       trigger         modes       layers     action

    // Encoder 1 is always ISO
    { encoder(1),  Trigger::Turn,  MODE_ANY,   LAYER_ANY, adjust(Setting::ISO) },

    // Thumb wheel: exposure compensation while Fn is held, otherwise the selected setting
    { encoder(0),  Trigger::Turn,  MODE_ANY,   layer(1),  adjust(Setting::ExposureComp) },
    { encoder(0),  Trigger::Turn,  MODE_ANY,   LAYER_ANY, adjustSelected() },

    // Two-stage shutter: half = AF / metering, full = shoot
    { button(0),   Trigger::Hold,  MODE_ANY,   LAYER_ANY, shutterHalf() },
    { button(1),   Trigger::Hold,  MODE_ANY,   LAYER_ANY, shutterFull() },

    // Dedicated REC button (works even when locked)
    { button(2),   Trigger::Short, MODE_ANY,   LAYER_ANY, recordToggle() },

    // ISO dial push in video: switch base ISO Low/High (ZR dual base ISO)
    { button(5),   Trigger::Short, MODE_VIDEO, LAYER_ANY, baseIsoToggle() },

    // Wheel push: next setting; long press: lock / unlock the controls
    { button(3),   Trigger::Short, MODE_ANY,   LAYER_ANY, selectNext() },
    { button(3),   Trigger::Long,  MODE_ANY,   LAYER_ANY, lockToggle() },

    // Fn: hold for layer 1
    { button(4),   Trigger::Hold,  MODE_ANY,   LAYER_ANY, layerHold(1) },

    // A lock slide switch would be: { button(n), Trigger::Hold, MODE_ANY, LAYER_ANY, lock() },
};

// Dual base ISO (ZR video). Switching base also sets ISO to that base's
// native value when BASE_ISO_SET_NATIVE is true. The display marks the
// native ISO with '*'.
static const bool     BASE_ISO_SET_NATIVE = true;
static const uint32_t BASE_ISO_LOW_NATIVE  = 800;
static const uint32_t BASE_ISO_HIGH_NATIVE = 6400;

static const Setting PHOTO_SETTINGS[] = {
    Setting::Shutter, Setting::Aperture, Setting::ISO, Setting::ExposureComp, Setting::WhiteBalance,
};

static const Setting VIDEO_SETTINGS[] = {
    Setting::ISO, Setting::Shutter, Setting::WhiteBalance, Setting::Aperture, Setting::ExposureComp,
};

}  // namespace config
}  // namespace grip
