
#pragma once

#include <Arduino.h>
#include <U8g2lib.h>

struct SholatData {
    String tanggalMasehi;
    String tanggalHijriah;

    String subuh;
    String dzuhur;
    String ashar;
    String maghrib;
    String isya;
    String imsak;

    String jamServer;
    String nextSholat;
    String nextWaktu;

    bool valid = false;
};

bool fetchSholat(SholatData& data);
void drawSholatTemplate(
    U8G2& oled,
    const SholatData& data
);
