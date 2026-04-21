# EQDirect Bridge

Firmware for an ESP32 that acts as a wireless serial bridge between SkyWatcher telescope mounts (EQDirect/EQMod protocol) and WiFi or Bluetooth clients.

---

## Flashing via Web Flasher

The easiest way to flash the firmware is via browser — no software installation required.

**[https://diyastro.github.io/Eqdirect_Bridge/](https://diyastro.github.io/Eqdirect_Bridge/)**

Requirements:
- Chrome or Edge (Web Serial API)
- USB cable connected to the ESP32

> **Warning:** Make sure only the EQDirect Bridge is connected via USB. Any ESP32 you select will be overwritten without further confirmation. All existing settings will be erased after flashing.

After flashing, the device boots into WiFi mode and opens an access point:

| | |
|-|-|
| SSID | `EQDirect_Bridge` |
| Password | `12345678` |
| Web UI | `http://192.168.4.1` or `http://eqbridge.local` |

---

## Wiring

### Mount connection (UART2)

| ESP32 GPIO | Connect to |
|------------|------------|
| 16 (RX)    | Mount TX   |
| 17 (TX)    | Mount TX   |

The mount serial interface runs at 9600 baud, 8N1 by default. The baud rate is configurable via the Web UI.

### Mode selection (GPIO 23)

| Jumper state | Mode |
|--------------|------|
| Open / not connected | WiFi mode |
| Connected to GND | Bluetooth mode |

The pin is read with an internal pull-up at boot. If the jumper state changes while running, the device waits 3 seconds and then restarts automatically.

> Bluetooth mode uses Bluetooth Classic SPP — compatible with Windows and Linux as a virtual COM port. Not compatible with Android or iOS.

---

## Operating modes

### WiFi mode

- The bridge opens its own access point (`EQDirect_Bridge`) at all times
- Optionally connects to a home network (station mode) — the AP stays active as fallback if the home network is unreachable
- Clients connect via TCP or UDP on port 11880
- Web UI available at `http://eqbridge.local` or the AP address

### Bluetooth mode

- Advertises as a Bluetooth Classic SPP device
- No WiFi active in this mode
- Bluetooth name is configured via the Web UI in WiFi mode

### USB bridge mode (optional)

When enabled in the Web UI, UART0 (the USB port) acts as a direct serial connection to the mount — identical to a dedicated USB-TTL adapter. WiFi bridge (TCP/UDP) and auto-discovery are disabled in this mode. The Web UI remains accessible via WiFi.

---

## Configuration

The Web UI is available in WiFi mode at `http://eqbridge.local` or `http://192.168.4.1`.

Settings are stored in LittleFS and survive reboots:

- AP SSID, password and IP address
- Station mode SSID, password, DHCP or static IP
- Bluetooth discovery name
- Mount baud rate (default: 9600)
- USB bridge mode
- Packet timeout and echo filter

---

## Network ports (WiFi mode)

| Port  | Protocol | Description |
|-------|----------|-------------|
| 80    | TCP/HTTP | Web UI |
| 11880 | TCP      | EQMod / Stellarium bridge |
| 11880 | UDP      | SynScan UDP data and discovery |
| 11881 | UDP      | Secondary UDP port |

mDNS hostname: `eqbridge.local`

---

## Hardware

Tested with an ESP32 development board in D1 Mini form factor.

---

## Building from source

**Requirements:** [PlatformIO](https://platformio.org/) (CLI or IDE extension)

```bash
# Build and upload firmware
pio run -t upload

# Build and upload filesystem (Web UI files from data/)
pio run -t uploadfs
```

## Releases

A GitHub Actions workflow builds and publishes the firmware automatically when a git tag is pushed:

```bash
git tag v1.0.0
git push origin v1.0.0
```

The workflow builds firmware and filesystem, updates the web flasher, and creates a GitHub Release with changelog.

---

## License

This project is licensed under the [GNU General Public License v3.0](LICENSE) and is provided **as is**, without warranty of any kind.
