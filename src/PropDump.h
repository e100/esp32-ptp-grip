#pragma once
#include <PtpIpCamera.h>

// Print every property the camera reports (code, type, read/write, current
// value, allowed values), one per line. fullScan also tries every code in the
// standard and Nikon vendor ranges, including ones DeviceInfo doesn't list.
void dumpCameraProps(NikonCamera& camera, Print& out, bool fullScan);

// Print the DeviceInfo operation, event and property lists.
void dumpDeviceInfo(NikonCamera& camera, Print& out);
