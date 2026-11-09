#include "BleDebug.h"

#include <ArduinoJson.h>
#include <BLE2902.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <WiFi.h>

namespace
{
constexpr char BLE_DEVICE_NAME[] = "PocketStatus Hafid";
constexpr char BLE_SERVICE_UUID[] = "7b5e0001-4f4f-4f4f-8b5a-000000000001";
constexpr char BLE_COMMAND_UUID[] = "7b5e0002-4f4f-4f4f-8b5a-000000000001";
constexpr char BLE_DATA_UUID[] = "7b5e0003-4f4f-4f4f-8b5a-000000000001";
constexpr char BLE_STATUS_UUID[] = "7b5e0004-4f4f-4f4f-8b5a-000000000001";

constexpr unsigned long BLE_COMMAND_POLL_INTERVAL = 20UL;

enum class PendingCommand
{
  NONE,
  SCAN_WIFI,
  GET_STATUS,
  UNKNOWN
};

BLECharacteristic *commandCharacteristic = nullptr;
BLECharacteristic *dataCharacteristic = nullptr;
BLECharacteristic *statusCharacteristic = nullptr;
volatile PendingCommand pendingCommand = PendingCommand::NONE;
unsigned long lastCommandPoll = 0;
uint32_t scanId = 0;
bool scanSessionActive = false;
int16_t sentNetworkCount = 0;

BLE2902 *notificationsDescriptor(BLECharacteristic *characteristic)
{
  if (characteristic == nullptr)
  {
    return nullptr;
  }

  return static_cast<BLE2902 *>(
      characteristic->getDescriptorByUUID((uint16_t)0x2902));
}

bool notificationsEnabled(BLECharacteristic *characteristic)
{
  BLE2902 *descriptor = notificationsDescriptor(characteristic);
  return descriptor != nullptr && descriptor->getNotifications();
}

bool sendRawData(const String &payload)
{
  if (dataCharacteristic == nullptr || !notificationsEnabled(dataCharacteristic))
  {
    return false;
  }

  const uint16_t mtu = BLEDevice::getMTU();
  const size_t maxPayload = mtu > 3 ? mtu - 3 : 0;
  if (payload.length() > maxPayload)
  {
    return false;
  }

  dataCharacteristic->setValue(payload.c_str());
  dataCharacteristic->notify();
  return true;
}

bool sendMtuError(size_t required, size_t available)
{
  JsonDocument document;
  document["event"] = "error";
  document["code"] = "mtu_too_small";
  document["required"] = required;
  document["available"] = available;

  String payload;
  serializeJson(document, payload);
  if (sendRawData(payload))
  {
    return false;
  }

  return sendRawData("{\"event\":\"error\"}");
}

bool sendEvent(JsonDocument &document)
{
  String payload;
  serializeJson(document, payload);

  const uint16_t mtu = BLEDevice::getMTU();
  const size_t maxPayload = mtu > 3 ? mtu - 3 : 0;
  if (payload.length() > maxPayload)
  {
    sendMtuError(payload.length(), maxPayload);
    return false;
  }

  return sendRawData(payload);
}

void setStatus(const char *state)
{
  if (statusCharacteristic == nullptr)
  {
    return;
  }

  JsonDocument document;
  document["event"] = "status";
  document["state"] = state;
  document["wifi_connected"] = WiFi.status() == WL_CONNECTED;

  String payload;
  serializeJson(document, payload);
  statusCharacteristic->setValue(payload.c_str());

  const uint16_t mtu = BLEDevice::getMTU();
  if (payload.length() <= (mtu > 3 ? mtu - 3 : 0) &&
      notificationsEnabled(statusCharacteristic))
  {
    statusCharacteristic->notify();
  }
}

class CommandCallbacks : public BLECharacteristicCallbacks
{
public:
  void onWrite(BLECharacteristic *characteristic) override
  {
    String command = characteristic->getValue().c_str();
    command.trim();

    if (command == "SCAN_WIFI")
    {
      pendingCommand = PendingCommand::SCAN_WIFI;
    }
    else if (command == "GET_STATUS")
    {
      pendingCommand = PendingCommand::GET_STATUS;
    }
    else
    {
      pendingCommand = PendingCommand::UNKNOWN;
    }
  }
};

void sendError(const char *code, const char *message)
{
  JsonDocument document;
  document["event"] = "error";
  document["code"] = code;
  document["message"] = message;
  sendEvent(document);
}

void sendScanStart()
{
  JsonDocument document;
  document["event"] = "scan_start";
  document["scan_id"] = scanId;
  sendEvent(document);
}

bool sendNetworkResult(const WifiNetwork &network, int16_t index, int16_t total)
{
  JsonDocument document;
  document["event"] = "wifi_result";
  document["scan_id"] = scanId;
  document["index"] = index;
  document["total"] = total;
  document["ssid"] = network.ssid;
  document["rssi"] = network.rssi;
  document["channel"] = network.channel;
  document["security"] = network.security;
  return sendEvent(document);
}

void sendScanComplete(int16_t total)
{
  JsonDocument document;
  document["event"] = "scan_complete";
  document["scan_id"] = scanId;
  document["total"] = total;
  sendEvent(document);
}

void processCommand(WifiScanner &wifiScanner)
{
  PendingCommand command = pendingCommand;
  pendingCommand = PendingCommand::NONE;

  if (command == PendingCommand::NONE)
  {
    return;
  }

  if (command == PendingCommand::GET_STATUS)
  {
    setStatus(scanSessionActive ? "scanning" : "idle");

    JsonDocument document;
    document["event"] = "status";
    document["state"] = scanSessionActive ? "scanning" : "idle";
    document["wifi_connected"] = WiFi.status() == WL_CONNECTED;
    document["scan_id"] = scanId;
    sendEvent(document);
    return;
  }

  if (command == PendingCommand::UNKNOWN)
  {
    sendError("unknown_command", "Use SCAN_WIFI or GET_STATUS");
    return;
  }

  if (!notificationsEnabled(dataCharacteristic))
  {
    setStatus("waiting_for_notifications");
    return;
  }

  if (scanSessionActive || wifiScanner.isScanning())
  {
    sendError("busy", "A Wi-Fi scan is already running");
    return;
  }

  if (!wifiScanner.startScan())
  {
    sendError("scan_start_failed", "Wi-Fi scan could not be started");
    return;
  }

  scanId++;
  scanSessionActive = true;
  sentNetworkCount = 0;
  setStatus("scanning");
  sendScanStart();
}
}

void initBleDebug()
{
  BLEDevice::init(BLE_DEVICE_NAME);

  BLEServer *server = BLEDevice::createServer();
  BLEService *service = server->createService(BLE_SERVICE_UUID);

  commandCharacteristic = service->createCharacteristic(
      BLE_COMMAND_UUID,
      BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_WRITE_NR);
  commandCharacteristic->setCallbacks(new CommandCallbacks());

  dataCharacteristic = service->createCharacteristic(
      BLE_DATA_UUID,
      BLECharacteristic::PROPERTY_NOTIFY);
  dataCharacteristic->addDescriptor(new BLE2902());

  statusCharacteristic = service->createCharacteristic(
      BLE_STATUS_UUID,
      BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY);
  statusCharacteristic->addDescriptor(new BLE2902());

  service->start();

  BLEAdvertising *advertising = BLEDevice::getAdvertising();
  advertising->addServiceUUID(service->getUUID());
  advertising->setScanResponse(true);
  BLEDevice::startAdvertising();

  setStatus("idle");
  Serial.println("BLE debug server started");
}

void updateBleDebug(WifiScanner &wifiScanner)
{
  processCommand(wifiScanner);

  const unsigned long now = millis();
  if (now - lastCommandPoll < BLE_COMMAND_POLL_INTERVAL)
  {
    return;
  }
  lastCommandPoll = now;

  wifiScanner.update();
  if (!scanSessionActive || !wifiScanner.isComplete())
  {
    return;
  }

  if (wifiScanner.hasError())
  {
    sendError("scan_failed", "Wi-Fi scan did not complete");
    scanSessionActive = false;
    setStatus("idle");
    return;
  }

  WifiNetwork network;
  if (wifiScanner.nextNetwork(network))
  {
    if (!sendNetworkResult(
            network,
            sentNetworkCount,
            wifiScanner.resultCount()))
    {
      scanSessionActive = false;
      setStatus("idle");
      return;
    }

    sentNetworkCount++;
    return;
  }

  sendScanComplete(wifiScanner.resultCount());
  scanSessionActive = false;
  setStatus("idle");
}
