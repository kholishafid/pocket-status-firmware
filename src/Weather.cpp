#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

#include "Config.h"
#include "Weather.h"

bool fetchWeather(WeatherData& data) {
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("Weather: Wi-Fi disconnected");
        return false;
    }

    WiFiClientSecure client;
    client.setInsecure();  // Prototype only

    HTTPClient http;
    http.setConnectTimeout(8000);
    http.setTimeout(8000);

    String url =
        "https://api.open-meteo.com/v1/forecast?latitude=" +
        String(LATITUDE, 4) +
        "&longitude=" + String(LONGITUDE, 4) +
        "&current=temperature_2m,weather_code"
        "&daily=temperature_2m_max,temperature_2m_min"
        "&forecast_days=1&timezone=Asia%2FJakarta";

    Serial.println("Fetching weather...");

    if (!http.begin(client, url)) {
        Serial.println("Weather: HTTP begin failed");
        return false;
    }

    http.addHeader("Accept", "application/json");

    int status = http.GET();

    if (status != HTTP_CODE_OK) {
        // Negative codes are connection/TLS/timeout errors (see HTTPClient.h)
        Serial.printf("Weather HTTP error: %d (%s)\n",
                      status, HTTPClient::errorToString(status).c_str());
        http.end();
        return false;
    }

    // IMPORTANT: Open-Meteo replies with "Transfer-Encoding: chunked".
    // http.getStream() returns the RAW stream, so the chunk-size lines get
    // mixed into the JSON and deserializeJson() fails (InvalidInput).
    // getString() decodes chunked responses correctly.
    String payload = http.getString();
    http.end();

    if (payload.isEmpty()) {
        Serial.println("Weather: empty response");
        return false;
    }

    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, payload);

    if (error) {
        Serial.printf("Weather JSON error: %s\n", error.c_str());
        Serial.println(payload);
        return false;
    }

    JsonObject current = doc["current"];
    JsonObject daily = doc["daily"];

    if (current.isNull() || daily.isNull()) {
        Serial.println("Weather: unexpected response:");
        Serial.println(payload);
        return false;
    }

    data.temperature    = current["temperature_2m"] | 0.0f;
    data.weatherCode    = current["weather_code"] | 0;
    data.maxTemperature = daily["temperature_2m_max"][0] | 0.0f;
    data.minTemperature = daily["temperature_2m_min"][0] | 0.0f;

    data.valid = true;

    Serial.printf("Weather updated: %.1f C, code %d\n",
                  data.temperature, data.weatherCode);
    return true;
}

const char* weatherDescription(int code) {
    if (code == 0) return "Cerah";
    if (code <= 3) return "Berawan";
    if (code <= 48) return "Berkabut";
    if (code <= 57) return "Gerimis";
    if (code <= 67) return "Hujan";
    if (code <= 77) return "Salju";
    if (code <= 82) return "Hujan";
    if (code <= 86) return "Salju";
    if (code <= 99) return "Badai";
    return "Tidak diketahui";
}
