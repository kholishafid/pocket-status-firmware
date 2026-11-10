
#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

#include "JadwalSholat.h"

static const char *SHOLAT_URL =
    "https://pocket-status-api.iwanfaiber1.workers.dev/api/sholat";

// =====================================================
// FETCH DATA
// =====================================================

bool fetchSholat(SholatData &data)
{
  if (WiFi.status() != WL_CONNECTED)
  {
    Serial.println("Sholat: Wi-Fi disconnected");
    return false;
  }

  WiFiClientSecure client;
  client.setInsecure(); // Prototype only

  HTTPClient http;
  http.setConnectTimeout(8000);
  http.setTimeout(8000);

  Serial.println("Fetching sholat schedule...");

  if (!http.begin(client, SHOLAT_URL))
  {
    Serial.println("Sholat: HTTP begin failed");
    return false;
  }

  http.addHeader("Accept", "application/json");

  int status = http.GET();

  if (status != HTTP_CODE_OK)
  {
    Serial.printf(
        "Sholat HTTP error: %d (%s)\n",
        status,
        HTTPClient::errorToString(status).c_str());

    http.end();
    return false;
  }

  String payload = http.getString();
  http.end();

  if (payload.isEmpty())
  {
    Serial.println("Sholat: empty response");
    return false;
  }

  JsonDocument doc;
  DeserializationError error =
      deserializeJson(doc, payload);

  if (error)
  {
    Serial.printf(
        "Sholat JSON error: %s\n",
        error.c_str());
    return false;
  }

  if (doc["status"] != "success")
  {
    Serial.println("Sholat: API status is not success");
    return false;
  }

  JsonObject json = doc["data"];

  if (json.isNull())
  {
    Serial.println("Sholat: missing data object");
    return false;
  }

  // Gunakan objek sementara agar data lama tetap aman
  // jika respons API tidak lengkap.
  SholatData result;

  result.tanggalMasehi =
      json["tanggal_masehi"] | "";

  result.tanggalHijriah =
      json["tanggal_hijriah"] | "";

  result.subuh = json["subuh"] | "";
  result.dzuhur = json["dzuhur"] | "";
  result.ashar = json["ashar"] | "";
  result.maghrib = json["maghrib"] | "";
  result.isya = json["isya"] | "";
  result.imsak = json["imsak"] | "";

  result.jamServer = json["jam_server"] | "";
  result.nextSholat = json["next_sholat"] | "";
  result.nextWaktu = json["next_waktu"] | "";

  // Pastikan field utama tersedia.
  if (result.subuh.isEmpty() ||
      result.dzuhur.isEmpty() ||
      result.ashar.isEmpty() ||
      result.maghrib.isEmpty() ||
      result.isya.isEmpty())
  {
    Serial.println("Sholat: required schedule missing");
    return false;
  }

  result.valid = true;
  data = result;

  Serial.printf(
      "Sholat updated: next %s at %s\n",
      data.nextSholat.c_str(),
      data.nextWaktu.c_str());

  return true;
}

// =====================================================
// DISPLAY HELPERS
// =====================================================

static void drawPrayerRow(
    U8G2 &oled,
    int x,
    int y,
    const char *name,
    const String &time)
{
  oled.drawStr(x, y, name);

  oled.drawStr(
      x + 38,
      y,
      time.c_str());
}

// =====================================================
// DRAW TEMPLATE
// =====================================================

void drawSholatTemplate(
    U8G2 &oled,
    const SholatData &data)
{
  oled.clearBuffer();
  oled.setFont(u8g2_font_5x8_tf);

  if (!data.valid)
  {
    oled.drawStr(5, 30, "No Wi-Fi / Internet");
    oled.drawStr(5, 43, "Configure via BLE");
    oled.sendBuffer();
    return;
  }

  // Tanggal Hijriah
  oled.drawStr(
      0, 20,
      data.tanggalHijriah.c_str());

  // Salat berikutnya
  oled.setFont(u8g2_font_6x10_tf);
  oled.drawStr(0, 32, "NEXT:");

  String next = data.nextSholat + " " + data.nextWaktu;
  oled.drawStr(34, 32, next.c_str());

  oled.drawHLine(0, 36, 128);

  // Jadwal dua kolom
  oled.setFont(u8g2_font_5x8_tf);

  String row1 = "Fjr  " + data.subuh;
  String row2 = "Dhu  " + data.dzuhur;
  String row3 = "Asr  " + data.ashar;

  String row4 = "Mgh  " + data.maghrib;
  String row5 = "Isha " + data.isya;
  String row6 = "Imsk " + data.imsak;

  oled.drawStr(0, 45, row1.c_str());
  oled.drawStr(0, 53, row2.c_str());
  oled.drawStr(0, 61, row3.c_str());

  oled.drawStr(66, 45, row4.c_str());
  oled.drawStr(66, 53, row5.c_str());
  oled.drawStr(66, 61, row6.c_str());

  oled.sendBuffer();
}
