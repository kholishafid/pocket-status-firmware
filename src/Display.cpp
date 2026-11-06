#include <Arduino.h>
#include <Wire.h>
#include <U8g2lib.h>

#include "Config.h"
#include "Display.h"

static U8G2_SSD1306_128X64_NONAME_F_HW_I2C oled(
    U8G2_R0,
    U8X8_PIN_NONE
);

void initDisplay() {
    Wire.begin(OLED_SDA, OLED_SCL);
    oled.begin();
}

void drawClock(const char* timeText, const char* dateText) {
    oled.clearBuffer();

    oled.setFont(u8g2_font_6x10_tf);
    oled.drawStr(0, 10, "POCKETSTATUS");

    oled.drawHLine(0, 15, 128);

    oled.setFont(u8g2_font_logisoso24_tf);
    oled.drawStr(0, 44, timeText);

    oled.setFont(u8g2_font_6x10_tf);
    oled.drawStr(0, 60, dateText);

    oled.sendBuffer();
}

void drawWeather(const WeatherData& weather) {
    oled.clearBuffer();

    oled.setFont(u8g2_font_6x10_tf);
    oled.drawStr(0, 10, "SURAKARTA");
    oled.drawHLine(0, 15, 128);

    if (!weather.valid) {
        oled.drawStr(0, 35, "Weather unavailable");
        oled.drawStr(0, 50, "Check WiFi/API");
    } else {
        char temp[20];
        char range[24];

        snprintf(
            temp, sizeof(temp),
            "%.1f C", weather.temperature
        );

        snprintf(
            range, sizeof(range),
            "H:%.0f L:%.0f C",
            weather.maxTemperature,
            weather.minTemperature
        );

        oled.setFont(u8g2_font_logisoso24_tf);
        oled.drawStr(0, 42, temp);

        oled.setFont(u8g2_font_6x10_tf);
        oled.drawStr(0, 53, weatherDescription(weather.weatherCode));
        oled.drawStr(0, 64, range);
    }

    oled.sendBuffer();
}

void drawMessage(const char* message) {
    oled.clearBuffer();
    oled.setFont(u8g2_font_6x10_tf);
    oled.drawStr(0, 30, message);
    oled.sendBuffer();
}


U8G2& getDisplay() {
    return oled;
}
