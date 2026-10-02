#pragma once
#include "GripTypes.h"

namespace grip {

// A list of settings that SelectNext/SelectPrev cycle through.
struct SettingList {
    const Setting* items;
    uint8_t        count;
};

// ============================================================================
// BindingEngine
// Turns input events into Commands using the binding table, the camera's
// mode (photo/video) and the active layer. Holds the UI state that belongs
// to the controls: selected setting and active layer.
// ============================================================================
class BindingEngine {
public:
    static constexpr uint8_t MAX_BUTTONS  = 16;
    static constexpr size_t  MAX_COMMANDS = 4;   // most commands one event can produce

    BindingEngine(const Binding* bindings, size_t count,
                  SettingList photoSettings, SettingList videoSettings,
                  uint8_t layerCount);

    void       setCameraMode(CameraMode mode);
    CameraMode cameraMode() const { return _mode; }
    uint8_t    activeLayer() const { return _held >= 0 ? (uint8_t)_held : _layer; }
    Setting    selected() const;
    // While locked, only record, lock/unlock and releases of held buttons act.
    bool       locked() const { return _locked; }

    // Each returns the number of commands written to out (at most MAX_COMMANDS).
    size_t onEncoder(uint8_t index, int16_t detents, Command* out);
    size_t onButton(uint8_t index, ButtonEvent event, Command* out);

private:
    const Binding* _bindings;
    size_t         _count;
    SettingList    _lists[2];      // indexed by CameraMode
    uint8_t        _selected[2];   // selected index per mode
    uint8_t        _layerCount;
    uint8_t        _layer;         // layer chosen with LayerNext
    int8_t         _held;          // layer forced by LayerHold, or -1
    CameraMode     _mode;
    bool           _locked;
    int16_t        _holding[MAX_BUTTONS];   // binding index each held button fired, or -1

    const Binding* _match(InputRef input, Trigger trigger) const;
    bool _allowedWhileLocked(const Action& a) const;
    size_t _fire(const Action& a, bool on, int16_t detents, Command* out);
};

}  // namespace grip
