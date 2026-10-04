#pragma once
// ============================================================================
// AutoLink — picks the camera connection automatically:
//   - A camera on the USB port wins: WiFi is turned off and the camera is
//     used over USB (UsbLink).
//   - With nothing on USB, WiFi is on and the grip joins a configured camera
//     (CameraLink).
// Unplugging (or switching off) the USB camera brings WiFi back.
//
// Without USB host support (GRIP_CAMERA_USB unset, or a chip without USB
// OTG) it is just the WiFi link. Same interface as CameraLink/UsbLink.
// ============================================================================

#include <PtpIpCamera.h>
#include "GripConfig.h"
#include "CameraLink.h"
#include "UsbLink.h"
#include "SwitchSession.h"

#if defined(GRIP_CAMERA_USB) && defined(PTP_USB_SUPPORTED)
#define GRIP_AUTO_USB 1
#endif

class AutoLink {
public:
#ifdef GRIP_AUTO_USB
    AutoLink(NikonCamera& camera, SwitchSession& session, PtpIpSession& ip, PtpUsbSession& usb)
        : _session(session), _ip(ip), _usb(usb), _wifi(camera, ip), _usbLink(camera, usb) {}
#else
    AutoLink(NikonCamera& camera, SwitchSession& session, PtpIpSession& ip)
        : _session(session), _ip(ip), _wifi(camera, ip) {}
#endif

    void begin(const grip::CameraConfig* cameras, size_t count);
    void update(uint32_t now);

    bool onButtonPress()      { return _onUsb() ? _usbLinkPress() : _wifi.onButtonPress(); }
    void cancel()             { if (!_onUsb()) _wifi.cancel(); }
    void forcePair()          { if (!_onUsb()) _wifi.forcePair(); }
    void disconnect();
    void connect();
    void printStatus(Print& out);

    bool        connected() const;
    bool        disabled() const;     // no camera can be reached at all
    bool        onUsb() const { return _onUsb(); }
    const char* headline() const;
    const char* detail() const;
    bool        takeChanged();

private:
    SwitchSession& _session;
    PtpIpSession&  _ip;
#ifdef GRIP_AUTO_USB
    PtpUsbSession& _usb;
#endif
    CameraLink     _wifi;
#ifdef GRIP_AUTO_USB
    UsbLink        _usbLink;
    bool           _usbMode = false;
    uint32_t       _goneSince = 0;     // USB camera absent since (0 = present)
    bool           _modeChanged = false;
    bool _onUsb() const { return _usbMode; }
    bool _usbLinkPress() { return _usbLink.onButtonPress(); }
#else
    bool _onUsb() const { return false; }
    bool _usbLinkPress() { return false; }
#endif
};
