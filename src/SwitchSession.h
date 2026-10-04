#pragma once
// ============================================================================
// SwitchSession — a PtpSession that forwards to whichever transport is active
// (PTP/IP or USB), so one NikonCamera can be used over either. AutoLink
// selects the transport; the camera must be end()ed before switching.
// ============================================================================

#include <PtpIpCamera.h>

class SwitchSession : public PtpSession {
public:
    explicit SwitchSession(PtpSession& initial) : _active(&initial) {}

    void        select(PtpSession& s) { _active = &s; }
    PtpSession& active() const { return *_active; }

    bool connect(const char* host) override { return _active->connect(host); }
    void disconnect() override { _active->disconnect(); }
    bool isConnected() override { return _active->isConnected(); }
    uint32_t lastInitFailReason() const override { return _active->lastInitFailReason(); }
    bool openSession() override { return _active->openSession(); }
    bool closeSession() override { return _active->closeSession(); }
    bool command(uint16_t op, const uint32_t* params, uint8_t n, uint16_t& rc) override {
        return _active->command(op, params, n, rc);
    }
    bool dataIn(uint16_t op, const uint32_t* params, uint8_t n, uint8_t* data, size_t maxLen,
                size_t& len, uint16_t& rc) override {
        return _active->dataIn(op, params, n, data, maxLen, len, rc);
    }
    bool dataOut(uint16_t op, const uint32_t* params, uint8_t n, const uint8_t* data, size_t len,
                 uint16_t& rc) override {
        return _active->dataOut(op, params, n, data, len, rc);
    }
    bool nextEvent(PtpEvent& ev) override { return _active->nextEvent(ev); }

private:
    PtpSession* _active;
};
