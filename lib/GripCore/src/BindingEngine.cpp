#include "BindingEngine.h"

namespace grip {

const char* settingName(Setting s) {
    switch (s) {
        case Setting::ISO:          return "ISO";
        case Setting::Shutter:      return "Shutter";
        case Setting::Aperture:     return "Aperture";
        case Setting::ExposureComp: return "EC";
        case Setting::WhiteBalance: return "WB";
        default:                    return "?";
    }
}

static uint8_t modeBit(CameraMode m) { return m == CameraMode::Photo ? MODE_PHOTO : MODE_VIDEO; }

BindingEngine::BindingEngine(const Binding* bindings, size_t count,
                             SettingList photoSettings, SettingList videoSettings,
                             uint8_t layerCount)
    : _bindings(bindings)
    , _count(count)
    , _lists{ photoSettings, videoSettings }
    , _selected{ 0, 0 }
    , _layerCount(layerCount == 0 ? 1 : (layerCount > MAX_LAYERS ? MAX_LAYERS : layerCount))
    , _layer(0)
    , _held(-1)
    , _mode(CameraMode::Photo)
    , _locked(false)
{
    for (int16_t& h : _holding) h = -1;
}

void BindingEngine::setCameraMode(CameraMode mode) {
    _mode = mode;
}

Setting BindingEngine::selected() const {
    const SettingList& list = _lists[(int)_mode];
    if (list.count == 0) return Setting::ISO;
    return list.items[_selected[(int)_mode] % list.count];
}

bool BindingEngine::_allowedWhileLocked(const Action& a) const {
    return a.type == ActionType::RecordToggle || a.type == ActionType::Lock ||
           a.type == ActionType::LockToggle;
}

const Binding* BindingEngine::_match(InputRef input, Trigger trigger) const {
    uint8_t mode  = modeBit(_mode);
    uint8_t layer = (uint8_t)(1u << activeLayer());
    for (size_t i = 0; i < _count; i++) {
        const Binding& b = _bindings[i];
        if (b.input.kind == input.kind && b.input.index == input.index &&
            b.trigger == trigger && (b.modes & mode) && (b.layers & layer)) {
            return (_locked && !_allowedWhileLocked(b.action)) ? nullptr : &b;
        }
    }
    return nullptr;
}

size_t BindingEngine::onEncoder(uint8_t index, int16_t detents, Command* out) {
    if (detents == 0) return 0;
    const Binding* b = _match(encoder(index), Trigger::Turn);
    if (!b) return 0;
    return _fire(b->action, true, detents, out);
}

size_t BindingEngine::onButton(uint8_t index, ButtonEvent event, Command* out) {
    if (index >= MAX_BUTTONS) return 0;

    switch (event) {
        case ButtonEvent::Down: {
            const Binding* b = _match(button(index), Trigger::Hold);
            if (!b) return 0;
            // Remember which binding fired so the release goes to the same one,
            // even if the layer or camera mode changes while the button is held.
            _holding[index] = (int16_t)(b - _bindings);
            return _fire(b->action, true, 0, out);
        }
        case ButtonEvent::Up: {
            if (_holding[index] < 0) return 0;
            const Binding& b = _bindings[_holding[index]];
            _holding[index] = -1;
            return _fire(b.action, false, 0, out);
        }
        case ButtonEvent::Short: {
            const Binding* b = _match(button(index), Trigger::Short);
            return b ? _fire(b->action, true, 0, out) : 0;
        }
        case ButtonEvent::Long: {
            const Binding* b = _match(button(index), Trigger::Long);
            return b ? _fire(b->action, true, 0, out) : 0;
        }
    }
    return 0;
}

size_t BindingEngine::_fire(const Action& a, bool on, int16_t detents, Command* out) {
    const SettingList& list = _lists[(int)_mode];
    uint8_t& sel = _selected[(int)_mode];

    switch (a.type) {
        case ActionType::Adjust:
            out[0] = Command{ CommandType::StepSetting, a.setting, detents, true };
            return 1;
        case ActionType::AdjustSelected:
            out[0] = Command{ CommandType::StepSetting, selected(), detents, true };
            return 1;
        case ActionType::SelectNext:
        case ActionType::SelectPrev:
            if (list.count == 0) return 0;
            sel = (uint8_t)((sel + (a.type == ActionType::SelectNext ? 1 : list.count - 1)) % list.count);
            out[0] = Command{ CommandType::SelectionChanged, selected(), 0, true };
            return 1;
        case ActionType::ShutterHalf:
            out[0] = Command{ CommandType::ShutterHalf, Setting::ISO, 0, on };
            return 1;
        case ActionType::ShutterFull:
            out[0] = Command{ CommandType::ShutterFull, Setting::ISO, 0, on };
            return 1;
        case ActionType::RecordToggle:
            out[0] = Command{ CommandType::RecordToggle, Setting::ISO, 0, true };
            return 1;
        case ActionType::LayerNext:
            _layer = (uint8_t)((_layer + 1) % _layerCount);
            out[0] = Command{ CommandType::LayerChanged, Setting::ISO, (int16_t)activeLayer(), true };
            return 1;
        case ActionType::LayerHold:
            if (on) _held = (int8_t)(a.arg < _layerCount ? a.arg : _layerCount - 1);
            else    _held = -1;
            out[0] = Command{ CommandType::LayerChanged, Setting::ISO, (int16_t)activeLayer(), true };
            return 1;
        case ActionType::Lock:
        case ActionType::LockToggle: {
            bool want = a.type == ActionType::Lock ? on : !_locked;
            if (want == _locked) return 0;
            _locked = want;
            out[0] = Command{ CommandType::LockChanged, Setting::ISO, 0, _locked };
            return 1;
        }
        case ActionType::None:
            return 0;
    }
    return 0;
}

}  // namespace grip
