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
