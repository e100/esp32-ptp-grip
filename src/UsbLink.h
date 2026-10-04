#pragma once
// ============================================================================
// UsbLink — the camera over USB (GRIP_CAMERA_USB builds; AutoLink switches
// between it and CameraLink). No WiFi or pairing: waits for a camera on the
// native USB port, connects, keeps the session up, and notices unplugging.
// ============================================================================

#include <PtpIpCamera.h>
#include "GripConfig.h"

#ifdef PTP_USB_SUPPORTED

class UsbLink {
public:
    enum class State : uint8_t {
        Disabled,       // USB host could not start
        WaitingForCamera,
        Connecting,
        Connected,
        Stopped,        // disconnect(); a button press or connect() resumes
        Waiting,        // retry after an error
    };

    UsbLink(NikonCamera& camera, PtpUsbSession& session) : _camera(camera), _session(session) {}

    void begin();         // starts the USB host
    void update(uint32_t now);

    bool onButtonPress();     // resumes after disconnect(); otherwise not used
    void cancel() {}          // no pairing over USB
    void forcePair() {}
    void disconnect();
    void connect();
    void printStatus(Print& out);

    State state() const { return _state; }
    bool  connected() const { return _state == State::Connected; }

    const char* headline() const;
    const char* detail() const;
    bool takeChanged() { bool c = _changed; _changed = false; return c; }

private:
    NikonCamera&   _camera;
    PtpUsbSession& _session;
    State          _state = State::Disabled;
    uint32_t       _since = 0;
    uint32_t       _retryAt = 0;
    uint32_t       _lastPoll = 0;
    uint8_t        _failures = 0;
    bool           _changed = true;
    char           _detail[24] = "";

    void _set(State s, uint32_t now);
    void _retry(uint32_t now, const char* why);
};

#endif
