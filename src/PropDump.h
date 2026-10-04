#pragma once
#include <PtpIpCamera.h>

// Print every property the camera reports (code, type, read/write, current
// value, allowed values), one per line.
void dumpCameraProps(NikonCamera& camera, Print& out);
