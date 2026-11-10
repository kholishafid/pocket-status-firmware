#include "WifiScanner.h"

#include <WiFi.h>

namespace
{
bool scanRunning = false;
bool scanFinished = false;
bool scanFailed = false;
int16_t scanResults = 0;
int16_t nextResult = 0;
bool reconnectAfterScan = false;

const char *securityName(wifi_auth_mode_t mode)
{
  switch (mode)
  {
  case WIFI_AUTH_OPEN:
    return "OPEN";
  case WIFI_AUTH_WEP:
    return "WEP";
  case WIFI_AUTH_WPA_PSK:
    return "WPA_PSK";
  case WIFI_AUTH_WPA2_PSK:
    return "WPA2_PSK";
  case WIFI_AUTH_WPA_WPA2_PSK:
    return "WPA_WPA2_PSK";
  case WIFI_AUTH_WPA2_ENTERPRISE:
    return "WPA2_ENTERPRISE";
  case WIFI_AUTH_WPA3_PSK:
    return "WPA3_PSK";
  case WIFI_AUTH_WPA2_WPA3_PSK:
    return "WPA2_WPA3_PSK";
  case WIFI_AUTH_WAPI_PSK:
    return "WAPI_PSK";
  case WIFI_AUTH_OWE:
    return "OWE";
  default:
    return "UNKNOWN";
  }
}
}

bool WifiScanner::startScan()
{
  if (scanRunning)
  {
    return false;
  }

  if (WiFi.status() == WL_CONNECTED)
  {
    reconnectAfterScan = true;
    WiFi.disconnect(false, false);
  }

  const int16_t currentScan = WiFi.scanComplete();
  if (currentScan == WIFI_SCAN_RUNNING)
  {
    scanRunning = true;
    scanFinished = false;
    return false;
  }

  WiFi.scanDelete();
  scanResults = 0;
  nextResult = 0;
  scanFinished = false;
  scanFailed = false;

  if (WiFi.scanNetworks(true, true) == WIFI_SCAN_FAILED)
  {
    if (reconnectAfterScan)
    {
      WiFi.reconnect();
      reconnectAfterScan = false;
    }
    return false;
  }

  scanRunning = true;
  return true;
}

void WifiScanner::update()
{
  if (!scanRunning)
  {
    return;
  }

  const int16_t result = WiFi.scanComplete();
  if (result == WIFI_SCAN_RUNNING)
  {
    return;
  }

  scanRunning = false;
  if (reconnectAfterScan)
  {
    WiFi.reconnect();
    reconnectAfterScan = false;
  }

  if (result < 0)
  {
    scanResults = 0;
    scanFailed = true;
    scanFinished = true;
    return;
  }

  scanResults = result;
  nextResult = 0;
  scanFinished = true;
}

bool WifiScanner::isScanning() const
{
  return scanRunning;
}

bool WifiScanner::isComplete() const
{
  return scanFinished;
}

bool WifiScanner::hasError() const
{
  return scanFailed;
}

int16_t WifiScanner::resultCount() const
{
  return scanResults;
}

bool WifiScanner::nextNetwork(WifiNetwork &network)
{
  if (!scanFinished || nextResult >= scanResults)
  {
    return false;
  }

  const uint8_t index = static_cast<uint8_t>(nextResult++);
  network.ssid = WiFi.SSID(index);
  network.rssi = WiFi.RSSI(index);
  network.channel = WiFi.channel(index);
  network.security = securityName(WiFi.encryptionType(index));
  return true;
}
