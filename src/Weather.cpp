
#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

#include "Config.h"
#include "Weather.h"

namespace {
    constexpr char WEATHER_API[] =
        "https://pocket-status-api.iwanfaiber1.workers.dev/api/cuaca";
}

bool fetchWeather(WeatherData& data) {
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("Weather: Wi-Fi disconnected");
        return false;
    }

    WiFiClientSecure client;

    // Prototype only. Use certificate verification in production.
    client.setInsecure();

    HTTPClient http;
    http.setConnectTimeout(8000);
    http.setTimeout(8000);

    Serial.println("Fetching PocketStatus weather...");

    if (!http.begin(client, WEATHER_API)) {
        Serial.println("Weather: HTTP begin failed");
        return false;
    }

    http.addHeader("Accept", "application/json");

    const int status = http.GET();

    if (status != HTTP_CODE_OK) {
        Serial.printf(
            "Weather HTTP error: %d (%s)\n",
            status,
            HTTPClient::errorToString(status).c_str()
        );

        http.end();
        return false;
    }

    // getString() handles the HTTP response body.
    String payload = http.getString();
    http.end();

    if (payload.isEmpty()) {
        Serial.println("Weather: empty response");
        return false;
    }

    JsonDocument doc;
    const DeserializationError error =
        deserializeJson(doc, payload);

    if (error) {
        Serial.printf(
            "Weather JSON error: %s\n",
            error.c_str()
        );
        Serial.println(payload);
        return false;
    }

    if (doc["status"] != "success" ||
        doc["data"].isNull()) {
        Serial.println("Weather: unexpected API response");
        Serial.println(payload);
        return false;
    }

    JsonObject weather = doc["data"];

    // Map fields from the PocketStatus API.
    data.temperature =
        weather["suhu"] | 0.0f;

    data.weatherCode =
        weather["kode_cuaca"] | -1;

    data.humidity =
        weather["kelembapan"] | 0;

    data.windSpeed =
        weather["kecepatan_angin"] | 0.0f;

    data.airQuality =
        weather["aqi_iqair"] | 0;

    strncpy(
        data.bmkgLocation,
        weather["lokasi_bmkg"] | "",
        sizeof(data.bmkgLocation) - 1);
    data.bmkgLocation[sizeof(data.bmkgLocation) - 1] = '\0';

    strncpy(
        data.airQualityLocation,
        weather["lokasi_iqair"] | "",
        sizeof(data.airQualityLocation) - 1);
    data.airQualityLocation[sizeof(data.airQualityLocation) - 1] = '\0';

    strncpy(
        data.description,
        weather["deskripsi_cuaca"] | weatherDescription(data.weatherCode),
        sizeof(data.description) - 1);
    data.description[sizeof(data.description) - 1] = '\0';

    strncpy(
        data.windDirection,
        weather["arah_angin"] | "-",
        sizeof(data.windDirection) - 1);
    data.windDirection[sizeof(data.windDirection) - 1] = '\0';

    // This API does not provide daily min/max temperatures.
    // Keep the existing fields populated for compatibility.
    data.maxTemperature = data.temperature;
    data.minTemperature = data.temperature;

    data.valid = true;

    Serial.printf(
        "Weather updated: %.1f C, %d%% humidity, AQI %d, code %d, %s\n",
        data.temperature,
        data.humidity,
        data.airQuality,
        data.weatherCode,
        data.description
    );

    return true;
}

const char* weatherDescription(int code) {
    // Weather codes from the PocketStatus API.
    // Code 2 = Cerah Berawan, based on the provided API response.
    switch (code) {
        case 0:
            return "Cerah";

        case 1:
            return "Cerah";

        case 2:
            return "Cerah Berawan";

        case 3:
            return "Berawan";

        default:
            return "Tidak diketahui";
    }
}
