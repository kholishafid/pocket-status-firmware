#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

#include "Finance.h"

static const char* FINANCE_URL =
    "https://pocket-status-api.iwanfaiber1.workers.dev/api/finance";

// =====================================================
// FETCH DATA
// =====================================================

bool fetchFinance(FinanceData& data) {
    if (WiFi.status() != WL_CONNECTED) {
        Serial.printf(
            "Finance: Wi-Fi unavailable (status=%d, RSSI=%d dBm)\n",
            WiFi.status(),
            WiFi.RSSI()
        );
        return false;
    }

    const String localIp = WiFi.localIP().toString();
    const String gatewayIp = WiFi.gatewayIP().toString();
    const String dnsIp = WiFi.dnsIP().toString();

    Serial.printf(
        "Finance: Wi-Fi connected, RSSI=%d dBm, IP=%s, gateway=%s, DNS=%s\n",
        WiFi.RSSI(),
        localIp.c_str(),
        gatewayIp.c_str(),
        dnsIp.c_str()
    );
    WiFiClientSecure client;
    client.setInsecure(); // Prototype only

    HTTPClient http;
    http.setConnectTimeout(8000);
    http.setTimeout(8000);

    Serial.printf("Finance: connecting to %s\n", FINANCE_URL);

    if (!http.begin(client, FINANCE_URL)) {
        Serial.println("Finance: HTTP begin failed");
        return false;
    }

    http.addHeader("Accept", "application/json");

    const int status = http.GET();

    if (status != HTTP_CODE_OK) {
        Serial.printf(
            "Finance HTTP error: %d (%s)\n",
            status,
            HTTPClient::errorToString(status).c_str()
        );

        if (status == HTTPC_ERROR_CONNECTION_REFUSED) {
            Serial.println(
                "Finance: TCP/TLS connection refused. "
                "Check DNS, internet access, API host, and port 443."
            );
        } else if (status < 0) {
            Serial.println(
                "Finance: request failed before receiving an HTTP response."
            );
        } else {
            Serial.printf(
                "Finance: server returned HTTP status %d.\n",
                status
            );
        }

        http.end();
        return false;
    }

    // getString() menangani respons HTTP chunked.
    String payload = http.getString();
    http.end();

    if (payload.isEmpty()) {
        Serial.println("Finance: empty response");
        return false;
    }

    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, payload);

    if (error) {
        Serial.printf(
            "Finance JSON error: %s\n",
            error.c_str()
        );
        return false;
    }

    if (doc["status"] != "success") {
        Serial.println("Finance: API status is not success");
        return false;
    }

    JsonObject dataJson = doc["data"];

    if (dataJson.isNull()) {
        Serial.println("Finance: missing data object");
        return false;
    }

    // Gunakan objek sementara agar data lama tidak rusak
    // jika parsing gagal atau field wajib tidak tersedia.
    FinanceData result;

    result.ihsgHarga =
        dataJson["ihsg_harga"].as<String>().toInt();

    result.ihsgPersen =
        dataJson["ihsg_persen"].as<String>().toFloat();

    result.emasDuniaIdr =
        dataJson["emas_dunia_idr"].as<String>().toInt();

    result.kursUsdIdr =
        dataJson["kurs_usd_idr"].as<String>().toInt();

    result.btcIdr =
        dataJson["btc_idr"].as<String>().toInt();

    result.btcPersen =
        dataJson["btc_persen"].as<String>().toFloat();

    result.ethIdr =
        dataJson["eth_idr"].as<String>().toInt();

    result.ethPersen =
        dataJson["eth_persen"].as<String>().toFloat();

    // Pastikan field wajib tersedia sebelum menerima data.
    if (dataJson["ihsg_harga"].isNull() ||
        dataJson["emas_dunia_idr"].isNull() ||
        dataJson["kurs_usd_idr"].isNull() ||
        dataJson["btc_idr"].isNull() ||
        dataJson["eth_idr"].isNull()) {
        Serial.println("Finance: required field missing");
        return false;
    }

    result.valid = true;
    data = result;

    Serial.printf(
        "Finance updated: IHSG %d, BTC %lld\n",
        data.ihsgHarga,
        (long long)data.btcIdr
    );

    return true;
}

// =====================================================
// DISPLAY HELPERS
// =====================================================

// Format angka menjadi 1.367.656.293
static String formatRupiah(int64_t value) {
    String input = String((long long)value);
    String output;

    int length = input.length();

    for (int i = 0; i < length; i++) {
        output += input[i];

        int remaining = length - i - 1;

        if (remaining > 0 && remaining % 3 == 0) {
            output += '.';
        }
    }

    return output;
}

static void drawRight(
    U8G2& oled,
    const char* text,
    int right,
    int baseline
) {
    oled.drawStr(
        right - oled.getStrWidth(text),
        baseline,
        text
    );
}

// =====================================================
// DRAW FINANCE TEMPLATE
// =====================================================

void drawFinanceTemplate(
    U8G2& oled,
    const FinanceData& data,
    uint8_t page
) {
    oled.clearBuffer();

    if (!data.valid) {
        oled.setFont(u8g2_font_6x10_tf);
        oled.drawStr(10, 34, "No Wi-Fi / Internet");
        oled.drawStr(0, 62, "Configure via BLE");
        oled.sendBuffer();
        return;
    }

    if (page == 0) {
        // MARKET PAGE
        oled.setFont(u8g2_font_6x10_tf);
        oled.drawStr(0, 9, "MARKET");
        oled.drawStr(0, 22, "IHSG");

        char ihsg[20];
        snprintf(
            ihsg,
            sizeof(ihsg),
            "%d",
            data.ihsgHarga
        );

        oled.setFont(u8g2_font_7x14B_tf);
        drawRight(oled, ihsg, 56, 38);

        oled.setFont(u8g2_font_6x10_tf);
        char change[16];
        snprintf(
            change,
            sizeof(change),
            "%+.2f%%",
            data.ihsgPersen
        );
        drawRight(oled, change, 56, 51);

        oled.drawStr(68, 22, "GOLD / IDR");
        String gold = formatRupiah(data.emasDuniaIdr);

        oled.setFont(u8g2_font_5x8_tf);
        drawRight(oled, gold.c_str(), 127, 34);

        oled.setFont(u8g2_font_6x10_tf);
        oled.drawStr(68, 47, "USD / IDR");
        String usd = formatRupiah(data.kursUsdIdr);

        oled.setFont(u8g2_font_5x8_tf);
        drawRight(oled, usd.c_str(), 127, 59);

    } else {
        // CRYPTO PAGE
        oled.setFont(u8g2_font_6x10_tf);
        oled.drawStr(0, 9, "CRYPTO / IDR");

        oled.drawStr(0, 22, "BTC");

        String btc = formatRupiah(data.btcIdr);
        oled.setFont(u8g2_font_6x10_tf);
        drawRight(oled, btc.c_str(), 127, 35);

        char btcChange[16];
        snprintf(
            btcChange,
            sizeof(btcChange),
            "%+.2f%%",
            data.btcPersen
        );
        drawRight(oled, btcChange, 127, 46);

        oled.setFont(u8g2_font_6x10_tf);
        oled.drawStr(0, 61, "ETH");
        String eth = formatRupiah(data.ethIdr);
        oled.setFont(u8g2_font_5x8_tf);
        oled.drawStr(25, 61, eth.c_str());

        char ethChange[16];
        snprintf(
            ethChange,
            sizeof(ethChange),
            "%+.2f%%",
            data.ethPersen
        );
        drawRight(oled, ethChange, 127, 61);
    }

    // Indikator halaman
    oled.drawDisc(page == 0 ? 116 : 122, 16, 2);
    oled.drawCircle(page == 0 ? 122 : 116, 16, 2);

    oled.sendBuffer();
}
