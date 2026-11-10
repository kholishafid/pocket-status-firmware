#include <time.h>
#include <WiFi.h>

#include "Config.h"
#include "Display.h"
#include "TemplateController.h"

TemplateController::TemplateController(
    WeatherData &weather,
    FinanceData &finance,
    uint8_t &financePage,
    SholatData &sholat)
    : weather_(weather),
      finance_(finance),
      financePage_(financePage),
      sholat_(sholat)
{
}

void TemplateController::handleButton()
{
  bool reading = digitalRead(BUTTON_PIN);

  if (reading != lastReading_)
  {
    lastDebounceTime_ = millis();
  }

  if (millis() - lastDebounceTime_ >= DEBOUNCE_MS &&
      reading != stableButtonState_)
  {
    stableButtonState_ = reading;

    if (stableButtonState_ == LOW)
    {
      switch (currentTemplate_)
      {
      case Template::CLOCK:
        currentTemplate_ = Template::WEATHER;
        break;
      case Template::WEATHER:
        currentTemplate_ = Template::FINANCE;
        break;
      case Template::FINANCE:
        currentTemplate_ = Template::JADWAL_SHOLAT;
        break;
      case Template::JADWAL_SHOLAT:
        currentTemplate_ = Template::IDLE_FACE;
        break;
      case Template::IDLE_FACE:
        currentTemplate_ = Template::CLOCK;
        break;
      }

      Serial.println("Template changed");
    }
  }

  lastReading_ = reading;
}

void TemplateController::updateData()
{
  if (WiFi.status() != WL_CONNECTED)
  {
    return;
  }

  const unsigned long now = millis();

  switch (currentTemplate_)
  {
  case Template::WEATHER:
    if (lastWeatherUpdate_ == 0 ||
        now - lastWeatherUpdate_ >= DATA_FETCH_INTERVAL)
    {
      fetchWeather(weather_);
      lastWeatherUpdate_ = millis();
    }
    break;

  case Template::FINANCE:
    if (lastFinanceUpdate_ == 0 ||
        now - lastFinanceUpdate_ >= DATA_FETCH_INTERVAL)
    {
      Serial.println("Starting finance fetch...");
      if (fetchFinance(finance_))
      {
        Serial.println("Finance fetch success");
      }
      else
      {
        Serial.println("Finance fetch failed");
      }
      lastFinanceUpdate_ = millis();
    }
    break;

  case Template::JADWAL_SHOLAT:
    if (lastSholatUpdate_ == 0 ||
        now - lastSholatUpdate_ >= DATA_FETCH_INTERVAL)
    {
      fetchSholat(sholat_);
      lastSholatUpdate_ = millis();
    }
    break;

  case Template::IDLE_FACE:
  case Template::CLOCK:
    break;
  }
}

void TemplateController::render()
{
  switch (currentTemplate_)
  {
  case Template::IDLE_FACE:
    break;
  case Template::CLOCK:
  {
    struct tm timeInfo;
    if (getLocalTime(&timeInfo, 10))
    {
      char timeText[12];
      char dateText[24];

      strftime(timeText, sizeof(timeText), "%H:%M:%S", &timeInfo);
      strftime(dateText, sizeof(dateText), "%a, %d %b", &timeInfo);
      drawClock(timeText, dateText);
    }
    else
    {
      drawMessage("No Wi-Fi / time unavailable");
    }
    break;
  }
  case Template::WEATHER:
    drawWeather(weather_);
    break;
  case Template::FINANCE:
    drawFinanceTemplate(getDisplay(), finance_, financePage_);
    break;
  case Template::JADWAL_SHOLAT:
    drawSholatTemplate(getDisplay(), sholat_);
    break;
  }
}

Template TemplateController::current() const
{
  return currentTemplate_;
}
