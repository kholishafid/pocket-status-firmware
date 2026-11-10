#include <Arduino.h>
#include <WiFi.h>

#include "BleDebug.h"
#include "Config.h"
#include "Display.h"
#include "Weather.h"
#include "CatFace.h"
#include "Finance.h"
#include "JadwalSholat.h"
#include "TemplateController.h"
#include "WifiConnection.h"

// Tarik semua variabel penampung dari BleDebug.cpp
extern String incomingTemplate;
extern String incomingSsid;
extern String incomingPassword;

static WeatherData weather;
static unsigned long lastDisplayUpdate = 0;

static FinanceData financeData;
static uint8_t financePage = 0;

static SholatData sholatData;
static WifiScanner wifiScanner;

static TemplateController templateController(
    weather,
    financeData,
    financePage,
    sholatData);

void setup()
{
  Serial.begin(115200);

  pinMode(BUTTON_PIN, INPUT_PULLUP);

  initDisplay();
  drawMessage("Connecting WiFi...");

  initCatFace(getDisplay());

  // BLE dihidupkan duluan supaya aplikasi tetap bisa mengatur Wi-Fi
  // walaupun koneksi Wi-Fi saat boot gagal.
  initBleDebug();

  connectWifi();

  templateController.render();
}

void loop()
{
  templateController.handleButton();
  templateController.updateData();

  // --- LOGIKA TANGKAP PERINTAH BLE: GANTI TEMPLATE ---
  if (incomingTemplate != "") 
  {
    templateController.setTemplateByName(incomingTemplate);
    incomingTemplate = ""; // Kosongkan lagi string-nya
  }

  // --- LOGIKA TANGKAP PERINTAH BLE: GANTI WI-FI ---
  // Wi-Fi baru dicoba dulu. Kalau gagal (misal password salah), ESP
  // otomatis kembali ke Wi-Fi sebelumnya. Kredensial baru hanya disimpan
  // kalau benar-benar tersambung.
  if (incomingSsid != "")
  {
    const String ssid = incomingSsid;
    const String pass = incomingPassword;

    // Kosongkan variabel agar tidak dieksekusi berulang
    incomingSsid = "";
    incomingPassword = "";

    Serial.println("Mencoba Wi-Fi baru dari aplikasi: " + ssid);
    if (!requestWifiChange(ssid, pass))
    {
      // Percobaan sebelumnya masih berjalan.
      notifyWifiChange("busy", ssid, "");
    }
  }

  // --- PANTAU HASIL PERCOBAAN PINDAH WI-FI ---
  switch (updateWifiChange())
  {
  case WifiChangeResult::SUCCESS:
    Serial.println("Wi-Fi baru tersambung: " + wifiChangeTargetSsid());
    notifyWifiChange(
        "success",
        wifiChangeTargetSsid(),
        wifiChangePreviousSsid());
    break;

  case WifiChangeResult::FAILED_RESTORING:
    Serial.println("Gagal ke " + wifiChangeTargetSsid() +
                   ", kembali ke " + wifiChangePreviousSsid());
    notifyWifiChange(
        "failed_restoring",
        wifiChangeTargetSsid(),
        wifiChangePreviousSsid());
    break;

  case WifiChangeResult::FAILED_NO_PREVIOUS:
    Serial.println("Gagal ke " + wifiChangeTargetSsid() +
                   ", tidak ada Wi-Fi sebelumnya");
    notifyWifiChange("failed_no_previous", wifiChangeTargetSsid(), "");
    break;

  case WifiChangeResult::RESTORED:
    Serial.println("Kembali tersambung ke " + wifiChangePreviousSsid());
    notifyWifiChange(
        "restored",
        wifiChangePreviousSsid(),
        wifiChangePreviousSsid());
    break;

  case WifiChangeResult::RESTORE_FAILED:
    Serial.println("Gagal kembali ke " + wifiChangePreviousSsid());
    notifyWifiChange(
        "restore_failed",
        wifiChangePreviousSsid(),
        wifiChangePreviousSsid());
    break;

  default:
    break;
  }

  // --- RENDER LAYAR ---
  if (templateController.current() == Template::IDLE_FACE)
  {
    updateCatFace();
  }
  else if (millis() - lastDisplayUpdate >= 1000)
  {
    lastDisplayUpdate = millis();
    templateController.render();
  }

  updateBleDebug(wifiScanner);

  delay(1);
}