#pragma once
// ============================================================================
// CameraControl — applies encoder steps to the camera.
//
// Each step reads the property's allowed values from the camera
// (GetDevicePropDesc) and moves the current value along that list, so the
// grip follows whatever the camera offers in its current mode and lens.
// Steps that arrive while a camera call is in progress add up and are sent
// as one change.
// ============================================================================

#include <PtpIpCamera.h>
#include "GripTypes.h"

class CameraControl {
public:
    explicit CameraControl(NikonCamera& camera) : _camera(camera) {}

    void step(grip::Setting s, int16_t delta);

    // Sends at most one pending change. Call from loop().
    void update(bool connected);

    // Text for the display after a change was applied (or refused), once.
    bool takeMessage(char* buf, size_t len);

    // "ISO 800", "1/250", "f/5.6", "EC +0.7", "WB Auto". value in raw PTP units.
    static void format(grip::Setting s, int32_t value, char* buf, size_t len);
    static bool settingFor(CameraProperty prop, grip::Setting& out);

private:
    NikonCamera&     _camera;
    int16_t          _pending[(size_t)grip::Setting::COUNT] = {};
    char             _message[24] = "";
    bool             _hasMessage = false;
    NikonPropOptions _options;   // large: kept here rather than on the stack

    void _apply(grip::Setting s, int16_t delta);
    void _say(const char* fmt, ...);
};
