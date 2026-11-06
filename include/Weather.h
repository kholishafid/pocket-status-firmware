#pragma once

struct WeatherData {
    bool valid = false;
    float temperature = 0;
    float minTemperature = 0;
    float maxTemperature = 0;
    int weatherCode = 0;
};

bool fetchWeather(WeatherData& data);
const char* weatherDescription(int code);
