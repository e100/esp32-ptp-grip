#include "UsbLink.h"

#ifdef PTP_USB_SUPPORTED

static const uint32_t POLL_MS = 500;

void UsbLink::begin() {
    if (!_session.begin()) {
        Serial.println("link: USB host failed to start");
        snprintf(_detail, sizeof(_detail), "USB host error");
        _set(State::Disabled, millis());
        return;
    }
    Serial.println("link: USB host ready, plug in the camera (USB set to MTP/PTP)");
    _set(State::WaitingForCamera, millis());
}

void UsbLink::_set(State s, uint32_t now) {
    _state = s;
    _since = now;
    _changed = true;
}

// Back off on repeated failures: 2, 4, 8 ... up to 30 s
void UsbLink::_retry(uint32_t now, const char* why) {
    uint32_t afterMs = 2000;
    if (_failures < 8) _failures++;
    for (uint8_t i = 1; i < _failures && afterMs < 30000; i++) afterMs *= 2;
    if (afterMs > 30000) afterMs = 30000;
    Serial.printf("link: %s, retrying in %us\n", why, (unsigned)(afterMs / 1000));
    snprintf(_detail, sizeof(_detail), "%s", why);
    _retryAt = now + afterMs;
    _set(State::Waiting, now);
}

void UsbLink::update(uint32_t now) {
    switch (_state) {
        case State::WaitingForCamera:
            if (_session.cameraAttached()) _set(State::Connecting, now);
            break;

        case State::Connecting: {
            Serial.println("link: connecting over USB");
            CameraResult r = _camera.begin("usb");
            if (r == CAM_OK) {
                const PtpDeviceInfo& info = _camera.deviceInfo();
                Serial.printf("link: connected to %s %s over USB\n", info.manufacturer, info.model);
                snprintf(_detail, sizeof(_detail), "%.19s USB", info.model);
                _failures = 0;
                _lastPoll = now;
                _set(State::Connected, now);
            } else {
                Serial.printf("link: begin failed: %s\n", cameraResultStr(r));
                if (!_session.cameraAttached()) _set(State::WaitingForCamera, now);
                else _retry(now, cameraResultStr(r));
            }
            break;
        }

        case State::Connected:
            if (now - _lastPoll >= POLL_MS) {
                _lastPoll = now;
                _camera.poll();
                if (!_camera.isReady() || !_session.isConnected()) {
                    Serial.println("link: camera lost");
                    _camera.end();
                    _set(State::WaitingForCamera, now);
                }
            }
            break;

        case State::Waiting:
            if ((int32_t)(now - _retryAt) >= 0) {
                _set(_session.cameraAttached() ? State::Connecting : State::WaitingForCamera, now);
            }
            break;

        case State::Disabled:
        case State::Stopped:
            break;
    }
}

bool UsbLink::onButtonPress() {
    if (_state != State::Stopped) return false;
    connect();
    return true;
}

void UsbLink::disconnect() {
    if (_state == State::Disabled) return;
    if (_state == State::Connected) _camera.end();
    Serial.println("link: disconnected. Send 'c' or press a button to reconnect");
    snprintf(_detail, sizeof(_detail), "disconnected");
    _set(State::Stopped, millis());
}

void UsbLink::connect() {
    if (_state != State::Stopped) return;
    _failures = 0;
    _set(State::WaitingForCamera, millis());
}

void UsbLink::printStatus(Print& out) {
    out.printf("link: USB, %s / %s\n", headline(), detail());
}

const char* UsbLink::headline() const {
    switch (_state) {
        case State::Disabled:         return "No USB host";
        case State::WaitingForCamera: return "Plug camera";
        case State::Connecting:       return "Connecting";
        case State::Connected:        return "Connected";
        case State::Stopped:          return "Disconnected";
        case State::Waiting:          return "Retrying";
    }
    return "";
}

const char* UsbLink::detail() const {
    switch (_state) {
        case State::WaitingForCamera: return "USB-C, MTP/PTP";
        case State::Connecting:       return "USB";
        case State::Stopped:          return "button to retry";
        default:                      return _detail;
    }
}

#endif
