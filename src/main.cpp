#include <Arduino.h>
#include <WiFi.h>
#include <time.h>

#include "Config.h"
#include "Display.h"
#include "Weather.h"
#include "CatFace.h"
#include "Finance.h"
#include "JadwalSholat.h"

enum class Template
{
  IDLE_FACE,
  CLOCK,
  WEATHER,
  FINANCE,
  JADWAL_SHOLAT
};

static Template currentTemplate = Template::WEATHER;
static WeatherData weather;

static bool lastReading = HIGH;
static bool stableButtonState = HIGH;

static unsigned long lastDebounceTime = 0;
static unsigned long lastWeatherUpdate = 0;
static unsigned long lastDisplayUpdate = 0;

static FinanceData financeData;
static uint8_t financePage = 0;

unsigned long lastFinanceUpdate = 0;
const unsigned long FINANCE_INTERVAL = 300000UL; // 5 menit

static SholatData sholatData;
static unsigned long lastSholatUpdate = 0;

static constexpr unsigned long SHOLAT_INTERVAL =
    300000UL; // 5 menit

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
          currentTemplate = Template::FINANCE;
          break;

        case Template::FINANCE:
          currentTemplate = Template::JADWAL_SHOLAT;
          break;

        case Template::JADWAL_SHOLAT:
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
  case Template::FINANCE:
    drawFinanceTemplate(
        getDisplay(),
        financeData,
        financePage);
    break;
  case Template::JADWAL_SHOLAT:
    drawSholatTemplate(
        getDisplay(),
        sholatData);
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

  const unsigned long now = millis();

  // Update cuaca hanya di luar animasi FACE.
  if (currentTemplate != Template::IDLE_FACE &&
      WiFi.status() == WL_CONNECTED &&
      now - lastWeatherUpdate >= WEATHER_INTERVAL)
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

  // Update finance saat template FINANCE aktif.
  // Fetch pertama dilakukan langsung, fetch berikutnya tiap 5 menit.
  if (currentTemplate == Template::FINANCE &&
      WiFi.status() == WL_CONNECTED &&
      (lastFinanceUpdate == 0 ||
       now - lastFinanceUpdate >= FINANCE_INTERVAL))
  {
    Serial.println("Starting finance fetch...");

    if (fetchFinance(financeData))
    {
      lastFinanceUpdate = millis();
      Serial.println("Finance fetch success");
    }
    else
    {
      // Coba kembali dalam 60 detik.
      lastFinanceUpdate =
          millis() - FINANCE_INTERVAL + 60000UL;

      Serial.println("Finance fetch failed");
    }
  }

  if (currentTemplate == Template::IDLE_FACE)
  {
    // Jangan melakukan HTTP request saat animasi berjalan.
    updateCatFace();
  }
  else if (millis() - lastDisplayUpdate >= 1000)
  {
    lastDisplayUpdate = millis();
    renderTemplate();
  }


  if (currentTemplate == Template::JADWAL_SHOLAT &&
    WiFi.status() == WL_CONNECTED &&
    (lastSholatUpdate == 0 ||
     millis() - lastSholatUpdate >= SHOLAT_INTERVAL))
{
    if (fetchSholat(sholatData))
    {
        lastSholatUpdate = millis();
    }
    else
    {
        // Coba lagi dalam 60 detik.
        lastSholatUpdate =
            millis() - SHOLAT_INTERVAL + 60000UL;
    }
}

  delay(1);
}
