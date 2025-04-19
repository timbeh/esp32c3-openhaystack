# ESP32C3 OpenHaystack

[![PlatformIO](https://img.shields.io/badge/platform-PlatformIO-orange.svg)](https://platformio.org)
[![License](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE)

This project implements OpenHaystack firmware for ESP32C3 chips, allowing you to create Apple Find My network compatible accessory tags. The firmware is optimized for low power consumption and reliable operation.

## Table of Contents

- [Features](#features)
- [Requirements](#requirements)
- [Installation](#installation)
- [Usage](#usage)
- [Configuration](#configuration)
- [Troubleshooting](#troubleshooting)
- [Technical Details](#technical-details)

## Features

- Low power consumption with optimized sleep modes
- Easy key generation and uploading through Python scripts
- Built on NimBLE for reliable Bluetooth LE operation


## Requirements

### Compatible with ESP32-C3 based boards

### Software
- [PlatformIO](https://platformio.org/install) (recommended) or Arduino IDE
- Python 3.7 or higher
- Python dependencies (install using `pip install -r scripts/requirements.txt`)


## Installation

1. Clone this repository:
   ```bash
   git clone https://github.com/timbeh/esp32c3-openhaystack.git
   cd esp32c3-openhaystack
   ```

2. Install Python dependencies:
   ```bash
   pip install -r scripts/requirements.txt
   ```

3. Open the project in PlatformIO:
   ```bash
   pio project init
   ```

## Usage

### Building and Uploading

1. Connect your ESP32C3 board to your computer
2. Build and upload the firmware:
   ```bash
   pio run -t upload
   ```

### Adding Your Keys

1. Generate keys using the included keygen.py script (modified generate_keys.py from [macless-haystack](https://github.com/dchristl/macless-haystack))
   ```bash
   python3 scripts/keygen.py
   ```

2. It will generate your keys into the scripts/output folder. 
   - Copy the PREFIX_keyfile to the scripts/input folder
   - You can import the PREFIX_devices.json to the [OpenHaystack macOS App](https://github.com/seemoo-lab/openhaystack)

3. Upload the keys to your ESP32C3:
   ```bash
   python3 scripts/keywriter.py
   ```
   The script will:
   - Auto-detect your ESP32C3 device
   - Present a selection menu if multiple ports are found

   For manual port or keyfile specification:
   ```bash
   python3 scripts/keywriter.py -p /dev/tty.usbserial-XXXX -k your_keyfile
   ```

4. After uploading keys, reset the device. It will automatically start advertising.


## Troubleshooting

### Common Issues

1. **No keys found in partition**
   - Verify key upload using the keywriter script
   - Check the serial monitor for error messages

2. **Device not visible in Find My**
   - Ensure correct public keys are uploaded
   - Verify keys are registered in the OpenHaystack or equivalent app

3. **Port not detected**
   - Install correct USB drivers for your ESP32C3 board
   - Check device manager for COM port assignment


## Technical Details

### Architecture

- Built on NimBLE for Bluetooth LE functionality
- Uses ESP32C3's low-power capabilities
- Stores keys in dedicated flash partition

### Power Management Settings

The firmware is optimized for low power operation:
- Advertises for only 20ms
- Uses light sleep between advertisements

### Power Consumption

- Average current: ~20μA in sleep mode
- Peak current during advertisement: ~10mA
- Battery life depends on advertisement interval and sleep duration
