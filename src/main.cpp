#include <Arduino.h>

#include "BleDebug.h"
#include "Config.h"
#include "Display.h"
#include "Weather.h"
#include "CatFace.h"
#include "Finance.h"
#include "JadwalSholat.h"
#include "TemplateController.h"
#include "WifiConnection.h"

static WeatherData weather;
static unsigned long lastDisplayUpdate = 0;

static FinanceData financeData;
static uint8_t financePage = 0;

static SholatData sholatData;
static WifiScanner wifiScanner;

static TemplateController templateController(
    weather,
    financeData,
    financePage,
    sholatData);

void setup()
{
  Serial.begin(115200);

  pinMode(BUTTON_PIN, INPUT_PULLUP);

  initDisplay();
  drawMessage("Connecting WiFi...");

  initCatFace(getDisplay());

  connectWifi();

  initBleDebug();

  templateController.render();
}

void loop()
{
  templateController.handleButton();
  templateController.updateData();

  if (templateController.current() == Template::IDLE_FACE)
  {
    updateCatFace();
  }
  else if (millis() - lastDisplayUpdate >= 1000)
  {
    lastDisplayUpdate = millis();
    templateController.render();
  }

  updateBleDebug(wifiScanner);

  delay(1);
}
