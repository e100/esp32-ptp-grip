#pragma once
#include <PtpIpCamera.h>

// Print every property the camera reports (code, type, read/write, current
// value, allowed values), one per line. fullScan also tries every code in the
// standard and Nikon vendor ranges, including ones DeviceInfo doesn't list.
void dumpCameraProps(NikonCamera& camera, Print& out, bool fullScan);

// First call: snapshot every property's description (full scan). Later calls:
// print the properties whose description (value, read/write, allowed values)
// changed since the previous call, then take a new snapshot.
void watchCameraProps(NikonCamera& camera, Print& out);

// Print the DeviceInfo operation, event and property lists.
void dumpDeviceInfo(NikonCamera& camera, Print& out);
