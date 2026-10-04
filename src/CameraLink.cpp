#include "CameraLink.h"
#include <WiFi.h>
#include <ESPmDNS.h>
#include <Preferences.h>
#include <string.h>

static const char*    HOST_NAME          = "ptp-grip";  // sent in the PTP/IP handshake and mDNS
static const uint32_t WIFI_RETRY_MS      = 20000;       // restart WiFi.begin() if not joined by then
static const uint32_t CODE_TIMEOUT_MS    = 120000;      // user hasn't pressed a button
static const uint32_t CONFIRM_TIMEOUT_MS = 120000;      // user hasn't confirmed on the camera
static const uint32_t RECONNECT_MS       = 60000;       // camera may restart its WiFi after pairing
static const uint32_t POLL_MS            = 500;

// The camera remembers a paired host by GUID, so the grip keeps one for life.
void CameraLink::_loadIdentity() {
    uint8_t guid[PTPIP_GUID_SIZE];
    Preferences prefs;
    prefs.begin("ptpip", false);
    if (prefs.getBytes("guid", guid, sizeof(guid)) != sizeof(guid)) {
        esp_fill_random(guid, sizeof(guid));
        prefs.putBytes("guid", guid, sizeof(guid));
        Serial.println("link: created host GUID");
    }
    prefs.end();
    _session.setHostIdentity(guid, HOST_NAME);
}

void CameraLink::begin(const grip::CameraConfig* cameras, size_t count) {
    // Keep only usable entries (an SSID is required)
    _cams = cameras;
    _camCount = count;
    _numCams = 0;
    for (size_t i = 0; i < count; i++) if (cameras[i].ssid && cameras[i].ssid[0]) _numCams++;
    if (_numCams == 0) {
        Serial.println("link: no camera WiFi configured (include/camera_wifi.h), WiFi disabled");
        _set(State::Disabled, millis());
        return;
    }
    _loadIdentity();
    WiFi.mode(WIFI_STA);
    WiFi.setHostname(HOST_NAME);
    WiFi.setAutoReconnect(true);
    _startWifi(millis());
}

// One camera: join it directly. Several: scan and pick the strongest known one.
void CameraLink::_startWifi(uint32_t now) {
    if (_numCams == 1) {
        for (size_t i = 0; i < _camCount; i++) {
            if (_cams[i].ssid && _cams[i].ssid[0]) { _cfg = _cams[i]; break; }
        }
        WiFi.begin(_cfg.ssid, _cfg.password);
        Serial.printf("link: joining WiFi '%s'\n", _cfg.ssid);
        _set(State::JoiningWifi, now);
        return;
    }
    WiFi.disconnect();
    WiFi.scanNetworks(true);                       // async; checked in update()
    Serial.println("link: scanning for a camera's WiFi");
    _set(State::Scanning, now);
}

bool CameraLink::_pickFromScan() {
    int n = WiFi.scanComplete();
    int bestRssi = -1000;
    const grip::CameraConfig* best = nullptr;
    for (int i = 0; i < n; i++) {
        String ssid = WiFi.SSID(i);
        for (size_t k = 0; k < _camCount; k++) {
            const grip::CameraConfig& c = _cams[k];
            if (c.ssid && c.ssid[0] && ssid == c.ssid && WiFi.RSSI(i) > bestRssi) {
                bestRssi = WiFi.RSSI(i);
                best = &c;
            }
        }
    }
    WiFi.scanDelete();
    if (!best) return false;
    _cfg = *best;
    Serial.printf("link: found %s (%s, %d dBm)\n", _cfg.name, _cfg.ssid, bestRssi);
    return true;
}

void CameraLink::suspend() {
    if (_state == State::Disabled || _state == State::Suspended) return;
    if (_state == State::ShowingCode || _state == State::ConfirmOnCamera) _camera.cancelPairing();
    else if (_state == State::Connected) _camera.end();
    if (_mdnsStarted) {
        MDNS.end();
        _mdnsStarted = false;
    }
    WiFi.scanDelete();
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    Serial.println("link: WiFi off");
    _set(State::Suspended, millis());
}

void CameraLink::resume() {
    if (_state != State::Suspended) return;
    Serial.println("link: WiFi on");
    WiFi.mode(WIFI_STA);
    _failures = 0;
    _startWifi(millis());
}

void CameraLink::_set(State s, uint32_t now) {
    _state = s;
    _since = now;
    _changed = true;
}

// Back off on repeated failures (x2 each time, up to 30 s), so a camera that
// is already in trouble isn't hit with a new connection every few seconds.
void CameraLink::_retry(uint32_t now, uint32_t afterMs, const char* why) {
    if (_failures < 8) _failures++;
    for (uint8_t i = 1; i < _failures && afterMs < 30000; i++) afterMs *= 2;
    if (afterMs > 30000) afterMs = 30000;
    Serial.printf("link: %s, retrying in %us\n", why, (unsigned)(afterMs / 1000));
    snprintf(_detail, sizeof(_detail), "%s", why);
    _retryAt = now + afterMs;
    _set(State::Waiting, now);
}

void CameraLink::update(uint32_t now) {
    if (_state == State::Disabled || _state == State::Suspended) return;

    if (_state != State::JoiningWifi && _state != State::Scanning && WiFi.status() != WL_CONNECTED) {
        Serial.println("link: WiFi lost");
        if (_state == State::ShowingCode || _state == State::ConfirmOnCamera) _camera.cancelPairing();
        else _camera.end();
        _startWifi(now);
        return;
    }

    switch (_state) {
        case State::Scanning: {
            int n = WiFi.scanComplete();
            if (n == WIFI_SCAN_RUNNING) break;
            if (n >= 0 && _pickFromScan()) {
                WiFi.begin(_cfg.ssid, _cfg.password);
                Serial.printf("link: joining WiFi '%s'\n", _cfg.ssid);
                _set(State::JoiningWifi, now);
            } else if (now - _since > 5000) {           // nothing known in range: scan again
                WiFi.scanDelete();
                WiFi.scanNetworks(true);
                _since = now;
            }
            break;
        }

        case State::JoiningWifi:
            if (WiFi.status() == WL_CONNECTED) {
                Serial.printf("link: WiFi joined, IP %s\n", WiFi.localIP().toString().c_str());
                if (!_mdnsStarted) _mdnsStarted = MDNS.begin(HOST_NAME);
                _set(State::Finding, now);
            } else if (now - _since > WIFI_RETRY_MS) {
                Serial.println("link: WiFi not joined yet, retrying");
                if (_numCams > 1) {
                    _startWifi(now);                    // another camera may be in range now
                } else {
                    WiFi.disconnect();
                    WiFi.begin(_cfg.ssid, _cfg.password);
                    _since = now;
                }
            }
            break;

        case State::Finding:
            _advert = _discover();
            if (_advert == Advert::Unpaired || _forcePair) _startPairing(now);
            else _set(State::Connecting, now);
            break;

        case State::Connecting:
            _connect(now);
            break;

        case State::ShowingCode:
            if (now - _since > CODE_TIMEOUT_MS) {
                Serial.println("link: no button press, pairing cancelled");
                cancel();
            }
            break;

        case State::ConfirmOnCamera: {
            CameraResult r = _camera.pollCameraConfirmation();
            if (r == CAM_OK) {
                Serial.println("link: confirmed on camera, reconnecting");
                _camera.finishPairing();
                _forcePair = false;
                _reconnectUntil = now + RECONNECT_MS;
                _set(State::Connecting, now);
            } else if (r == CAM_E_NOT_CONNECTED) {
                _camera.cancelPairing();
                _retry(now, 3000, "camera hung up");
            } else if (now - _since > CONFIRM_TIMEOUT_MS) {
                Serial.println("link: not confirmed on camera, pairing cancelled");
                cancel();
            }
            break;
        }

        case State::Connected:
            if (now - _lastPoll >= POLL_MS) {
                _lastPoll = now;
                _camera.poll();
                if (!_camera.isReady()) _retry(now, 2000, "camera lost");
            }
            break;

        case State::OtherHost:
            // Keep checking: the user may switch to a free profile on the camera.
            if (now - _since > 5000) _set(State::Finding, now);
            break;

        case State::Waiting:
            if ((int32_t)(now - _retryAt) >= 0) _set(State::Finding, now);
            break;

        case State::Disabled:
        case State::Stopped:
        case State::Suspended:
            break;
    }
}

// Reads the camera's `apps` TXT record: PAIR = no host paired to the active
// profile, $DSC = paired. Unknown if the camera isn't found.
CameraLink::Advert CameraLink::_discover() {
    if (!_mdnsStarted) return Advert::Unknown;
    int n = MDNS.queryService("ptp", "tcp");
    int pick = -1;
    for (int i = 0; i < n; i++) {
        bool sameIp = MDNS.IP(i).toString() == _cfg.host;
        Serial.printf("link: found %s at %s:%u%s\n", MDNS.hostname(i).c_str(),
                      MDNS.IP(i).toString().c_str(), MDNS.port(i), sameIp ? "" : " (not the configured IP)");
        for (int k = 0; k < MDNS.numTxt(i); k++) {
            Serial.printf("link:   %s=%s\n", MDNS.txtKey(i, k).c_str(), MDNS.txt(i, k).c_str());
        }
        if (sameIp || pick < 0) pick = i;
    }
    if (pick < 0) {
        Serial.println("link: camera not found over mDNS, trying to connect anyway");
        return Advert::Unknown;
    }
    String apps = MDNS.txt(pick, "apps");
    if (apps == "PAIR") return Advert::Unpaired;
    if (apps == "$DSC") return Advert::Paired;
    return Advert::Unknown;
}

void CameraLink::_connect(uint32_t now) {
    Serial.printf("link: connecting to %s\n", _cfg.host);
    CameraResult r = _camera.begin(_cfg.host);
    if (r == CAM_OK) {
        const PtpDeviceInfo& info = _camera.deviceInfo();
        Serial.printf("link: connected to %s %s\n", info.manufacturer, info.model);
        snprintf(_detail, sizeof(_detail), "%.23s", info.model);
        _reconnectUntil = 0;
        _failures = 0;
        _lastPoll = now;
        _set(State::Connected, now);
        return;
    }

    Serial.printf("link: begin failed: %s\n", cameraResultStr(r));
    if ((int32_t)(_reconnectUntil - now) > 0) {      // just paired: camera may still be restarting WiFi
        _retry(now, 3000, "camera restarting");
        return;
    }
    if (r == CAM_E_NOT_PAIRED) {
        if (_advert == Advert::Paired) {
            Serial.println("link: camera is paired with another device. Pick a profile with no "
                           "pairing on the camera (or create one), or send 'p' to try pairing anyway");
            _set(State::OtherHost, now);
        } else {
            _startPairing(now);
        }
        return;
    }
    _retry(now, 3000, cameraResultStr(r));
}

void CameraLink::_startPairing(uint32_t now) {
    Serial.println("link: requesting pairing code");
    CameraResult r = _camera.startPairing(_cfg.host, _code, sizeof(_code));
    if (r != CAM_OK) {
        _retry(now, 5000, "pairing refused");
        return;
    }
    Serial.printf("link: pairing code %s. If the camera shows the same code, press any button "
                  "(or send 'y'); 'n' cancels\n", _code);
    _set(State::ShowingCode, now);
}

bool CameraLink::onButtonPress() {
    uint32_t now = millis();
    if (_state == State::ShowingCode) {
        CameraResult r = _camera.confirmPairing();
        if (r == CAM_OK) {
            Serial.println("link: code accepted. Confirm on the camera");
            _set(State::ConfirmOnCamera, now);
        } else {
            _retry(now, 5000, "confirm failed");
        }
        return true;
    }
    if (_state == State::Stopped) {
        _set(State::Finding, now);
        return true;
    }
    return false;
}

void CameraLink::cancel() {
    if (_state != State::ShowingCode && _state != State::ConfirmOnCamera) return;
    _camera.cancelPairing();
    _forcePair = false;
    _detail[0] = '\0';
    Serial.println("link: pairing cancelled. Press a button (or send 'p') to start again");
    _set(State::Stopped, millis());
}

void CameraLink::disconnect() {
    if (_state == State::Disabled || _state == State::Suspended) return;
    if (_state == State::ShowingCode || _state == State::ConfirmOnCamera) _camera.cancelPairing();
    else if (_state == State::Connected) _camera.end();
    Serial.println("link: disconnected. Send 'c' or press a button to reconnect");
    snprintf(_detail, sizeof(_detail), "disconnected");
    _set(State::Stopped, millis());
}

void CameraLink::connect() {
    if (_state != State::Stopped) return;
    _failures = 0;
    _set(State::Finding, millis());
}

void CameraLink::forcePair() {
    if (_state == State::Disabled || _state == State::JoiningWifi || _state == State::Scanning ||
        _state == State::Suspended) return;
    if (_state == State::Connected) _camera.end();
    if (_state == State::ShowingCode || _state == State::ConfirmOnCamera) return;
    _forcePair = true;
    _set(State::Finding, millis());
}

void CameraLink::printStatus(Print& out) {
    out.printf("link: %s / %s\n", headline(), detail());
    if (_state == State::Disabled) return;
    out.printf("link: WiFi %s, IP %s\n", WiFi.status() == WL_CONNECTED ? "joined" : "not joined",
               WiFi.localIP().toString().c_str());
    const uint8_t* g = _session.hostGuid();
    out.print("link: host GUID ");
    for (int i = 0; i < PTPIP_GUID_SIZE; i++) out.printf("%02x", g[i]);
    out.println();
}

const char* CameraLink::headline() const {
    switch (_state) {
        case State::Disabled:        return "No camera";
        case State::Scanning:        return "Finding WiFi";
        case State::JoiningWifi:     return "Joining WiFi";
        case State::Finding:         return "Finding cam";
        case State::Connecting:      return "Connecting";
        case State::ShowingCode:     return _code;
        case State::ConfirmOnCamera: return "OK on camera";
        case State::Connected:       return "Connected";
        case State::OtherHost:       return "Cam taken";
        case State::Stopped:         return strcmp(_detail, "disconnected") == 0 ? "Disconnected" : "Not paired";
        case State::Waiting:         return "Retrying";
        case State::Suspended:       return "WiFi off";
    }
    return "";
}

const char* CameraLink::detail() const {
    switch (_state) {
        case State::Disabled:        return "set SSID in config";
        case State::Scanning:        return "no known camera yet";
        case State::JoiningWifi:     return _cfg.name && _cfg.name[0] ? _cfg.name : _cfg.ssid;
        case State::Finding:         return _cfg.host;
        case State::Connecting:      return _cfg.host;
        case State::ShowingCode:     return "same? press a button";
        case State::ConfirmOnCamera: return "confirm the grip";
        case State::Connected:       return _detail;
        case State::OtherHost:       return "use a free profile";
        case State::Stopped:         return "button to retry";
        case State::Waiting:         return _detail;
        case State::Suspended:       return "";
    }
    return "";
}
