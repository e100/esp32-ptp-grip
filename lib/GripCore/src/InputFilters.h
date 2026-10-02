#pragma once
#include "GripTypes.h"

namespace grip {

// ============================================================================
// ButtonFilter — debounces a raw switch reading and produces ButtonEvents.
// Feed it the raw pressed state and the time on every loop; it reports at most
// two events per call (e.g. Up + Short).
// ============================================================================
class ButtonFilter {
public:
    explicit ButtonFilter(uint16_t debounceMs = 20, uint16_t longPressMs = 600)
        : _debounceMs(debounceMs), _longMs(longPressMs) {}

    // Returns the number of events written to out (0..2).
    uint8_t update(bool rawPressed, uint32_t nowMs, ButtonEvent out[2]) {
        uint8_t n = 0;
        if (rawPressed != _raw) {          // input moved: restart the settle timer
            _raw = rawPressed;
            _rawSince = nowMs;
        }
        if (_raw != _stable && nowMs - _rawSince >= _debounceMs) {
            _stable = _raw;
            if (_stable) {
                _pressedAt = nowMs;
                _longSent = false;
                out[n++] = ButtonEvent::Down;
            } else {
                out[n++] = ButtonEvent::Up;
                if (!_longSent) out[n++] = ButtonEvent::Short;
            }
        }
        if (_stable && !_longSent && nowMs - _pressedAt >= _longMs) {
            _longSent = true;
            if (n < 2) out[n++] = ButtonEvent::Long;
        }
        return n;
    }

    bool pressed() const { return _stable; }

private:
    uint16_t _debounceMs;
    uint16_t _longMs;
    bool     _raw = false;
    bool     _stable = false;
    bool     _longSent = false;
    uint32_t _rawSince = 0;
    uint32_t _pressedAt = 0;
};

// ============================================================================
// EncoderFilter — converts a running quadrature count into whole detents.
// Tracks the absolute detent position, rounding to the nearest detent, so the
// switch-over point sits halfway between clicks: a wheel resting on a click
// can't jitter, and reversing direction is never lost. `reverse` flips direction.
// ============================================================================
class EncoderFilter {
public:
    explicit EncoderFilter(int8_t countsPerDetent = 4, bool reverse = false)
        : _perDetent(countsPerDetent < 1 ? 1 : countsPerDetent), _reverse(reverse) {}

    // count: the encoder's absolute count. Returns detents moved since last call.
    int16_t update(int32_t count) {
        if (!_started) { _origin = count; _started = true; return 0; }
        int32_t pos = _nearestDetent(count - _origin);
        int32_t moved = pos - _pos;
        _pos = pos;
        return (int16_t)(_reverse ? -moved : moved);
    }

private:
    int8_t  _perDetent;
    bool    _reverse;
    bool    _started = false;
    int32_t _origin = 0;
    int32_t _pos = 0;

    int32_t _nearestDetent(int32_t counts) const {
        int32_t n = counts + _perDetent / 2;   // floor division (correct for negatives)
        return n >= 0 ? n / _perDetent : -((-n + _perDetent - 1) / _perDetent);
    }
};

}  // namespace grip
