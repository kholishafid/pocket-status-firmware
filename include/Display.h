#pragma once


#include <U8g2lib.h>
#include "Weather.h"

void initDisplay();

void drawClock(
    const char* timeText,
    const char* dateText
);

void drawWeather(const WeatherData& weather);
void drawMessage(const char* message);

U8G2& getDisplay();
