#pragma once
#include <stdint.h>
#include <stddef.h>

// ============================================================================
// GripTypes — vocabulary shared by the configuration and the binding engine.
// Platform-independent: no Arduino or camera-library dependencies.
// ============================================================================

namespace grip {

// Camera settings a control can adjust.
enum class Setting : uint8_t {
    ISO,
    Shutter,
    Aperture,
    ExposureComp,
    WhiteBalance,
    COUNT
};

const char* settingName(Setting s);

// The camera's current shooting mode. Bindings can be limited to one of them.
enum class CameraMode : uint8_t { Photo, Video };

static constexpr uint8_t MODE_PHOTO = 0x01;
static constexpr uint8_t MODE_VIDEO = 0x02;
static constexpr uint8_t MODE_ANY   = MODE_PHOTO | MODE_VIDEO;

// Layers are user-selected control sets (like a function/shift key).
// A binding lists the layers it applies to as a bitmask.
static constexpr uint8_t MAX_LAYERS = 8;
static constexpr uint8_t LAYER_ANY  = 0xFF;
constexpr uint8_t layer(uint8_t n) { return (uint8_t)(1u << n); }

// ----------------------------------------------------------------------------
// Inputs. An encoder is only its rotation; a push switch on an encoder is
// configured as an ordinary button.
// ----------------------------------------------------------------------------

enum class InputKind : uint8_t { Encoder, Button };

struct InputRef {
    InputKind kind;
    uint8_t   index;   // position in the ENCODERS[] or BUTTONS[] config table
};

constexpr InputRef encoder(uint8_t i) { return InputRef{ InputKind::Encoder, i }; }
constexpr InputRef button(uint8_t i)  { return InputRef{ InputKind::Button, i }; }

enum class ButtonEvent : uint8_t {
    Down,    // pressed (after debounce)
    Up,      // released
    Short,   // released before the long-press time
    Long     // held for the long-press time (fires once, while still held)
};

// What makes a binding fire.
enum class Trigger : uint8_t {
    Turn,    // encoder rotation
    Hold,    // button: action is "on" while pressed, "off" on release
    Short,   // button: short press
    Long     // button: long press
};

// ----------------------------------------------------------------------------
// Actions
// ----------------------------------------------------------------------------

enum class ActionType : uint8_t {
    None,
    Adjust,          // Turn: step a fixed setting
    AdjustSelected,  // Turn: step the currently selected setting
    SelectNext,      // Short/Long: select the next setting in the mode's list
    SelectPrev,
    ShutterHalf,     // Hold: half-press (AF / metering) while held
    ShutterFull,     // Hold: full press while held
    RecordToggle,    // Short/Long: start/stop video recording
    LayerNext,       // Short/Long: cycle to the next layer
    LayerHold,       // Hold: use layer `arg` while held
    Lock,            // Hold: controls locked while held (e.g. a slide switch)
    LockToggle       // Short/Long: lock / unlock
};

struct Action {
    ActionType type;
    Setting    setting;   // for Adjust
    uint8_t    arg;       // for LayerHold: layer number
};

constexpr Action adjust(Setting s)    { return Action{ ActionType::Adjust, s, 0 }; }
constexpr Action adjustSelected()     { return Action{ ActionType::AdjustSelected, Setting::ISO, 0 }; }
constexpr Action selectNext()         { return Action{ ActionType::SelectNext, Setting::ISO, 0 }; }
constexpr Action selectPrev()         { return Action{ ActionType::SelectPrev, Setting::ISO, 0 }; }
constexpr Action shutterHalf()        { return Action{ ActionType::ShutterHalf, Setting::ISO, 0 }; }
constexpr Action shutterFull()        { return Action{ ActionType::ShutterFull, Setting::ISO, 0 }; }
constexpr Action recordToggle()       { return Action{ ActionType::RecordToggle, Setting::ISO, 0 }; }
constexpr Action layerNext()          { return Action{ ActionType::LayerNext, Setting::ISO, 0 }; }
constexpr Action layerHold(uint8_t n) { return Action{ ActionType::LayerHold, Setting::ISO, n }; }
constexpr Action lock()               { return Action{ ActionType::Lock, Setting::ISO, 0 }; }
constexpr Action lockToggle()         { return Action{ ActionType::LockToggle, Setting::ISO, 0 }; }

// One row of the binding table. The first row that matches an input event wins,
// so list specific rows (one mode / one layer) before general ones.
struct Binding {
    InputRef input;
    Trigger  trigger;
    uint8_t  modes;    // MODE_PHOTO / MODE_VIDEO / MODE_ANY
    uint8_t  layers;   // layer(n) bits, or LAYER_ANY
    Action   action;
};

// ----------------------------------------------------------------------------
// Commands — what the engine asks the rest of the grip to do.
// ----------------------------------------------------------------------------

enum class CommandType : uint8_t {
    StepSetting,      // setting, delta (detents; sign = direction)
    ShutterHalf,      // on
    ShutterFull,      // on
    RecordToggle,
    SelectionChanged, // setting = newly selected (for the display)
    LayerChanged,     // delta = new layer number (for the display)
    LockChanged       // on = locked
};

struct Command {
    CommandType type;
    Setting     setting;
    int16_t     delta;
    bool        on;
};

}  // namespace grip
