#include <Arduino.h>
#include <WiFi.h>

#include "Config.h"
#include "WifiConnection.h"

bool connectWifi()
{
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.setAutoReconnect(true);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  const unsigned long start = millis();

  while (WiFi.status() != WL_CONNECTED &&
         millis() - start < 15000)
  {
    delay(250);
  }

  if (WiFi.status() != WL_CONNECTED)
  {
    Serial.println("Wi-Fi connection failed");
    return false;
  }

  Serial.println("Wi-Fi connected");

  configTime(
      7 * 3600, 0,
      "pool.ntp.org",
      "time.google.com");

  return true;
}
