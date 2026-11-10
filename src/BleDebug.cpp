#include "BleDebug.h"

#include <ArduinoJson.h>
#include <NimBLEDevice.h>
#include <NimBLEServer.h>
#include <NimBLEUtils.h>
#include <WiFi.h>
#include <esp_bt.h>

namespace
{
constexpr char BLE_DEVICE_NAME[] = "PocketStatus Hafid";
constexpr char BLE_SERVICE_UUID[] = "7b5e0001-4f4f-4f4f-8b5a-000000000001";
constexpr char BLE_COMMAND_UUID[] = "7b5e0002-4f4f-4f4f-8b5a-000000000001";
constexpr char BLE_DATA_UUID[] = "7b5e0003-4f4f-4f4f-8b5a-000000000001";
constexpr char BLE_STATUS_UUID[] = "7b5e0004-4f4f-4f4f-8b5a-000000000001";
constexpr unsigned long BLE_COMMAND_POLL_INTERVAL = 20UL;
constexpr uint16_t BLE_LOCAL_MTU_TARGET = 247;
constexpr uint16_t BLE_MIN_MTU = 23;
constexpr uint16_t BLE_ATT_OVERHEAD = 3;
constexpr size_t MAX_BLE_CLIENTS = 4;

enum class PendingCommand
{
  NONE,
  SCAN_WIFI,
  GET_STATUS,
  UNKNOWN
};

struct PendingRequest
{
  PendingCommand command = PendingCommand::NONE;
  uint16_t connHandle = BLE_HS_CONN_HANDLE_NONE;
};

struct ClientState
{
  uint16_t connHandle = BLE_HS_CONN_HANDLE_NONE;
  uint16_t mtu = BLE_MIN_MTU;
  bool connected = false;
  bool dataSubscribed = false;
  bool statusSubscribed = false;
};

NimBLECharacteristic *commandCharacteristic = nullptr;
NimBLECharacteristic *dataCharacteristic = nullptr;
NimBLECharacteristic *statusCharacteristic = nullptr;
ClientState clients[MAX_BLE_CLIENTS];
PendingRequest pendingRequest;
portMUX_TYPE bleStateMux = portMUX_INITIALIZER_UNLOCKED;
unsigned long lastCommandPoll = 0;
uint32_t scanId = 0;
bool scanSessionActive = false;
uint16_t scanOwnerHandle = BLE_HS_CONN_HANDLE_NONE;
int16_t sentNetworkCount = 0;

int findClientLocked(uint16_t connHandle)
{
  for (size_t index = 0; index < MAX_BLE_CLIENTS; index++)
  {
    if (clients[index].connected &&
        clients[index].connHandle == connHandle)
    {
      return static_cast<int>(index);
    }
  }

  return -1;
}

int findFreeClientLocked()
{
  for (size_t index = 0; index < MAX_BLE_CLIENTS; index++)
  {
    if (!clients[index].connected)
    {
      return static_cast<int>(index);
    }
  }

  return -1;
}

void registerClient(const NimBLEConnInfo &connInfo)
{
  portENTER_CRITICAL(&bleStateMux);
  const int index = findFreeClientLocked();
  if (index >= 0)
  {
    clients[index].connHandle = connInfo.getConnHandle();
    clients[index].mtu = connInfo.getMTU();
    clients[index].connected = true;
    clients[index].dataSubscribed = false;
    clients[index].statusSubscribed = false;
  }
  portEXIT_CRITICAL(&bleStateMux);

  Serial.printf(
      "[BLE] Client connected handle=%u MTU=%u payload_limit=%u\n",
      connInfo.getConnHandle(),
      connInfo.getMTU(),
      connInfo.getMTU() >= BLE_MIN_MTU
          ? connInfo.getMTU() - BLE_ATT_OVERHEAD
          : BLE_MIN_MTU - BLE_ATT_OVERHEAD);
}

void unregisterClient(uint16_t connHandle)
{
  portENTER_CRITICAL(&bleStateMux);
  const int index = findClientLocked(connHandle);
  if (index >= 0)
  {
    clients[index] = ClientState{};
  }

  if (scanOwnerHandle == connHandle)
  {
    scanSessionActive = false;
    scanOwnerHandle = BLE_HS_CONN_HANDLE_NONE;
  }
  portEXIT_CRITICAL(&bleStateMux);
}

void updateMtu(uint16_t connHandle, uint16_t mtu)
{
  portENTER_CRITICAL(&bleStateMux);
  const int index = findClientLocked(connHandle);
  if (index >= 0)
  {
    clients[index].mtu = mtu;
  }
  portEXIT_CRITICAL(&bleStateMux);

  Serial.printf(
      "[BLE] Negotiated MTU: %u handle=%u payload_limit=%u\n",
      mtu,
      connHandle,
      mtu >= BLE_MIN_MTU ? mtu - BLE_ATT_OVERHEAD : BLE_MIN_MTU - BLE_ATT_OVERHEAD);
}

void updateSubscription(
    NimBLECharacteristic *characteristic,
    uint16_t connHandle,
    uint16_t subValue)
{
  portENTER_CRITICAL(&bleStateMux);
  const int index = findClientLocked(connHandle);
  if (index >= 0)
  {
    const bool subscribed = subValue == 1 || subValue == 3;
    if (characteristic == dataCharacteristic)
    {
      clients[index].dataSubscribed = subscribed;
    }
    else if (characteristic == statusCharacteristic)
    {
      clients[index].statusSubscribed = subscribed;
    }
  }
  portEXIT_CRITICAL(&bleStateMux);

  const char *name = characteristic == dataCharacteristic ? "Data" : "Status";
  Serial.printf(
      "[BLE] %s notifications %s handle=%u\n",
      name,
      subValue == 0 ? "unsubscribed" : "subscribed",
      connHandle);
}

bool getClientState(uint16_t connHandle, ClientState &state)
{
  portENTER_CRITICAL(&bleStateMux);
  const int index = findClientLocked(connHandle);
  if (index >= 0)
  {
    state = clients[index];
  }
  portEXIT_CRITICAL(&bleStateMux);

  return index >= 0;
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

  ClientState subscribedClients[MAX_BLE_CLIENTS];
  size_t subscribedCount = 0;
  portENTER_CRITICAL(&bleStateMux);
  for (size_t index = 0; index < MAX_BLE_CLIENTS; index++)
  {
    if (clients[index].connected && clients[index].statusSubscribed)
    {
      subscribedClients[subscribedCount++] = clients[index];
    }
  }
  portEXIT_CRITICAL(&bleStateMux);

  for (size_t index = 0; index < subscribedCount; index++)
  {
    const ClientState &client = subscribedClients[index];
    const uint16_t maxPayload = client.mtu >= BLE_MIN_MTU
                                    ? client.mtu - BLE_ATT_OVERHEAD
                                    : BLE_MIN_MTU - BLE_ATT_OVERHEAD;
    if (payload.length() > maxPayload)
    {
      Serial.printf(
          "[BLE ERROR] Status payload length=%u exceeds max=%u MTU=%u handle=%u\n",
          static_cast<unsigned>(payload.length()),
          maxPayload,
          client.mtu,
          client.connHandle);
      continue;
    }

    const bool submitted = statusCharacteristic->notify(client.connHandle);
    Serial.printf(
        "[BLE TX] event=status length=%u mtu=%u max=%u submitted=%s handle=%u\n",
        static_cast<unsigned>(payload.length()),
        client.mtu,
        maxPayload,
        submitted ? "true" : "false",
        client.connHandle);
  }
}

class ServerCallbacks : public NimBLEServerCallbacks
{
public:
  void onConnect(NimBLEServer *, NimBLEConnInfo &connInfo) override
  {
    registerClient(connInfo);
  }

  void onDisconnect(
      NimBLEServer *,
      NimBLEConnInfo &connInfo,
      int) override
  {
    Serial.printf("[BLE] Client disconnected handle=%u\n", connInfo.getConnHandle());
    unregisterClient(connInfo.getConnHandle());
    NimBLEDevice::startAdvertising();
  }

  void onMTUChange(uint16_t mtu, NimBLEConnInfo &connInfo) override
  {
    updateMtu(connInfo.getConnHandle(), mtu);
  }
};

class CharacteristicCallbacks : public NimBLECharacteristicCallbacks
{
public:
  void onWrite(
      NimBLECharacteristic *characteristic,
      NimBLEConnInfo &connInfo) override
  {
    String command = characteristic->getValue().c_str();
    command.trim();

    PendingCommand pending = PendingCommand::UNKNOWN;
    if (command == "SCAN_WIFI")
    {
      pending = PendingCommand::SCAN_WIFI;
    }
    else if (command == "GET_STATUS")
    {
      pending = PendingCommand::GET_STATUS;
    }

    portENTER_CRITICAL(&bleStateMux);
    pendingRequest.command = pending;
    pendingRequest.connHandle = connInfo.getConnHandle();
    portEXIT_CRITICAL(&bleStateMux);

    Serial.printf(
        "[BLE RX] command=%s handle=%u\n",
        command.c_str(),
        connInfo.getConnHandle());
  }

  void onSubscribe(
      NimBLECharacteristic *characteristic,
      NimBLEConnInfo &connInfo,
      uint16_t subValue) override
  {
    updateSubscription(
        characteristic,
        connInfo.getConnHandle(),
        subValue);
  }
};

PendingRequest takePendingRequest()
{
  PendingRequest request;
  portENTER_CRITICAL(&bleStateMux);
  request = pendingRequest;
  pendingRequest = PendingRequest{};
  portEXIT_CRITICAL(&bleStateMux);
  return request;
}

bool isScanActive()
{
  portENTER_CRITICAL(&bleStateMux);
  const bool active = scanSessionActive;
  portEXIT_CRITICAL(&bleStateMux);
  return active;
}

uint16_t getScanOwner()
{
  portENTER_CRITICAL(&bleStateMux);
  const uint16_t owner = scanOwnerHandle;
  portEXIT_CRITICAL(&bleStateMux);
  return owner;
}

void setScanState(bool active, uint16_t ownerHandle)
{
  portENTER_CRITICAL(&bleStateMux);
  scanSessionActive = active;
  scanOwnerHandle = active ? ownerHandle : BLE_HS_CONN_HANDLE_NONE;
  portEXIT_CRITICAL(&bleStateMux);
}

bool dataNotificationsEnabled(uint16_t connHandle)
{
  ClientState client;
  return getClientState(connHandle, client) &&
         client.connected &&
         client.dataSubscribed;
}

uint16_t notificationLimit(const ClientState &client)
{
  const uint16_t mtu = client.mtu >= BLE_MIN_MTU ? client.mtu : BLE_MIN_MTU;
  return mtu - BLE_ATT_OVERHEAD;
}

bool sendRawData(
    const String &payload,
    uint16_t connHandle,
    const char *eventName)
{
  ClientState client;
  if (dataCharacteristic == nullptr ||
      !getClientState(connHandle, client) ||
      !client.connected ||
      !client.dataSubscribed)
  {
    Serial.printf(
        "[BLE ERROR] Data notification unavailable handle=%u event=%s\n",
        connHandle,
        eventName);
    return false;
  }

  const uint16_t maxPayload = notificationLimit(client);
  if (payload.length() > maxPayload)
  {
    Serial.printf(
        "[BLE ERROR] Payload exceeds notification limit\n"
        "[BLE ERROR] Event: %s\n"
        "[BLE ERROR] Payload length: %u\n"
        "[BLE ERROR] Maximum payload: %u\n"
        "[BLE ERROR] Negotiated MTU: %u\n",
        eventName,
        static_cast<unsigned>(payload.length()),
        maxPayload,
        client.mtu);
    return false;
  }

  dataCharacteristic->setValue(payload.c_str());
  const bool submitted = dataCharacteristic->notify(connHandle);
  Serial.printf(
      "[BLE TX] event=%s length=%u mtu=%u max=%u submitted=%s handle=%u\n",
      eventName,
      static_cast<unsigned>(payload.length()),
      client.mtu,
      maxPayload,
      submitted ? "true" : "false",
      connHandle);
  return submitted;
}

bool sendMtuError(size_t required, uint16_t available, uint16_t connHandle)
{
  JsonDocument document;
  document["event"] = "error";
  document["code"] = "mtu_too_small";
  document["required"] = required;
  document["available"] = available;

  String payload;
  serializeJson(document, payload);
  if (payload.length() <= available &&
      sendRawData(payload, connHandle, "error"))
  {
    return true;
  }

  return sendRawData("{\"event\":\"error\"}", connHandle, "error");
}

bool sendEvent(JsonDocument &document, uint16_t connHandle)
{
  String payload;
  serializeJson(document, payload);

  ClientState client;
  const char *eventName = document["event"] | "unknown";
  if (!getClientState(connHandle, client))
  {
    Serial.printf(
        "[BLE ERROR] Event=%s has no active client handle=%u\n",
        eventName,
        connHandle);
    return false;
  }

  const uint16_t maxPayload = notificationLimit(client);
  if (payload.length() > maxPayload)
  {
    Serial.printf(
        "[BLE ERROR] Event=%s length=%u exceeds max=%u MTU=%u\n",
        eventName,
        static_cast<unsigned>(payload.length()),
        maxPayload,
        client.mtu);
    sendMtuError(payload.length(), maxPayload, connHandle);
    return false;
  }

  return sendRawData(payload, connHandle, eventName);
}

void sendError(const char *code, const char *message, uint16_t connHandle)
{
  JsonDocument document;
  document["event"] = "error";
  document["code"] = code;
  document["message"] = message;
  sendEvent(document, connHandle);
}

bool sendScanStart(uint16_t connHandle)
{
  JsonDocument document;
  document["event"] = "scan_start";
  document["scan_id"] = scanId;
  return sendEvent(document, connHandle);
}

bool sendNetworkResult(
    const WifiNetwork &network,
    int16_t index,
    int16_t total,
    uint16_t connHandle)
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
  return sendEvent(document, connHandle);
}

bool sendScanComplete(int16_t total, uint16_t connHandle)
{
  JsonDocument document;
  document["event"] = "scan_complete";
  document["scan_id"] = scanId;
  document["total"] = total;
  return sendEvent(document, connHandle);
}

bool isScanActive();
uint16_t getScanOwner();
void setScanState(bool active, uint16_t ownerHandle);

void processCommand(WifiScanner &wifiScanner)
{
  PendingRequest request;
  portENTER_CRITICAL(&bleStateMux);
  request = pendingRequest;
  pendingRequest = PendingRequest{};
  portEXIT_CRITICAL(&bleStateMux);

  if (request.command == PendingCommand::NONE)
  {
    return;
  }

  if (request.command == PendingCommand::GET_STATUS)
  {
    const bool scanning = isScanActive();
    setStatus(scanning ? "scanning" : "idle");
    if (!dataNotificationsEnabled(request.connHandle))
    {
      setStatus("waiting_for_notifications");
      return;
    }

    JsonDocument document;
    document["event"] = "status";
    document["state"] = scanning ? "scanning" : "idle";
    document["wifi_connected"] = WiFi.status() == WL_CONNECTED;
    document["scan_id"] = scanId;
    sendEvent(document, request.connHandle);
    return;
  }

  if (request.command == PendingCommand::UNKNOWN)
  {
    sendError(
        "unknown_command",
        "Use SCAN_WIFI or GET_STATUS",
        request.connHandle);
    return;
  }

  if (!dataNotificationsEnabled(request.connHandle))
  {
    setStatus("waiting_for_notifications");
    Serial.printf(
        "[BLE ERROR] SCAN_WIFI rejected: Data notifications are not subscribed handle=%u\n",
        request.connHandle);
    return;
  }

  if (isScanActive() || wifiScanner.isScanning())
  {
    sendError(
        "busy",
        "A Wi-Fi scan is already running",
        request.connHandle);
    return;
  }

  if (!wifiScanner.startScan())
  {
    sendError(
        "scan_start_failed",
        "Wi-Fi scan could not be started",
        request.connHandle);
    return;
  }

  scanId++;
  sentNetworkCount = 0;
  portENTER_CRITICAL(&bleStateMux);
  scanSessionActive = true;
  scanOwnerHandle = request.connHandle;
  portEXIT_CRITICAL(&bleStateMux);
  setStatus("scanning");
  if (!sendScanStart(request.connHandle))
  {
    Serial.println("[BLE ERROR] Scan started but scan_start could not be submitted");
    setScanState(false, BLE_HS_CONN_HANDLE_NONE);
    setStatus("idle");
    return;
  }

  Serial.printf(
      "[WIFI] Scan started scan_id=%lu\n",
      static_cast<unsigned long>(scanId));
}

}

void initBleDebug()
{
  esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT);
  NimBLEDevice::init(BLE_DEVICE_NAME);

  const bool mtuConfigured = NimBLEDevice::setMTU(BLE_LOCAL_MTU_TARGET);
  Serial.printf(
      "[BLE] Local MTU target: %u configured=%s\n",
      BLE_LOCAL_MTU_TARGET,
      mtuConfigured ? "true" : "false");

  NimBLEServer *server = NimBLEDevice::createServer();
  server->setCallbacks(new ServerCallbacks());
  NimBLEService *service = server->createService(BLE_SERVICE_UUID);

  commandCharacteristic = service->createCharacteristic(
      BLE_COMMAND_UUID,
      NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR);
  commandCharacteristic->setCallbacks(new CharacteristicCallbacks());
  commandCharacteristic
      ->createDescriptor("2901", NIMBLE_PROPERTY::READ, 32)
      ->setValue("Command");

  dataCharacteristic = service->createCharacteristic(
      BLE_DATA_UUID,
      NIMBLE_PROPERTY::NOTIFY);
  dataCharacteristic->setCallbacks(new CharacteristicCallbacks());
  dataCharacteristic
      ->createDescriptor("2901", NIMBLE_PROPERTY::READ, 32)
      ->setValue("WiFi scan data");

  statusCharacteristic = service->createCharacteristic(
      BLE_STATUS_UUID,
      NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY);
  statusCharacteristic->setCallbacks(new CharacteristicCallbacks());
  statusCharacteristic
      ->createDescriptor("2901", NIMBLE_PROPERTY::READ, 32)
      ->setValue("Status");

  server->start();

  NimBLEAdvertising *advertising = NimBLEDevice::getAdvertising();
  advertising->setName(BLE_DEVICE_NAME);
  advertising->addServiceUUID(service->getUUID());
  advertising->enableScanResponse(true);
  const bool advertisingStarted = advertising->start();

  setStatus("idle");
  Serial.printf(
      "[BLE] Server initialized advertising=%s\n",
      advertisingStarted ? "started" : "failed to start");
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
  if (!isScanActive() || !wifiScanner.isComplete())
  {
    return;
  }

  const uint16_t ownerHandle = getScanOwner();
  if (!dataNotificationsEnabled(ownerHandle))
  {
    Serial.println("[BLE ERROR] Scan owner is no longer subscribed");
    setScanState(false, BLE_HS_CONN_HANDLE_NONE);
    setStatus("idle");
    return;
  }

  if (wifiScanner.hasError())
  {
    sendError(
        "scan_failed",
        "Wi-Fi scan did not complete",
        ownerHandle);
    setScanState(false, BLE_HS_CONN_HANDLE_NONE);
    setStatus("idle");
    return;
  }

  WifiNetwork network;
  if (wifiScanner.nextNetwork(network))
  {
    if (!sendNetworkResult(
            network,
            sentNetworkCount,
            wifiScanner.resultCount(),
            ownerHandle))
    {
          Serial.println("[BLE ERROR] Wi-Fi result delivery failed");
          setScanState(false, BLE_HS_CONN_HANDLE_NONE);
      setStatus("idle");
      return;
    }

    sentNetworkCount++;
    return;
  }

  if (sendScanComplete(wifiScanner.resultCount(), ownerHandle))
  {
    Serial.printf(
        "[BLE TX] scan_complete submitted total=%d\n",
        wifiScanner.resultCount());
  }
  else
  {
    Serial.println("[BLE ERROR] scan_complete delivery failed");
  }
  setScanState(false, BLE_HS_CONN_HANDLE_NONE);
  setStatus("idle");
}
