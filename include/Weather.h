#pragma once

struct WeatherData {
    bool valid = false;
    float temperature = 0;
    float minTemperature = 0;
    float maxTemperature = 0;
    int weatherCode = 0;
    int humidity = 0;
    float windSpeed = 0;
    int airQuality = 0;
    char bmkgLocation[20] = {};
    char airQualityLocation[20] = {};
    char description[24] = {};
    char windDirection[8] = {};
};

bool fetchWeather(WeatherData& data);
const char* weatherDescription(int code);
