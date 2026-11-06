#pragma once

#define WIFI_SSID     "Android"
#define WIFI_PASSWORD "qwerty12z"

constexpr int BUTTON_PIN = 0;  // Tombol BOOT
constexpr int OLED_SDA = 21;
constexpr int OLED_SCL = 22;

constexpr double LATITUDE = -7.5755;
constexpr double LONGITUDE = 110.8243;

constexpr unsigned long WEATHER_INTERVAL = 15UL * 60UL * 1000UL;
constexpr unsigned long DEBOUNCE_MS = 50;

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64

constexpr float FACE_EASING = 0.25f;
constexpr float FACE_ROUNDNESS = 0.45f;

constexpr unsigned long FACE_FRAME_MS = 33;
constexpr float FACE_EYE_CY = 26.0f;
constexpr float FACE_MOUTH_CY = 55.0f;
