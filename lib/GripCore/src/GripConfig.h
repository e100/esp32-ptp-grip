#pragma once
#include "GripTypes.h"

// ============================================================================
// Hardware description types used by include/grip_config.h.
// ============================================================================

namespace grip {

struct EncoderConfig {
    uint8_t     pinA;
    uint8_t     pinB;
    int8_t      countsPerDetent;   // quadrature counts per click: 4 for most EC11 encoders, 2 for half-step ones
                                   // (if only every other click registers, halve it)
    bool        reverse;           // flip direction
    const char* label;
};

struct ButtonConfig {
    uint8_t     pin;
    bool        activeLow;         // true: switch to GND, internal pull-up enabled
    const char* label;
};

struct DisplayConfig {
    bool    enabled;
    uint8_t sdaPin;
    uint8_t sclPin;
};

struct CameraConfig {
    const char* ssid;              // camera's WiFi network; empty = no camera (input only)
    const char* password;
    const char* host;              // camera's IP address on that network
};

}  // namespace grip
