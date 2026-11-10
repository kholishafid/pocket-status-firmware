#include <Arduino.h>
#include <Preferences.h>
#include <WiFi.h>

#include "Config.h"
#include "WifiConnection.h"

namespace
{
constexpr unsigned long WIFI_ATTEMPT_TIMEOUT = 15000UL;
// Beri waktu WiFi.disconnect() "mengendap" supaya status lama (masih
// connected ke Wi-Fi sebelumnya) tidak dikira sukses.
constexpr unsigned long WIFI_SETTLE_TIME = 1500UL;

enum class ChangeState
{
  IDLE,
  TRYING_NEW,
  RESTORING_OLD
};

ChangeState changeState = ChangeState::IDLE;
unsigned long attemptStart = 0;

// Wi-Fi terakhir yang TERBUKTI tersambung. Ini yang dipakai untuk rollback.
String activeSsid = "";
String activePass = "";

String targetSsid = "";
String targetPass = "";
String previousSsid = "";
String previousPass = "";

void syncTime()
{
  configTime(
      7 * 3600, 0,
      "pool.ntp.org",
      "time.google.com");
}

void saveCredentials(const String &ssid, const String &password)
{
  Preferences prefs;
  prefs.begin("wifi", false);
  prefs.putString("ssid", ssid);
  prefs.putString("pass", password);
  prefs.end();
}

void beginWifi(const String &ssid, const String &password)
{
  WiFi.mode(WIFI_STA);
  WiFi.disconnect(false, false);
  WiFi.setSleep(false);
  WiFi.setAutoReconnect(true);
  WiFi.begin(ssid.c_str(), password.c_str());
}

bool isConnectedTo(const String &ssid)
{
  return WiFi.status() == WL_CONNECTED && WiFi.SSID() == ssid;
}
} // namespace

bool connectWifi()
{
  Preferences prefs;
  prefs.begin("wifi", false);
  const String ssid = prefs.getString("ssid", WIFI_SSID);
  const String pass = prefs.getString("pass", WIFI_PASSWORD);
  prefs.end();

  beginWifi(ssid, pass);

  const unsigned long start = millis();

  while (WiFi.status() != WL_CONNECTED &&
         millis() - start < 15000)
  {
    delay(250);
  }

  if (WiFi.status() != WL_CONNECTED)
  {
    Serial.println("Wi-Fi connection failed");
    return false;
  }

  Serial.println("Wi-Fi connected");

  activeSsid = ssid;
  activePass = pass;

  syncTime();

  return true;
}

bool requestWifiChange(const String &ssid, const String &password)
{
  if (changeState != ChangeState::IDLE || ssid.isEmpty())
  {
    return false;
  }

  targetSsid = ssid;
  targetPass = password;

  // Simpan Wi-Fi yang sedang dipakai sebagai cadangan.
  previousSsid = activeSsid;
  previousPass = activePass;

  beginWifi(targetSsid, targetPass);
  attemptStart = millis();
  changeState = ChangeState::TRYING_NEW;
  return true;
}

WifiChangeResult updateWifiChange()
{
  if (changeState == ChangeState::IDLE)
  {
    return WifiChangeResult::NONE;
  }

  const unsigned long elapsed = millis() - attemptStart;
  const bool settled = elapsed >= WIFI_SETTLE_TIME;
  const bool timedOut = elapsed >= WIFI_ATTEMPT_TIMEOUT;

  if (changeState == ChangeState::TRYING_NEW)
  {
    // Berhasil: pakai Wi-Fi baru, simpan permanen, sinkron waktu.
    if (settled && isConnectedTo(targetSsid))
    {
      activeSsid = targetSsid;
      activePass = targetPass;
      saveCredentials(activeSsid, activePass);
      syncTime();
      changeState = ChangeState::IDLE;
      return WifiChangeResult::SUCCESS;
    }

    if (!timedOut)
    {
      return WifiChangeResult::NONE;
    }

    // Gagal (password salah / sinyal lemah) -> kembali ke Wi-Fi lama.
    if (previousSsid.length() > 0)
    {
      beginWifi(previousSsid, previousPass);
      attemptStart = millis();
      changeState = ChangeState::RESTORING_OLD;
      return WifiChangeResult::FAILED_RESTORING;
    }

    // Tidak ada Wi-Fi lama: berhenti mencoba kredensial yang salah.
    WiFi.disconnect(false, false);
    changeState = ChangeState::IDLE;
    return WifiChangeResult::FAILED_NO_PREVIOUS;
  }

  // changeState == RESTORING_OLD
  if (settled && isConnectedTo(previousSsid))
  {
    changeState = ChangeState::IDLE;
    return WifiChangeResult::RESTORED;
  }

  if (timedOut)
  {
    // Auto-reconnect tetap mencoba Wi-Fi lama di belakang layar.
    changeState = ChangeState::IDLE;
    return WifiChangeResult::RESTORE_FAILED;
  }

  return WifiChangeResult::NONE;
}

const String &wifiChangeTargetSsid()
{
  return targetSsid;
}

const String &wifiChangePreviousSsid()
{
  return previousSsid;
}