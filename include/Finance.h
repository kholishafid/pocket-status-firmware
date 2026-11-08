#pragma once

#include <Arduino.h>
#include <U8g2lib.h>

struct FinanceData {
    int ihsgHarga = 0;
    float ihsgPersen = 0.0f;

    int64_t emasDuniaIdr = 0;
    int kursUsdIdr = 0;

    int64_t btcIdr = 0;
    float btcPersen = 0.0f;

    int64_t ethIdr = 0;
    float ethPersen = 0.0f;

    bool valid = false;
};

bool fetchFinance(FinanceData& data);

void drawFinanceTemplate(
    U8G2& oled,
    const FinanceData& data,
    uint8_t page
);
