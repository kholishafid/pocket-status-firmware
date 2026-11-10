#include <Arduino.h>
#include <Preferences.h>
#include <WiFi.h>

#include "Config.h"
#include "WifiConnection.h"

namespace
{
constexpr char WIFI_NAMESPACE[] = "wifi";
constexpr char WIFI_SSID_KEY[] = "ssid";
constexpr char WIFI_PASSWORD_KEY[] = "password";
constexpr unsigned long WIFI_CONNECT_TIMEOUT = 15000UL;
constexpr unsigned long WIFI_RETRY_INTERVAL = 30000UL;

String configuredSsid;
String configuredPassword;
bool connectionAttemptActive = false;
unsigned long connectionAttemptStarted = 0;
unsigned long lastConnectionAttempt = 0;

void loadWifiConfiguration()
{
  Preferences preferences;
  preferences.begin(WIFI_NAMESPACE, true);
  configuredSsid = preferences.getString(WIFI_SSID_KEY, "");
  configuredPassword = preferences.getString(WIFI_PASSWORD_KEY, "");
  preferences.end();
}

void startConnectionAttempt()
{
  if (configuredSsid.isEmpty())
  {
    return;
  }

  WiFi.disconnect(false, false);
  WiFi.begin(configuredSsid.c_str(), configuredPassword.c_str());
  connectionAttemptActive = true;
  connectionAttemptStarted = millis();
  lastConnectionAttempt = connectionAttemptStarted;

  Serial.printf(
      "[WIFI] Connecting to SSID: %s\n",
      configuredSsid.c_str());
}
}

void beginWifi()
{
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.setAutoReconnect(true);
  loadWifiConfiguration();

  if (configuredSsid.isEmpty())
  {
    Serial.println("[WIFI] No saved Wi-Fi configuration");
    return;
  }

  startConnectionAttempt();
}

void updateWifi()
{
  if (configuredSsid.isEmpty())
  {
    return;
  }

  if (WiFi.status() == WL_CONNECTED)
  {
    if (connectionAttemptActive)
    {
      connectionAttemptActive = false;
      Serial.printf(
          "[WIFI] Connected, IP=%s\n",
          WiFi.localIP().toString().c_str());
      configTime(
          7 * 3600,
          0,
          "pool.ntp.org",
          "time.google.com");
    }
    return;
  }

  if (connectionAttemptActive)
  {
    if (millis() - connectionAttemptStarted >= WIFI_CONNECT_TIMEOUT)
    {
      connectionAttemptActive = false;
      Serial.println("[WIFI] Connection timeout");
    }
    return;
  }

  if (millis() - lastConnectionAttempt >= WIFI_RETRY_INTERVAL)
  {
    startConnectionAttempt();
  }
}

bool configureWifi(const String &ssid, const String &password)
{
  if (ssid.isEmpty())
  {
    return false;
  }

  Preferences preferences;
  if (!preferences.begin(WIFI_NAMESPACE, false))
  {
    Serial.println("[WIFI] Failed to open persistent configuration");
    return false;
  }

  preferences.putString(WIFI_SSID_KEY, ssid);
  preferences.putString(WIFI_PASSWORD_KEY, password);
  preferences.end();

  configuredSsid = ssid;
  configuredPassword = password;
  startConnectionAttempt();
  Serial.println("[WIFI] Configuration saved");
  return true;
}

void clearWifiConfiguration()
{
  Preferences preferences;
  if (preferences.begin(WIFI_NAMESPACE, false))
  {
    preferences.clear();
    preferences.end();
  }

  WiFi.disconnect(true, true);
  configuredSsid.clear();
  configuredPassword.clear();
  connectionAttemptActive = false;
  lastConnectionAttempt = 0;
  Serial.println("[WIFI] Configuration cleared");
}

bool hasWifiConfiguration()
{
  return !configuredSsid.isEmpty();
}

bool isWifiConnecting()
{
  return connectionAttemptActive;
}

const String &getConfiguredWifiSsid()
{
  return configuredSsid;
}
