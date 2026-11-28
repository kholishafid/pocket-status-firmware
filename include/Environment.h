#pragma once

#include <Arduino.h>
#include <U8g2lib.h>

// Semua data yang bisa dihasilkan BME280 (ukur langsung + turunan).
struct EnvData
{
  bool valid = false;

  // --- Ukuran langsung dari sensor ---
  float temperature = 0.0f; // °C
  float humidity = 0.0f;    // % RH
  float pressure = 0.0f;    // hPa (tekanan di lokasi sensor)

  // --- Turunan ---
  float seaLevelPressure = 0.0f; // hPa, dikoreksi ke permukaan laut
  float altitude = 0.0f;         // m, perkiraan dari tekanan (standar 1013.25 hPa)
  float dewPoint = 0.0f;         // °C
  float heatIndex = 0.0f;        // °C ("suhu terasa")
  const char *comfort = "-";     // label kenyamanan

  // --- Statistik sejak ESP menyala ---
  float minTemp = 0.0f, maxTemp = 0.0f;
  float minHum = 0.0f, maxHum = 0.0f;
  float minPressure = 0.0f, maxPressure = 0.0f;

  // --- Tren tekanan ---
  bool trendReady = false;        // true setelah ±30 menit data
  float pressureTrend3h = 0.0f;   // perubahan hPa per 3 jam
  const char *forecast = "-";     // indikasi cuaca sederhana
};

// Panggil sekali di setup() SETELAH initDisplay() (Wire sudah aktif).
bool initEnvironment();

// Panggil terus di loop(). Tidak memblokir; membaca sensor tiap ±2 detik.
void updateEnvironment();

const EnvData &getEnvData();

// Gambar template Environment (otomatis berganti halaman tiap beberapa detik).
void drawEnvTemplate(U8G2 &display, const EnvData &env);