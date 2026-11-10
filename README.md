# PocketStatus Firmware

## Wi-Fi configuration over BLE

On a fresh device, no Wi-Fi credentials are stored. Use the existing BLE
`SCAN_WIFI` command after subscribing to Data notifications to discover nearby
networks. Each network is returned as a `wifi_result` event.

Send the following JSON to the Command characteristic to save credentials and
start a connection attempt:

```json
{"command":"SET_WIFI","ssid":"My WiFi","password":"secret"}
```

The password is stored in ESP32 NVS and is never included in BLE responses.
The firmware sends `wifi_config_saved` immediately, followed by `wifi_status`
events when the connection state changes.

Other configuration commands:

- `GET_WIFI_CONFIG` returns the configured SSID and connection state.
- `CLEAR_WIFI` removes the saved SSID and password.
- `GET_STATUS` continues to return the device and Wi-Fi status.

Subscribe to the Data and Status characteristics before sending commands.
The Wi-Fi-dependent templates show an offline/configuration message when no
connection or internet data is available.
# PocketStatus Firmware

PocketStatus is a compact ESP32-based desktop gadget that displays real-time time and weather updates on a small OLED display.

## Software Requirements

- [PlatformIO](https://platformio.org/) or Arduino IDE
- ESP32 Arduino framework
- U8g2 graphics library
- Wi-Fi credentials and any required API configuration for the weather feature

Install the U8g2 library through the PlatformIO Library Manager or Arduino IDE Library Manager.

## Build and Upload

### PlatformIO

1. Open the PocketStatus project folder in VS Code.
2. Verify the ESP32 board configuration and required libraries in `platformio.ini`.
3. Connect the ESP32 through USB.
4. Build the firmware.
5. Upload the firmware to the board.
6. Open the Serial Monitor to inspect startup messages and debug network connectivity.

### Arduino IDE

1. Open the firmware project or its main sketch.
2. Install the required libraries.
3. Select the appropriate ESP32 board and serial port.
4. Compile and upload the firmware.

## Development Status

PocketStatus is an experimental gadget project. Features, module names, hardware configuration, and documentation may change as development continues.
