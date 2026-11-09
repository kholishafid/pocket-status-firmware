#pragma once

#include <Arduino.h>

struct WifiNetwork
{
  String ssid;
  int32_t rssi = 0;
  int32_t channel = 0;
  String security;
};

class WifiScanner
{
public:
  bool startScan();
  void update();

  bool isScanning() const;
  bool isComplete() const;
  bool hasError() const;
  int16_t resultCount() const;
  bool nextNetwork(WifiNetwork &network);
};
