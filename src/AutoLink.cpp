#include "AutoLink.h"

#ifdef GRIP_AUTO_USB
static const uint32_t USB_GONE_MS = 1500;   // USB camera absent this long: back to WiFi
#endif

void AutoLink::begin(const grip::CameraConfig* cameras, size_t count) {
#ifdef GRIP_AUTO_USB
    _usbLink.begin();
#endif
    _session.select(_ip);
    _wifi.begin(cameras, count);
}

void AutoLink::update(uint32_t now) {
#ifdef GRIP_AUTO_USB
    if (!_usbMode && _usb.cameraAttached()) {
        // Camera on USB: it takes over, WiFi goes off
        Serial.println("link: camera on USB, switching from WiFi to USB");
        _wifi.suspend();
        _session.select(_usb);
        _usbMode = true;
        _goneSince = 0;
        _modeChanged = true;
    } else if (_usbMode) {
        // Back to WiFi once the USB camera has been gone for a moment
        // (unplugged or switched off), unless the user stopped the link
        bool idle = _usbLink.state() == UsbLink::State::WaitingForCamera;
        if (idle && !_usb.cameraAttached()) {
            if (_goneSince == 0) _goneSince = now ? now : 1;
            else if (now - _goneSince >= USB_GONE_MS) {
                Serial.println("link: no camera on USB, switching to WiFi");
                _session.select(_ip);
                _usbMode = false;
                _modeChanged = true;
                _wifi.resume();
            }
        } else {
            _goneSince = 0;
        }
    }
    if (_usbMode) {
        _usbLink.update(now);
        return;
    }
#endif
    _wifi.update(now);
}

void AutoLink::disconnect() {
#ifdef GRIP_AUTO_USB
    if (_usbMode) { _usbLink.disconnect(); return; }
#endif
    _wifi.disconnect();
}

void AutoLink::connect() {
#ifdef GRIP_AUTO_USB
    if (_usbMode) { _usbLink.connect(); return; }
#endif
    _wifi.connect();
}

void AutoLink::printStatus(Print& out) {
#ifdef GRIP_AUTO_USB
    out.printf("link: transport %s\n", _usbMode ? "USB" : "WiFi");
    if (_usbMode) { _usbLink.printStatus(out); return; }
#endif
    _wifi.printStatus(out);
}

bool AutoLink::connected() const {
#ifdef GRIP_AUTO_USB
    if (_usbMode) return _usbLink.connected();
#endif
    return _wifi.connected();
}

bool AutoLink::disabled() const {
#ifdef GRIP_AUTO_USB
    if (_usbMode) return _usbLink.state() == UsbLink::State::Disabled;
    return false;                 // USB can still bring a camera even with no WiFi configured
#else
    return _wifi.state() == CameraLink::State::Disabled;
#endif
}

const char* AutoLink::headline() const {
#ifdef GRIP_AUTO_USB
    if (_usbMode) return _usbLink.headline();
    if (_wifi.state() == CameraLink::State::Disabled) return "Plug camera";
#endif
    return _wifi.headline();
}

const char* AutoLink::detail() const {
#ifdef GRIP_AUTO_USB
    if (_usbMode) return _usbLink.detail();
    if (_wifi.state() == CameraLink::State::Disabled) return "USB-C (no WiFi set)";
#endif
    return _wifi.detail();
}

bool AutoLink::takeChanged() {
    bool c = _wifi.takeChanged();
#ifdef GRIP_AUTO_USB
    c |= _usbLink.takeChanged();
    c |= _modeChanged;
    _modeChanged = false;
#endif
    return c;
}
