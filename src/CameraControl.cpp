#include "CameraControl.h"
#include <math.h>
#include <stdarg.h>

using grip::Setting;

static CameraProperty propertyFor(Setting s) {
    switch (s) {
        case Setting::ISO:          return CameraProperty::ISO;
        case Setting::Shutter:      return CameraProperty::SHUTTER_SPEED;
        case Setting::Aperture:     return CameraProperty::APERTURE;
        case Setting::ExposureComp: return CameraProperty::EXPOSURE_COMPENSATION;
        default:                    return CameraProperty::WHITE_BALANCE;
    }
}

bool CameraControl::settingFor(CameraProperty prop, Setting& out) {
    switch (prop) {
        case CameraProperty::ISO:                   out = Setting::ISO;          return true;
        case CameraProperty::SHUTTER_SPEED:         out = Setting::Shutter;      return true;
        case CameraProperty::APERTURE:              out = Setting::Aperture;     return true;
        case CameraProperty::EXPOSURE_COMPENSATION: out = Setting::ExposureComp; return true;
        case CameraProperty::WHITE_BALANCE:         out = Setting::WhiteBalance; return true;
        default:                                    return false;
    }
}

void CameraControl::step(Setting s, int16_t delta) {
    if ((size_t)s < (size_t)Setting::COUNT) _pending[(size_t)s] += delta;
}

void CameraControl::update(bool connected) {
    for (size_t i = 0; i < (size_t)Setting::COUNT; i++) {
        if (_pending[i] == 0) continue;
        int16_t delta = _pending[i];
        _pending[i] = 0;
        if (connected) _apply((Setting)i, delta);
        return;   // one camera change per loop keeps the controls responsive
    }
}

void CameraControl::_apply(Setting s, int16_t delta) {
    const char* name = grip::settingName(s);
    NikonPropOptions& o = _options;
    CameraResult r = _camera.getPropOptions(propertyFor(s), o);
    if (r != CAM_OK) {
        Serial.printf("cam: %s options: %s\n", name, cameraResultStr(r));
        _say("%s: %s", name, cameraResultStr(r));
        return;
    }
    if (!o.writable) {
        Serial.printf("cam: %s is read-only in this mode\n", name);
        _say("%s fixed", name);
        return;
    }

    int32_t target = o.current;
    if (o.form == NikonPropOptions::Form::List && o.count > 0) {
        // Exact match, otherwise the nearest value
        int idx = 0;
        for (int i = 0; i < o.count; i++) {
            if (labs((long)o.values[i] - o.current) < labs((long)o.values[idx] - o.current)) idx = i;
        }
        int next = idx + delta;
        if (next < 0) next = 0;
        if (next >= o.count) next = o.count - 1;
        target = o.values[next];
        Serial.printf("cam: %s %ld -> %ld (step %d of %u values)\n", name, (long)o.current,
                      (long)target, next, (unsigned)o.count);
    } else if (o.form == NikonPropOptions::Form::Range && o.step != 0) {
        int32_t t = o.current + (int32_t)delta * o.step;
        if (t < o.min) t = o.min;
        if (t > o.max) t = o.max;
        target = t;
        Serial.printf("cam: %s %ld -> %ld (range %ld..%ld step %ld)\n", name, (long)o.current,
                      (long)target, (long)o.min, (long)o.max, (long)o.step);
    } else {
        Serial.printf("cam: %s has no list of values\n", name);
        _say("%s fixed", name);
        return;
    }

    char text[24];
    if (target == o.current) {                 // already at the end of the list
        format(s, o.current, text, sizeof(text));
        _say("%s", text);
        return;
    }
    r = _camera.setCameraProperty(propertyFor(s), (uint32_t)target);
    if (r != CAM_OK) {
        Serial.printf("cam: set %s: %s\n", name, cameraResultStr(r));
        _say("%s: %s", name, cameraResultStr(r));
        return;
    }
    format(s, target, text, sizeof(text));
    _say("%s", text);
}

void CameraControl::_say(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vsnprintf(_message, sizeof(_message), fmt, args);
    va_end(args);
    _hasMessage = true;
}

bool CameraControl::takeMessage(char* buf, size_t len) {
    if (!_hasMessage) return false;
    snprintf(buf, len, "%s", _message);
    _hasMessage = false;
    return true;
}

static const char* whiteBalanceName(uint32_t v) {
    switch (v) {
        case 0x0002: return "Auto";
        case 0x0004: return "Daylight";
        case 0x0005: return "Fluor";
        case 0x0006: return "Tungsten";
        case 0x0007: return "Flash";
        case 0x8010: return "Cloudy";
        case 0x8011: return "Shade";
        case 0x8012: return "Kelvin";
        case 0x8013: return "Preset";
        case 0x8016: return "Natural";
        default:     return nullptr;
    }
}

// The camera reports shutter speeds as whole 1/10000 s units, so 1/160 arrives
// as 62 and 1/320 as 31. Show the nearest standard speed.
static unsigned nominalDenominator(uint32_t t) {
    static const uint16_t SPEEDS[] = {
        4, 5, 6, 8, 10, 13, 15, 20, 25, 30, 40, 50, 60, 80, 100, 125, 160, 200, 250, 320,
        400, 500, 640, 800, 1000, 1250, 1600, 2000, 2500, 3200, 4000, 5000, 6400, 8000,
        10000, 12800, 16000, 20000, 25000, 32000,
    };
    double exact = 10000.0 / t;
    unsigned best = SPEEDS[0];
    for (uint16_t s : SPEEDS) {
        if (fabs(log(s / exact)) < fabs(log(best / exact))) best = s;
    }
    return best;
}

void CameraControl::format(Setting s, int32_t value, char* buf, size_t len) {
    switch (s) {
        case Setting::ISO:
            snprintf(buf, len, "ISO %ld", (long)value);
            break;
        case Setting::Shutter: {
            uint32_t t = (uint32_t)value;            // seconds * 10000
            if (t >= 0xFFFFFFF0u)   snprintf(buf, len, "Bulb");
            else if (t == 0)        snprintf(buf, len, "Shutter --");
            else if (t >= 10000)    snprintf(buf, len, t % 10000 ? "%.1fs" : "%.0fs", t / 10000.0);
            else if (t >= 2500)     snprintf(buf, len, "%.1fs", t / 10000.0);
            else                    snprintf(buf, len, "1/%u", nominalDenominator(t));
            break;
        }
        case Setting::Aperture:
            if (value == 0) snprintf(buf, len, "f/--");
            else            snprintf(buf, len, "f/%.1f", value / 100.0);
            break;
        case Setting::ExposureComp:
            snprintf(buf, len, "EC %+.1f", value / 1000.0);
            break;
        case Setting::WhiteBalance: {
            const char* n = whiteBalanceName((uint32_t)value);
            if (n) snprintf(buf, len, "WB %s", n);
            else   snprintf(buf, len, "WB 0x%04lX", (unsigned long)value);
            break;
        }
        default:
            snprintf(buf, len, "?");
            break;
    }
}
