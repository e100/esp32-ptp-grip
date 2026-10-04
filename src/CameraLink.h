#pragma once
// ============================================================================
// CameraLink — joins the camera's WiFi, finds it, pairs if needed and keeps
// the PTP/IP session up. Non-blocking apart from the network calls themselves
// (begin() and the mDNS query can each take a few seconds).
//
// Pairing (Nikon Z, see PtpIpCamera's README):
//   1. The camera advertises `apps=PAIR` over mDNS while its active profile
//      has no paired host. The grip then asks for a pairing code.
//   2. Grip and camera show the same code. The user presses any grip button
//      if they match (or sends 'y' over Serial).
//   3. The camera asks the user to confirm this host. Once they do, the grip
//      reconnects; the camera remembers the grip's GUID from then on.
// ============================================================================

#include <PtpIpCamera.h>
#include "GripConfig.h"

class CameraLink {
public:
    enum class State : uint8_t {
        Disabled,       // no SSID configured
        JoiningWifi,
        Finding,        // mDNS query for the camera's pairing state
        Connecting,     // PTP/IP begin()
        ShowingCode,    // waiting for the user to check the code and press a button
        ConfirmOnCamera,// waiting for the user to confirm on the camera
        Connected,
        OtherHost,      // camera is paired with another device
        Stopped,        // pairing cancelled, or disconnect(); a button press or connect() retries
        Waiting,        // retry after an error
    };

    CameraLink(NikonCamera& camera, PtpIpSession& session) : _camera(camera), _session(session) {}

    void begin(const grip::CameraConfig& config);
    void update(uint32_t now);

    // A short button press. Returns true if the link used it (code accepted,
    // or retry after Stopped), in which case it shouldn't also be a control.
    bool onButtonPress();

    void cancel();       // abandon pairing
    void forcePair();    // start pairing even if the camera says it is paired
    void disconnect();   // close the session cleanly and stay disconnected (e.g. before reflashing)
    void connect();      // reconnect after disconnect() or a cancelled pairing
    void printStatus(Print& out);

    State       state() const { return _state; }
    bool        connected() const { return _state == State::Connected; }
    const char* code() const { return _code; }

    // Display text for the current state: a short headline and a detail line.
    const char* headline() const;
    const char* detail() const;

    // True once after the state (or anything shown) changes.
    bool takeChanged() { bool c = _changed; _changed = false; return c; }

private:
    enum class Advert : uint8_t { Unknown, Unpaired, Paired };

    void   _set(State s, uint32_t now);
    void   _retry(uint32_t now, uint32_t afterMs, const char* why);
    Advert _discover();
    void   _connect(uint32_t now);
    void   _startPairing(uint32_t now);
    void   _loadIdentity();

    NikonCamera&         _camera;
    PtpIpSession&        _session;
    grip::CameraConfig   _cfg {};
    State                _state = State::Disabled;
    uint32_t             _since = 0;          // when _state was entered
    uint32_t             _retryAt = 0;
    uint32_t             _lastPoll = 0;
    uint32_t             _reconnectUntil = 0; // after pairing: keep retrying begin() until then
    uint8_t              _failures = 0;       // consecutive failed attempts, for retry back-off
    Advert               _advert = Advert::Unknown;
    bool                 _mdnsStarted = false;
    bool                 _forcePair = false;
    bool                 _changed = true;
    char                 _code[9] = "";
    char                 _detail[24] = "";
};
