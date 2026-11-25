#pragma once

#include <Arduino.h>

#include "Finance.h"
#include "JadwalSholat.h"
#include "Weather.h"

enum class Template
{
  IDLE_FACE,
  CLOCK,
  WEATHER,
  FINANCE,
  JADWAL_SHOLAT
};

class TemplateController
{
public:
  TemplateController(
      WeatherData &weather,
      FinanceData &finance,
      uint8_t &financePage,
      SholatData &sholat);

  void handleButton();
  void updateData();
  void render();
  Template current() const;
  void setTemplateByName(const String& templateName);

private:
  WeatherData &weather_;
  FinanceData &finance_;
  uint8_t &financePage_;
  SholatData &sholat_;

  Template currentTemplate_ = Template::IDLE_FACE;
  bool lastReading_ = HIGH;
  bool stableButtonState_ = HIGH;
  unsigned long lastDebounceTime_ = 0;
  unsigned long lastWeatherUpdate_ = 0;
  unsigned long lastFinanceUpdate_ = 0;
  unsigned long lastSholatUpdate_ = 0;
};
