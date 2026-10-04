#pragma once
// Copy this file to camera_wifi.h and fill in your cameras' WiFi details.
// camera_wifi.h is ignored by git, so the keys stay out of the repo.
//
// One line per camera: { name, SSID, key, IP }. The grip joins whichever
// listed camera is in range (strongest signal). The IP is 192.168.1.1 for a
// Nikon in "Direct connection to computer" mode. Entries with an empty SSID
// are ignored; with none left, WiFi stays off and only USB is used.
#define CAMERA_WIFI_LIST \
    { "ZR", "", "", "192.168.1.1" },
//  { "Z8", "Z8_1234567", "key", "192.168.1.1" },
