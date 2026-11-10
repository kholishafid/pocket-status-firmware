#include <Arduino.h>
#include <Wire.h>
#include <U8g2lib.h>

#include "Config.h"
#include "Display.h"

static U8G2_SSD1306_128X64_NONAME_F_HW_I2C oled(
    U8G2_R0,
    U8X8_PIN_NONE);

void initDisplay()
{
    Wire.begin(OLED_SDA, OLED_SCL);
    oled.begin();
}

void drawClock(const char *timeText, const char *dateText)
{
    oled.clearBuffer();
    oled.setFont(u8g2_font_open_iconic_embedded_1x_t);

    int iconWidth = 8;  // Lebar ikon 1x
    int gap = 4;        // Jarak antar ikon (bisa disesuaikan)
    int totalWidth = (iconWidth * 2) + gap; // Total 20 px

    // 2. Hitung posisi X awal agar berada tepat di tengah layar
    int startX = (oled.getDisplayWidth() - totalWidth) / 2;

    // 3. Gambar ikon pertama (Wi-Fi)
    oled.drawGlyph(startX, 10, 80);

    // 4. Gambar ikon kedua (Bluetooth) setelah posisi ikon pertama + gap
    oled.drawGlyph(startX + iconWidth + gap, 10, 74);
    
    oled.setFont(u8g2_font_logisoso24_tf);
    int timeX = (oled.getDisplayWidth() - oled.getStrWidth(timeText)) / 2;
    int timeY = (oled.getDisplayHeight() + 24) / 2; // 24 adalah tinggi font logisoso24_tf
    oled.drawStr(timeX, timeY, timeText);

    oled.setFont(u8g2_font_6x10_tf);
    int dateX = (oled.getDisplayWidth() - oled.getStrWidth(dateText)) / 2;
    oled.drawStr(dateX, 60, dateText);

    oled.sendBuffer();
}

void drawWeather(const WeatherData &weather)
{
    oled.clearBuffer();

    oled.setFont(u8g2_font_6x10_tf);
    char locations[40];
    snprintf(
        locations, sizeof(locations),
        "%.17s / %.17s",
        weather.bmkgLocation,
        weather.airQualityLocation);
    oled.drawStr(0, 9, locations);

    if (!weather.valid)
    {
        oled.drawStr(0, 35, "No Wi-Fi / Internet");
        oled.drawStr(0, 50, "Configure via BLE");
    }
    else
    {
        char temp[20];
        char details[32];
        char wind[32];

        snprintf(
            temp, sizeof(temp),
            "%.1f C", weather.temperature);

        snprintf(
            details, sizeof(details),
            "H:%d%%  AQI:%d",
            weather.humidity,
            weather.airQuality);

        snprintf(
            wind, sizeof(wind),
            "A:%s %.1f",
            weather.windDirection,
            weather.windSpeed);

        // Compact line-art icon keeps the weather screen independent of font glyphs.
        const int iconX = 0;
        const int iconY = 29;
        if (weather.weatherCode <= 1)
        {
            oled.drawDisc(iconX + 10, iconY, 6);
            oled.drawCircle(iconX + 10, iconY, 9);
        }
        else
        {
            oled.drawDisc(iconX + 8, iconY + 2, 5);
            oled.drawDisc(iconX + 15, iconY, 7);
            oled.drawDisc(iconX + 23, iconY + 3, 5);
            oled.drawBox(iconX + 5, iconY + 2, 21, 7);
        }

        oled.setFont(u8g2_font_logisoso24_tf);
        oled.drawStr(32, 41, temp);

        oled.setFont(u8g2_font_6x10_tf);
        int descX = (oled.getDisplayWidth() - oled.getStrWidth(weather.description)) / 2;
        oled.drawStr(descX, 52, weather.description);

        oled.setFont(u8g2_font_5x8_tf);
        oled.drawStr(0, 63, details);
        oled.drawStr(88, 63, wind);
    }

    oled.sendBuffer();
}

void drawMessage(const char *message)
{
    oled.clearBuffer();
    oled.setFont(u8g2_font_6x10_tf);
    oled.drawStr(0, 30, message);
    oled.sendBuffer();
}

U8G2 &getDisplay()
{
    return oled;
}
