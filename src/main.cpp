#include <Arduino.h>
#include <WiFi.h>
#include <time.h>

#include "Config.h"
#include "Display.h"
#include "Weather.h"
#include "CatFace.h"

enum class Template
{
  IDLE_FACE,
  CLOCK,
  WEATHER
};

static Template currentTemplate = Template::IDLE_FACE;
static WeatherData weather;

static bool lastReading = HIGH;
static bool stableButtonState = HIGH;

static unsigned long lastDebounceTime = 0;
static unsigned long lastWeatherUpdate = 0;
static unsigned long lastDisplayUpdate = 0;

static void handleButton()
{
  bool reading = digitalRead(BUTTON_PIN);

  if (reading != lastReading)
  {
    lastDebounceTime = millis();
  }

  if (millis() - lastDebounceTime >= DEBOUNCE_MS)
  {
    if (reading != stableButtonState)
    {
      stableButtonState = reading;

      if (stableButtonState == LOW)
      {
        switch (currentTemplate)
        {
        case Template::CLOCK:
          currentTemplate = Template::WEATHER;
          break;

        case Template::WEATHER:
          currentTemplate = Template::IDLE_FACE;
          break;

        case Template::IDLE_FACE:
          currentTemplate = Template::CLOCK;
          break;
        }

        Serial.println("Template changed");
      }
    }
  }

  lastReading = reading;
}

static void renderTemplate()
{

  switch (currentTemplate)
  {
  case Template::IDLE_FACE:
    // updateCatFace();
    break;
  case Template::CLOCK:
    struct tm timeInfo;
    if (getLocalTime(&timeInfo, 10))
    {
      char timeText[12];
      char dateText[24];

      strftime(
          timeText, sizeof(timeText),
          "%H:%M:%S", &timeInfo);

      strftime(
          dateText, sizeof(dateText),
          "%a, %d %b", &timeInfo);

      drawClock(timeText, dateText);
    }
    else
    {
      drawMessage("Waiting for time...");
    }
    break;

  case Template::WEATHER:
    drawWeather(weather);
    break;
  }
}

void setup()
{
  Serial.begin(115200);

  pinMode(BUTTON_PIN, INPUT_PULLUP);

  initDisplay();
  drawMessage("Connecting WiFi...");

  // initEmotionFace(getDisplay());
  initCatFace(getDisplay());

  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  unsigned long start = millis();

  while (WiFi.status() != WL_CONNECTED &&
         millis() - start < 15000)
  {
    delay(250);
  }

  if (WiFi.status() == WL_CONNECTED)
  {
    Serial.println("Wi-Fi connected");

    configTime(
        7 * 3600, 0,
        "pool.ntp.org",
        "time.google.com");

    if (fetchWeather(weather))
    {
      lastWeatherUpdate = millis();
    }
  }
  else
  {
    Serial.println("Wi-Fi connection failed");
  }

  renderTemplate();
}

void loop()
{
  handleButton();

  // Jangan melakukan HTTP request saat animasi berjalan.
  // Pembaruan cuaca akan dilanjutkan setelah keluar dari FACE.
  if (currentTemplate != Template::IDLE_FACE &&
      WiFi.status() == WL_CONNECTED &&
      millis() - lastWeatherUpdate >= WEATHER_INTERVAL)
  {

    if (fetchWeather(weather))
    {
      lastWeatherUpdate = millis();
    }
    else
    {
      lastWeatherUpdate =
          millis() - WEATHER_INTERVAL + 60000UL;
    }
  }

  if (currentTemplate == Template::IDLE_FACE)
  {
    // Animasi diperiksa setiap putaran loop.
    updateCatFace();
  }
  else if (millis() - lastDisplayUpdate >= 500)
  {
    lastDisplayUpdate = millis();
    renderTemplate();
  }

  delay(1);
}
