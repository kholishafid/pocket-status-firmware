#pragma once

#define WIFI_SSID     "Diklat"      // Nanti ini bisa dioverride via BLE
#define WIFI_PASSWORD "diklat2025"

// Tambahkan endpoint API Cloudflare lu di sini
#define CLOUDFLARE_API_URL "https://pocket-status-api.iwanfaiber1.workers.dev"

constexpr int BUTTON_PIN = 0;  // Tombol BOOT
constexpr int OLED_SDA = 21;
constexpr int OLED_SCL = 22;

constexpr double DEFAULT_LATITUDE = -7.5755;
constexpr double DEFAULT_LONGITUDE = 110.8243;
constexpr char DEFAULT_ADM4[] = "33.72.02.1004";

constexpr unsigned long DATA_FETCH_INTERVAL = 30UL * 1000UL;
constexpr unsigned long DEBOUNCE_MS = 50;

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64

constexpr float FACE_EASING = 0.25f;
constexpr float FACE_ROUNDNESS = 0.45f;

constexpr unsigned long FACE_FRAME_MS = 33;
constexpr float FACE_EYE_CY = 26.0f;
constexpr float FACE_MOUTH_CY = 55.0f;
