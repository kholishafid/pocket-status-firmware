#include <Arduino.h>
#include <Adafruit_BME280.h>
#include <Wire.h>
#include <math.h>

#include "Config.h"
#include "Environment.h"

namespace
{
constexpr unsigned long READ_INTERVAL_MS = 2000UL;
constexpr unsigned long HISTORY_INTERVAL_MS = 10UL * 60UL * 1000UL; // 10 menit
constexpr uint8_t HISTORY_SIZE = 19;     // 0..180 menit
constexpr uint8_t TREND_MIN_SAMPLES = 4; // minimal ±30 menit data
constexpr unsigned long PAGE_INTERVAL_MS = 4000UL;
constexpr uint8_t PAGE_COUNT = 5;

Adafruit_BME280 bme;
EnvData env;

bool sensorReady = false;
bool firstRead = true;
bool statsInit = false;
unsigned long lastRead = 0;
unsigned long lastHistory = 0;

float history[HISTORY_SIZE];
uint8_t historyCount = 0;

// ---------------------------------------------------------------------------
// PERHITUNGAN
// ---------------------------------------------------------------------------

// Titik embun (rumus Magnus).
float calcDewPoint(float tempC, float rh)
{
  constexpr float b = 17.62f;
  constexpr float c = 243.12f;
  const float clampedRh = constrain(rh, 1.0f, 100.0f);
  const float gamma = logf(clampedRh / 100.0f) + (b * tempC) / (c + tempC);
  return (c * gamma) / (b - gamma);
}

// Heat index (regresi Rothfusz, NWS). Hanya bermakna di udara hangat.
float calcHeatIndexC(float tempC, float rh)
{
  const float t = tempC * 9.0f / 5.0f + 32.0f; // ke Fahrenheit
  float hi = 0.5f * (t + 61.0f + ((t - 68.0f) * 1.2f) + (rh * 0.094f));

  if (hi >= 80.0f)
  {
    hi = -42.379f + 2.04901523f * t + 10.14333127f * rh -
         0.22475541f * t * rh - 0.00683783f * t * t -
         0.05481717f * rh * rh + 0.00122874f * t * t * rh +
         0.00085282f * t * rh * rh - 0.00000199f * t * t * rh * rh;

    if (rh < 13.0f && t >= 80.0f && t <= 112.0f)
    {
      hi -= ((13.0f - rh) / 4.0f) * sqrtf((17.0f - fabsf(t - 95.0f)) / 17.0f);
    }
    else if (rh > 85.0f && t >= 80.0f && t <= 87.0f)
    {
      hi += ((rh - 85.0f) / 10.0f) * ((87.0f - t) / 5.0f);
    }
  }

  return (hi - 32.0f) * 5.0f / 9.0f; // kembali ke Celsius
}

// Label kenyamanan berdasarkan titik embun.
const char *comfortLabel(float dewPointC)
{
  if (dewPointC < 10.0f) return "Kering";
  if (dewPointC < 16.0f) return "Nyaman";
  if (dewPointC < 18.0f) return "Agak lembap";
  if (dewPointC < 21.0f) return "Lembap";
  if (dewPointC < 24.0f) return "Gerah";
  return "Sangat gerah";
}

// Estimasi ketinggian dari tekanan (atmosfer standar).
float pressureAltitude(float hPa)
{
  return 44330.0f * (1.0f - powf(hPa / 1013.25f, 0.1903f));
}

// Koreksi tekanan ke permukaan laut, berdasarkan ketinggian lokasi.
float toSeaLevel(float hPa)
{
  return hPa / powf(1.0f - STATION_ALTITUDE_M / 44330.0f, 5.255f);
}

void pushHistory(float pressure)
{
  if (historyCount < HISTORY_SIZE)
  {
    history[historyCount++] = pressure;
    return;
  }
  memmove(history, history + 1, (HISTORY_SIZE - 1) * sizeof(float));
  history[HISTORY_SIZE - 1] = pressure;
}

void updateTrend()
{
  if (historyCount < TREND_MIN_SAMPLES)
  {
    env.trendReady = false;
    return;
  }

  const float delta = history[historyCount - 1] - history[0];
  const float spanMinutes =
      (historyCount - 1) * (HISTORY_INTERVAL_MS / 60000.0f);

  env.pressureTrend3h = delta / spanMinutes * 180.0f;
  env.trendReady = true;

  // Indikasi kasar. Di daerah tropis tekanan punya pasang-surut harian
  // (±1-2 hPa), jadi ambang dibuat agak longgar.
  const float t = env.pressureTrend3h;
  if (t <= -3.5f)
    env.forecast = "Hujan / badai?";
  else if (t <= -1.5f)
    env.forecast = "Mendung / hujan";
  else if (t >= 3.5f)
    env.forecast = "Cepat cerah";
  else if (t >= 1.5f)
    env.forecast = "Cuaca membaik";
  else if (env.seaLevelPressure >= 1015.0f)
    env.forecast = "Cerah, stabil";
  else if (env.seaLevelPressure >= 1008.0f)
    env.forecast = "Cuaca tetap";
  else
    env.forecast = "Berawan, stabil";
}

// ---------------------------------------------------------------------------
// GAMBAR
// ---------------------------------------------------------------------------

void drawHeader(U8G2 &d, const char *title, uint8_t page)
{
  d.setFont(u8g2_font_6x10_tf);
  d.drawStr(0, 9, title);

  for (uint8_t i = 0; i < PAGE_COUNT; i++)
  {
    const int x = d.getDisplayWidth() - (PAGE_COUNT - i) * 7 + 3;
    if (i == page)
      d.drawDisc(x, 5, 2);
    else
      d.drawCircle(x, 5, 1);
  }
  d.drawHLine(0, 12, d.getDisplayWidth());
}

void drawRow(U8G2 &d, int y, const char *label, const char *value)
{
  d.setFont(u8g2_font_6x10_tf);
  d.drawStr(0, y, label);
  d.drawStr(d.getDisplayWidth() - d.getStrWidth(value), y, value);
}

void drawCentered(U8G2 &d, int y, const char *text)
{
  d.drawStr((d.getDisplayWidth() - d.getStrWidth(text)) / 2, y, text);
}

// Halaman 0: suhu + kelembapan besar, tekanan di bawah.
void drawLivePage(U8G2 &d, const EnvData &e)
{
  char buf[20];

  d.setFont(u8g2_font_logisoso24_tf);
  snprintf(buf, sizeof(buf), "%.1f", e.temperature);
  d.drawStr(0, 42, buf);
  const int tempWidth = d.getStrWidth(buf);

  snprintf(buf, sizeof(buf), "%.0f", e.humidity);
  const int humWidth = d.getStrWidth(buf);
  d.drawStr(d.getDisplayWidth() - 12 - humWidth, 42, buf);

  d.setFont(u8g2_font_6x10_tf);
  d.drawStr(tempWidth + 2, 26, "C");
  d.drawStr(d.getDisplayWidth() - 8, 26, "%");

  d.setFont(u8g2_font_5x8_tf);
  d.drawStr(0, 52, "SUHU");
  d.drawStr(d.getDisplayWidth() - 50, 52, "KELEMBAPAN");

  d.setFont(u8g2_font_6x10_tf);
  snprintf(buf, sizeof(buf), "%.1f hPa", e.pressure);
  drawCentered(d, 63, buf);
}

// Halaman 1: tekanan, tekanan permukaan laut, ketinggian.
void drawPressurePage(U8G2 &d, const EnvData &e)
{
  char buf[28];

  d.setFont(u8g2_font_logisoso24_tf);
  snprintf(buf, sizeof(buf), "%.1f", e.pressure);
  d.drawStr(0, 40, buf);
  const int w = d.getStrWidth(buf);

  d.setFont(u8g2_font_6x10_tf);
  d.drawStr(w + 4, 40, "hPa");

  d.setFont(u8g2_font_5x8_tf);
  snprintf(buf, sizeof(buf), "Permukaan laut: %.1f hPa", e.seaLevelPressure);
  d.drawStr(0, 52, buf);

  snprintf(buf, sizeof(buf), "Perkiraan alt: %.0f m", e.altitude);
  d.drawStr(0, 63, buf);
}

// Halaman 2: tren tekanan 3 jam + indikasi cuaca.
void drawTrendPage(U8G2 &d, const EnvData &e)
{
  d.setFont(u8g2_font_6x10_tf);

  if (!e.trendReady)
  {
    drawCentered(d, 34, "Mengumpulkan data...");
    d.setFont(u8g2_font_5x8_tf);
    drawCentered(d, 48, "Tampil setelah 30 menit");
    return;
  }

  const float t = e.pressureTrend3h;
  const int cx = 20;
  const int cy = 34;

  if (t >= 1.5f)
  {
    d.drawTriangle(cx, cy - 11, cx - 10, cy + 7, cx + 10, cy + 7); // naik
  }
  else if (t <= -1.5f)
  {
    d.drawTriangle(cx, cy + 11, cx - 10, cy - 7, cx + 10, cy - 7); // turun
  }
  else
  {
    d.drawBox(cx - 10, cy - 2, 13, 4); // stabil
    d.drawTriangle(cx + 11, cy, cx + 3, cy - 7, cx + 3, cy + 7);
  }

  char buf[16];
  d.setFont(u8g2_font_logisoso16_tf);
  snprintf(buf, sizeof(buf), "%+.1f", t);
  d.drawStr(42, 40, buf);

  d.setFont(u8g2_font_6x10_tf);
  d.drawStr(d.getDisplayWidth() - 36, 40, "hPa/3j");

  drawCentered(d, 62, e.forecast);
}

// Halaman 3: titik embun, heat index, kenyamanan.
void drawComfortPage(U8G2 &d, const EnvData &e)
{
  char buf[20];

  snprintf(buf, sizeof(buf), "%.1f C", e.dewPoint);
  drawRow(d, 26, "Titik embun", buf);

  snprintf(buf, sizeof(buf), "%.1f C", e.heatIndex);
  drawRow(d, 38, "Terasa", buf);

  snprintf(buf, sizeof(buf), "%.0f %%", e.humidity);
  drawRow(d, 50, "Kelembapan", buf);

  d.setFont(u8g2_font_6x10_tf);
  drawCentered(d, 63, e.comfort);
}

// Halaman 4: nilai terendah / tertinggi sejak ESP menyala.
void drawMinMaxPage(U8G2 &d, const EnvData &e)
{
  char buf[24];

  snprintf(buf, sizeof(buf), "%.1f~%.1f C", e.minTemp, e.maxTemp);
  drawRow(d, 26, "Suhu", buf);

  snprintf(buf, sizeof(buf), "%.0f~%.0f %%", e.minHum, e.maxHum);
  drawRow(d, 38, "Lembap", buf);

  snprintf(buf, sizeof(buf), "%.1f~%.1f", e.minPressure, e.maxPressure);
  drawRow(d, 50, "Tekanan", buf);

  d.setFont(u8g2_font_5x8_tf);
  drawCentered(d, 63, "sejak ESP menyala");
}
} // namespace

// ---------------------------------------------------------------------------
// API PUBLIK
// ---------------------------------------------------------------------------

bool initEnvironment()
{
  sensorReady = bme.begin(ENV_SENSOR_ADDR, &Wire);

  // Modul BME280 biasanya 0x76 (SDO ke GND) atau 0x77 (SDO ke VCC).
  if (!sensorReady)
  {
    const uint8_t alt = (ENV_SENSOR_ADDR == 0x76) ? 0x77 : 0x76;
    sensorReady = bme.begin(alt, &Wire);
  }

  if (!sensorReady)
  {
    Serial.println("BME280 tidak ditemukan. Cek kabel SDA/SCL dan alamat I2C.");
    return false;
  }

  // Setelan akurasi tinggi: oversampling tekanan x16, filter x16.
  bme.setSampling(
      Adafruit_BME280::MODE_NORMAL,
      Adafruit_BME280::SAMPLING_X2,  // suhu
      Adafruit_BME280::SAMPLING_X16, // tekanan
      Adafruit_BME280::SAMPLING_X2,  // kelembapan
      Adafruit_BME280::FILTER_X16,
      Adafruit_BME280::STANDBY_MS_500);

  delay(200); // beri waktu pengukuran pertama selesai
  Serial.println("BME280 siap.");
  return true;
}

void updateEnvironment()
{
  if (!sensorReady)
  {
    return;
  }

  const unsigned long now = millis();
  if (!firstRead && now - lastRead < READ_INTERVAL_MS)
  {
    return;
  }
  firstRead = false;
  lastRead = now;

  const float t = bme.readTemperature();
  const float h = bme.readHumidity();
  const float p = bme.readPressure() / 100.0f;

  if (isnan(t) || isnan(h) || isnan(p) || p < 300.0f || p > 1100.0f)
  {
    return; // pembacaan buruk, pakai data terakhir
  }

  env.temperature = t;
  env.humidity = constrain(h, 0.0f, 100.0f);
  env.pressure = p;
  env.seaLevelPressure = toSeaLevel(p);
  env.altitude = pressureAltitude(p);
  env.dewPoint = calcDewPoint(t, env.humidity);
  env.heatIndex = calcHeatIndexC(t, env.humidity);
  env.comfort = comfortLabel(env.dewPoint);

  if (!statsInit)
  {
    env.minTemp = env.maxTemp = t;
    env.minHum = env.maxHum = env.humidity;
    env.minPressure = env.maxPressure = p;
    statsInit = true;
  }
  else
  {
    env.minTemp = min(env.minTemp, t);
    env.maxTemp = max(env.maxTemp, t);
    env.minHum = min(env.minHum, env.humidity);
    env.maxHum = max(env.maxHum, env.humidity);
    env.minPressure = min(env.minPressure, p);
    env.maxPressure = max(env.maxPressure, p);
  }

  env.valid = true;

  if (historyCount == 0 || now - lastHistory >= HISTORY_INTERVAL_MS)
  {
    pushHistory(p);
    lastHistory = now;
  }
  updateTrend();
}

const EnvData &getEnvData()
{
  return env;
}

void drawEnvTemplate(U8G2 &display, const EnvData &e)
{
  display.clearBuffer();

  if (!e.valid)
  {
    display.setFont(u8g2_font_6x10_tf);
    display.drawStr(0, 24, "Sensor BME280");
    display.drawStr(0, 38, "belum terbaca");
    display.setFont(u8g2_font_5x8_tf);
    display.drawStr(0, 54, "Cek kabel SDA/SCL");
    display.sendBuffer();
    return;
  }

  const uint8_t page = (millis() / PAGE_INTERVAL_MS) % PAGE_COUNT;

  switch (page)
  {
  case 0:
    drawHeader(display, "LINGKUNGAN", page);
    drawLivePage(display, e);
    break;
  case 1:
    drawHeader(display, "TEKANAN", page);
    drawPressurePage(display, e);
    break;
  case 2:
    drawHeader(display, "TREN 3 JAM", page);
    drawTrendPage(display, e);
    break;
  case 3:
    drawHeader(display, "KENYAMANAN", page);
    drawComfortPage(display, e);
    break;
  default:
    drawHeader(display, "MIN / MAX", page);
    drawMinMaxPage(display, e);
    break;
  }

  display.sendBuffer();
}