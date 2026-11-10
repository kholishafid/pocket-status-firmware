#pragma once

#include <Arduino.h>

void beginWifi();
void updateWifi();

bool configureWifi(const String &ssid, const String &password);
void clearWifiConfiguration();

bool hasWifiConfiguration();
bool isWifiConnecting();
const String &getConfiguredWifiSsid();
