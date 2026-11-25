#pragma once

#include <Arduino.h>

// Hasil percobaan pindah Wi-Fi. updateWifiChange() mengembalikan nilai selain
// NONE satu kali saja, tepat saat ada kejadian baru.
enum class WifiChangeResult
{
  NONE,
  SUCCESS,            // Wi-Fi baru tersambung dan kredensialnya disimpan
  FAILED_RESTORING,   // Wi-Fi baru gagal, sedang mengembalikan ke Wi-Fi lama
  FAILED_NO_PREVIOUS, // Wi-Fi baru gagal, dan memang tidak ada Wi-Fi lama
  RESTORED,           // Wi-Fi lama berhasil tersambung lagi
  RESTORE_FAILED      // Wi-Fi lama juga gagal tersambung
};

// Koneksi saat boot (memakai kredensial tersimpan, atau default dari Config.h).
bool connectWifi();

// Mulai mencoba Wi-Fi baru TANPA memblokir loop(). Return false kalau
// percobaan sebelumnya masih berjalan.
bool requestWifiChange(const String &ssid, const String &password);

// Panggil terus di loop() untuk memajukan proses pindah Wi-Fi.
WifiChangeResult updateWifiChange();

const String &wifiChangeTargetSsid();
const String &wifiChangePreviousSsid();