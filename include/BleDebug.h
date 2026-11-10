#pragma once

#include <Arduino.h>

#include "WifiScanner.h"

void initBleDebug();
void updateBleDebug(WifiScanner &wifiScanner);

// Kirim hasil percobaan pindah Wi-Fi ke aplikasi Flutter.
// state: "success", "failed_restoring", "failed_no_previous",
//        "restored", "restore_failed", "busy"
void notifyWifiChange(
    const char *state,
    const String &ssid,
    const String &previousSsid);