# EQDirect Bridge

Firmware for an ESP32 that acts as a serial bridge between a telescope mount (EQDirect/EQMod protocol) and a WiFi or Bluetooth client.

## What it does

- **WiFi mode** — the ESP32 opens its own access point and optionally connects to an existing network as a station at the same time. Clients connect via TCP or UDP on port 11880. A browser-based configuration UI is available at `http://eqbridge.local` or the AP address (default `192.168.4.1`). Default AP credentials: SSID `EQDirect_Bridge`, password `12345678`. In station mode the AP is only started as a fallback if the configured network is unreachable.
- **Bluetooth mode** — the ESP32 advertises itself as a Bluetooth Classic SPP device. Windows and Linux can pair it and use it as a virtual COM port. No WiFi is active in this mode.

The mode is determined at boot by a hardware jumper and cannot be changed in software.

## Hardware

- ESP32 development board (tested with Wemos D1 Mini ESP32)
- USB cable for flashing and serial monitoring
- Optional: hardware jumper or switch for mode selection

## GPIO pinout

| GPIO | Function |
|------|----------|
| 16   | UART2 RX — connect to mount TX |
| 17   | UART2 TX — connect to mount RX |
| 23   | Mode select (jumper to GND) |

**GPIO 23 mode select:**
- Jumper open (or not connected) → WiFi mode
- Jumper connected to GND → Bluetooth mode

The jumper is read at boot with an internal pull-up. If the jumper state changes while running, the device restarts automatically within one second.

## Serial interface to mount

- Baud rate: 9600
- Format: 8N1
- The echo filter discards incoming bytes from the mount until a valid EQDirect response start character (`=` or `!`) is seen.

## Network ports (WiFi mode)

| Port | Protocol | Description |
|------|----------|-------------|
| 80   | TCP/HTTP | Web configuration UI |
| 11880 | TCP | EQMod/Stellarium bridge |
| 11880 | UDP | EQMod UDP discovery and data |
| 11881 | UDP | Secondary UDP port |

mDNS hostname: `eqbridge.local`

## Configuration

All settings are stored in LittleFS as `/config.json` and survive reboots. The configuration UI (WiFi mode only) allows:

- AP SSID, password, and IP address
- Station mode SSID and password (DHCP or static IP)
- Bluetooth discovery name (used when the device boots in BT mode)
- Packet timeout (ms) and echo filter toggle

The Bluetooth name must be configured while in WiFi mode and is stored in the config file.

## Building and flashing

**Requirements:**
- [PlatformIO](https://platformio.org/) (CLI or IDE extension)

```bash
# Build and upload firmware
pio run -t upload

# Build and upload filesystem (web UI files in data/)
pio run -t uploadfs
```

**Web Flasher:**
A browser-based flasher is available at [diyastro.github.io/Eqdirect_Bridge](https://diyastro.github.io/Eqdirect_Bridge/) (Chrome or Edge required — Web Serial API).

## Releases

Firmware binaries are built automatically when a git tag is pushed:

```bash
git tag v1.0.0
git push origin v1.0.0
```

This triggers a GitHub Actions workflow that builds the firmware, updates the web flasher binaries, and creates a GitHub Release with the three `.bin` files attached.

## License

This project is licensed under the [GNU General Public License v3.0](LICENSE) and is provided **as is**, without warranty of any kind.

## Dependencies

- [ArduinoJson](https://arduinojson.org/) ≥ 7.0
- ESP32 Arduino framework (espressif32, Arduino framework via PlatformIO)
- Built-in: `BluetoothSerial`, `WiFi`, `WebServer`, `LittleFS`, `ESPmDNS`, `DNSServer`
