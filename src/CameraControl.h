#pragma once
// ============================================================================
// CameraControl — applies encoder steps to the camera.
//
// Each step reads the property's allowed values from the camera
// (GetDevicePropDesc) and moves the current value along that list, so the
// grip follows whatever the camera offers in its current mode and lens.
// In movie mode the library addresses the movie settings instead.
// Steps that arrive while a camera call is in progress add up and are sent
// as one change.
// ============================================================================

#include <PtpIpCamera.h>
#include "GripTypes.h"

class CameraControl {
public:
    explicit CameraControl(NikonCamera& camera) : _camera(camera) {}

    void step(grip::Setting s, int16_t delta);

    // Switch the camera's dual base ISO (Low <-> High), and optionally set ISO
    // to the new base's native value (see grip_config.h). Applied by update().
    void toggleBaseIso() { _baseIsoToggle = true; }

    // Switch the video shutter between speed and angle. Applied by update().
    void toggleShutterMode() { _shutterModeToggle = true; }

    // Sends at most one pending change. Call from loop().
    void update(bool connected);

    // Text for the display after a change was applied (or refused), once.
    bool takeMessage(char* buf, size_t len);

    // "ISO 800", "1/250", "180 deg", "f/5.6", "EC +0.7", "WB Auto". value in raw
    // PTP units; the shutter's encoding depends on the camera's mode (photo
    // speed, movie speed fraction, or movie shutter angle).
    static void format(grip::Setting s, int32_t value, const NikonCamera& camera, char* buf, size_t len);
    static bool settingFor(CameraProperty prop, grip::Setting& out);

private:
    NikonCamera&     _camera;
    int16_t          _pending[(size_t)grip::Setting::COUNT] = {};
    char             _message[24] = "";
    bool             _hasMessage = false;
    NikonPropOptions _options;   // large: kept here rather than on the stack
    bool             _baseIsoToggle = false;
    bool             _shutterModeToggle = false;

    void _apply(grip::Setting s, int16_t delta);
    void _applyShutterModeToggle();
    void _applyBaseIsoToggle();
    // Any setting as format(), but ISO in movie mode as "ISO 6400 H*": the base
    // ISO letter, and '*' at the base's native ISO.
    void _formatValue(grip::Setting s, int32_t value, char* buf, size_t len);
    void _say(const char* fmt, ...);
};
