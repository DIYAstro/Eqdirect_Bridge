// EQDirect Wireless Bridge
// Mode via hardware jumper on GPIO23:
//   Jumper OPEN   → WiFi mode (WebUI active, BT off)
//   Jumper CLOSED → BT mode  (BT SPP bridge, WebUI off)
// BT name is configured via WebUI in WiFi mode and stored in config.

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <WebServer.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <BluetoothSerial.h>
#include <ESPmDNS.h>
#include <DNSServer.h>
#include <esp_wifi.h>

// ---------------------------------------------------------------------------
// Configuration
// ---------------------------------------------------------------------------
struct Config {
  String wifi_mode   = "AP";
  String ssid        = "";
  String psk         = "";
  bool   use_static  = false;
  String static_ip   = "192.168.1.100";
  String static_gw   = "192.168.1.1";
  String static_sn   = "255.255.255.0";
  String ap_ssid     = "EQDirect_Bridge";
  String ap_psk      = "12345678";
  String ap_ip       = "192.168.4.1";
  String bt_name     = "Telescope Bridge";
  bool   echo_filter = true;
  int    packet_timeout = 10;
  bool   usb_bridge   = false;
  int    serial_baud  = 9600;
};

Config    config;
WebServer server(80);
WiFiServer tcpServer(11880);
WiFiClient tcpClient;
WiFiUDP    udp;
WiFiUDP    udp11881;
WiFiUDP    udpDiscovery;
BluetoothSerial btSerial;
DNSServer  dnsServer;

// ---------------------------------------------------------------------------
// Runtime state
// ---------------------------------------------------------------------------
#define JUMPER_PIN 23

#ifndef VERSION
  #define VERSION "dev"
#endif
#define RXD2 16
#define TXD2 17
#define BUFFER_SIZE 1024

bool btMode = false;

enum BridgeMode { MODE_NONE, MODE_WIFI_TCP, MODE_WIFI_UDP, MODE_BT };
BridgeMode activeMode = MODE_NONE;
IPAddress  udpRemoteIP;
uint16_t   udpRemotePort;
String     deviceID = "EQDirect_Bridge";

bool  isRecovering      = false;
bool  dnsStarted        = false;
unsigned long lastWifiCheck      = 0;
unsigned long wifiDisconnectTime = 0;
unsigned long lastUdpPacket      = 0;

WiFiUDP* currentUdpSource = &udp;

static uint8_t serBuf[BUFFER_SIZE];

// ---------------------------------------------------------------------------
// Config persistence
// ---------------------------------------------------------------------------
void loadConfig() {
  if (!LittleFS.begin()) { Serial.println("LittleFS Mount Failed"); return; }
  File file = LittleFS.open("/config.json", "r");
  if (!file) return;
  JsonDocument doc;
  if (deserializeJson(doc, file)) { file.close(); return; }
  file.close();
  config.wifi_mode     = doc["wifi_mode"]     | "AP";
  config.ssid          = doc["ssid"]          | "";
  config.psk           = doc["psk"]           | "";
  config.use_static    = doc["use_static"]    | false;
  config.static_ip     = doc["static_ip"]     | "192.168.1.100";
  config.static_gw     = doc["static_gw"]     | "192.168.1.1";
  config.static_sn     = doc["static_sn"]     | "255.255.255.0";
  config.ap_ssid       = doc["ap_ssid"]       | "EQDirect_Bridge";
  config.ap_psk        = doc["ap_psk"]        | "12345678";
  config.ap_ip         = doc["ap_ip"]         | "192.168.4.1";
  config.bt_name       = doc["bt_name"]       | "Telescope Bridge";
  config.echo_filter    = doc["echo_filter"]   | true;
  config.packet_timeout = doc["packet_timeout"] | 10;
  config.usb_bridge     = doc["usb_bridge"]    | false;
  config.serial_baud    = doc["serial_baud"]   | 9600;

  uint8_t mac[6]; WiFi.macAddress(mac);
  char buf[20]; sprintf(buf, "EQDirect_%02X%02X", mac[4], mac[5]);
  deviceID = String(buf);

  Serial.printf("Config: WiFi=%s SSID=%s BT-Name=%s\n",
    config.wifi_mode.c_str(), config.ssid.c_str(), config.bt_name.c_str());
}

void serializeConfig() {
  JsonDocument doc;
  doc["wifi_mode"]      = config.wifi_mode;
  doc["ssid"]           = config.ssid;
  doc["psk"]            = config.psk;
  doc["use_static"]     = config.use_static;
  doc["static_ip"]      = config.static_ip;
  doc["static_gw"]      = config.static_gw;
  doc["static_sn"]      = config.static_sn;
  doc["ap_ssid"]        = config.ap_ssid;
  doc["ap_psk"]         = config.ap_psk;
  doc["ap_ip"]          = config.ap_ip;
  doc["bt_name"]        = config.bt_name;
  doc["echo_filter"]    = config.echo_filter;
  doc["packet_timeout"] = config.packet_timeout;
  doc["usb_bridge"]     = config.usb_bridge;
  doc["serial_baud"]    = config.serial_baud;
  File file = LittleFS.open("/config.json", "w");
  if (file) { serializeJson(doc, file); file.close(); }
}

// ---------------------------------------------------------------------------
// Web UI handlers (WiFi mode only)
// ---------------------------------------------------------------------------
void handleRoot() {
  File file = LittleFS.open("/index.html", "r");
  if (file) { server.streamFile(file, "text/html"); file.close(); }
  else server.send(404, "text/plain", "File Not Found");
}

void handleGetConfig() {
  JsonDocument doc;
  doc["wifi_mode"]      = config.wifi_mode;
  doc["ssid"]           = config.ssid;
  doc["psk"]            = config.psk;
  doc["use_static"]     = config.use_static;
  doc["static_ip"]      = config.static_ip;
  doc["static_gw"]      = config.static_gw;
  doc["static_sn"]      = config.static_sn;
  doc["ap_ssid"]        = config.ap_ssid;
  doc["ap_psk"]         = config.ap_psk;
  doc["ap_ip"]          = config.ap_ip;
  doc["bt_name"]        = config.bt_name;
  doc["echo_filter"]    = config.echo_filter;
  doc["packet_timeout"] = config.packet_timeout;
  doc["usb_bridge"]     = config.usb_bridge;
  doc["serial_baud"]    = config.serial_baud;
  doc["bt_mode_active"] = btMode;
  String response; serializeJson(doc, response);
  server.send(200, "application/json", response);
}

void handleStatus() {
  JsonDocument doc;
  String modeStr = "Idle";
  if (activeMode == MODE_WIFI_TCP) modeStr = "Active (WiFi TCP)";
  else if (activeMode == MODE_WIFI_UDP) modeStr = "Active (WiFi UDP)";
  else if (activeMode == MODE_BT) modeStr = "Active (Bluetooth)";
  doc["status"]   = modeStr;
  doc["recovery"] = isRecovering;
  doc["ip"]       = WiFi.localIP().toString();
  doc["ap_ip"]    = WiFi.softAPIP().toString();
  doc["rssi"]     = WiFi.RSSI();
  doc["id"]       = deviceID;
  doc["bt_mode"]  = btMode;
  doc["version"]  = VERSION;
  String response; serializeJson(doc, response);
  server.send(200, "application/json", response);
}

void handleScan() {
  int n = WiFi.scanNetworks();
  JsonDocument doc;
  JsonArray networks = doc["networks"].to<JsonArray>();
  for (int i = 0; i < n; ++i) {
    JsonObject net = networks.add<JsonObject>();
    net["ssid"]   = WiFi.SSID(i);
    net["rssi"]   = WiFi.RSSI(i);
    net["secure"] = WiFi.encryptionType(i) != WIFI_AUTH_OPEN;
  }
  String response; serializeJson(doc, response);
  server.send(200, "application/json", response);
  WiFi.scanDelete();
}

void handleSave() {
  config.wifi_mode      = server.arg("wifi_mode");
  config.ssid           = server.arg("ssid");
  config.psk            = server.arg("psk");
  config.use_static     = server.arg("use_static") == "true";
  config.static_ip      = server.arg("static_ip");
  config.static_gw      = server.arg("static_gw");
  config.static_sn      = server.arg("static_sn");
  config.ap_ssid        = server.arg("ap_ssid");
  config.ap_psk         = server.arg("ap_psk");
  config.ap_ip          = server.arg("ap_ip");
  config.bt_name        = server.arg("bt_name");
  config.echo_filter    = server.arg("echo_filter") == "true";
  config.packet_timeout = server.arg("packet_timeout").toInt();
  config.usb_bridge     = server.arg("usb_bridge") == "true";
  config.serial_baud    = server.arg("serial_baud").toInt();
  if (config.serial_baud == 0) config.serial_baud = 9600;
  serializeConfig();
  server.send(200, "text/plain", "Settings saved. Restarting...");
  delay(500); ESP.restart();
}

void handleCaptive() {
  server.sendHeader("Location", String("http://") + WiFi.softAPIP().toString(), true);
  server.send(302, "text/plain", "");
}

// ---------------------------------------------------------------------------
// WiFi setup (WiFi mode only)
// ---------------------------------------------------------------------------
void startWiFi() {
  Serial.println("Initializing WiFi...");
  udpDiscovery.stop();
  udp11881.stop();
  udp.stop();
  tcpServer.stop();
  server.stop();

  if (config.wifi_mode == "STA") {
    WiFi.mode(WIFI_STA);
    if (config.use_static) {
      IPAddress ip, gw, sn;
      String s_ip = config.static_ip; s_ip.trim();
      String s_gw = config.static_gw; s_gw.trim();
      String s_sn = config.static_sn; s_sn.trim();
      if (s_gw.length() < 7) s_gw = s_ip.substring(0, s_ip.lastIndexOf('.') + 1) + "1";
      if (s_sn.length() < 7) s_sn = "255.255.255.0";
      if (ip.fromString(s_ip) && gw.fromString(s_gw) && sn.fromString(s_sn))
        WiFi.config(ip, gw, sn, gw);
    }
    Serial.print("Connecting to: "); Serial.println(config.ssid);
    WiFi.begin(config.ssid.c_str(), config.psk.c_str());
    int retry = 0;
    while (WiFi.status() != WL_CONNECTED && retry < 20) {
      delay(500); Serial.print("."); retry++;
    }
    if (WiFi.status() == WL_CONNECTED) {
      Serial.printf("\nConnected! IP: %s\n", WiFi.localIP().toString().c_str());
      WiFi.softAPdisconnect(true);
    } else {
      Serial.println("\nWiFi failed. Starting Recovery AP.");
      isRecovering = true;
      WiFi.mode(WIFI_AP_STA);
      IPAddress apIP; apIP.fromString(config.ap_ip);
      WiFi.softAPConfig(apIP, apIP, IPAddress(255, 255, 255, 0));
      WiFi.softAP(config.ap_ssid.c_str(), config.ap_psk.c_str());
      dnsServer.start(53, "*", apIP);
      dnsStarted = true;
    }
  } else {
    WiFi.mode(WIFI_AP);
    IPAddress apIP; apIP.fromString(config.ap_ip);
    WiFi.softAPConfig(apIP, apIP, IPAddress(255, 255, 255, 0));
    WiFi.softAP(config.ap_ssid.c_str(), config.ap_psk.c_str());
    Serial.printf("AP Mode: SSID=%s IP=%s\n", config.ap_ssid.c_str(), apIP.toString().c_str());
    dnsServer.start(53, "*", apIP);
    dnsStarted = true;
  }

  WiFi.setSleep(false);
  server.begin();
  tcpServer.begin();
  udp.begin(11880);
  udp11881.begin(11881);
  udpDiscovery.begin(30718);
}

// ---------------------------------------------------------------------------
// BT callback
// ---------------------------------------------------------------------------
void btCallback(esp_spp_cb_event_t event, esp_spp_cb_param_t *param) {
  if (event == ESP_SPP_SRV_OPEN_EVT) Serial.println("BT: Connected.");
  if (event == ESP_SPP_CLOSE_EVT)    Serial.println("BT: Disconnected.");
}

// ---------------------------------------------------------------------------
// UDP helpers (WiFi mode only)
// ---------------------------------------------------------------------------
void handleUdpPacket(WiFiUDP &u) {
  uint8_t buf[256];
  int len = u.read(buf, 256);
  if (len == 0) return;

  if (len == 1 && buf[0] == '1') {
    IPAddress ip = WiFi.localIP();
    if (ip[0] == 0) ip = WiFi.softAPIP();
    uint8_t mac[6]; WiFi.macAddress(mac);
    char macStr[18];
    sprintf(macStr, "%02X:%02X:%02X:%02X:%02X:%02X",
            mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    String resp = ip.toString() + "\r" + String(macStr) + "\r" + config.ap_ssid + "\r";
    u.beginPacket(u.remoteIP(), u.remotePort());
    u.print(resp); u.endPacket();
    return;
  }

  if (buf[0] == ':') {
    if (activeMode == MODE_NONE || activeMode == MODE_WIFI_UDP) {
      currentUdpSource = &u;
      activeMode       = MODE_WIFI_UDP;
      udpRemoteIP      = u.remoteIP();
      udpRemotePort    = u.remotePort();
      lastUdpPacket    = millis();
      Serial2.write(buf, len);
    }
    return;
  }

  u.beginPacket(u.remoteIP(), u.remotePort());
  u.print(deviceID); u.endPacket();
}

// ---------------------------------------------------------------------------
// setup()
// ---------------------------------------------------------------------------
void setup() {
  Serial.begin(115200);
  loadConfig();
  Serial2.begin(config.serial_baud, SERIAL_8N1, RXD2, TXD2);

  if (config.usb_bridge) {
    Serial.println("USB Bridge enabled. Switching to mount baud rate — debug output disabled.");
    Serial.flush();
    Serial.begin(config.serial_baud);
  }

  pinMode(JUMPER_PIN, INPUT_PULLUP);
  delay(10);
  btMode = (digitalRead(JUMPER_PIN) == LOW);

  Serial.printf("Mode: %s (Jumper IO%d: %s)\n",
    btMode ? "BLUETOOTH" : "WIFI", JUMPER_PIN, btMode ? "CLOSED" : "OPEN");

  if (btMode) {
    // ---- BT mode: only BT bridge, no WebServer ----
    btSerial.register_callback(btCallback);
    btSerial.begin(config.bt_name);
    Serial.printf("BT: Advertising as '%s'\n", config.bt_name.c_str());
  } else {
    // ---- WiFi mode: full WebServer, no BT ----
    server.on("/",           HTTP_GET,  handleRoot);
    server.on("/status",     HTTP_GET,  handleStatus);
    server.on("/scan",       HTTP_GET,  handleScan);
    server.on("/save",       HTTP_POST, handleSave);
    server.on("/reboot",     HTTP_POST, []() {
      server.send(200, "text/plain", "Rebooting...");
      delay(200); ESP.restart();
    });
    server.on("/restore", HTTP_POST, []() {
      String body = server.arg("plain");
      if (body.length() > 0) {
        File file = LittleFS.open("/config.json", "w");
        if (file) {
          file.print(body); file.close();
          server.send(200, "text/plain", "Restored. Rebooting...");
          delay(1000); ESP.restart();
        } else server.send(500, "text/plain", "FileSystem Error");
      } else server.send(400, "text/plain", "Empty Body");
    });
    server.on("/config.json", HTTP_GET, handleGetConfig);
    server.onNotFound([]() {
      String uri = server.uri();
      if (uri.endsWith(".map") || uri.endsWith(".xml") ||
          uri.endsWith(".ico") || uri.endsWith(".png"))
        server.send(404, "text/plain", "");
      else handleCaptive();
    });

    startWiFi();

    if (MDNS.begin("eqbridge")) {
      MDNS.addService("http", "tcp", 80);
      Serial.println("mDNS: http://eqbridge.local");
    }
  }

  Serial.println("---------------------------------------");
  if (!btMode) {
    if (WiFi.getMode() & WIFI_AP)
      Serial.printf("AP-IP  : %s\n", WiFi.softAPIP().toString().c_str());
    if (WiFi.status() == WL_CONNECTED)
      Serial.printf("STA-IP : %s\n", WiFi.localIP().toString().c_str());
  }
  Serial.printf("Free heap: %d bytes\n", ESP.getFreeHeap());
  Serial.println("Bridge Ready.");
  Serial.println("---------------------------------------");
}

// ---------------------------------------------------------------------------
// loop()
// ---------------------------------------------------------------------------
void loop() {

  // Jumper change detection: pin must stay in new state for 3s before restart
  static unsigned long lastJumperCheck = 0;
  static unsigned long jumperChangedAt  = 0;
  if (millis() - lastJumperCheck > 500) {
    lastJumperCheck = millis();
    bool pinState = (digitalRead(JUMPER_PIN) == LOW);
    if (pinState != btMode) {
      if (jumperChangedAt == 0) jumperChangedAt = millis();
      else if (millis() - jumperChangedAt >= 3000) {
        if (!config.usb_bridge) Serial.println("Jumper changed — restarting in new mode...");
        delay(200);
        ESP.restart();
      }
    } else {
      jumperChangedAt = 0;
    }
  }

  if (btMode) {
    // ---- BT mode ----
    if (btSerial.connected()) {
      if (activeMode == MODE_NONE) {
        activeMode = MODE_BT;
        if (!config.usb_bridge) Serial.println("BT session started.");
      }
    } else {
      if (activeMode == MODE_BT) {
        activeMode = MODE_NONE;
        if (!config.usb_bridge) Serial.println("BT session ended.");
      }
    }

  } else {
    // ---- WiFi mode ----
    server.handleClient();
    if (dnsStarted) dnsServer.processNextRequest();

    // WiFi recovery
    if (millis() - lastWifiCheck > 5000) {
      lastWifiCheck = millis();
      if (config.wifi_mode == "STA") {
        if (WiFi.status() == WL_CONNECTED) {
          wifiDisconnectTime = 0;
          if (isRecovering) {
            WiFi.mode(WIFI_STA);
            isRecovering = false;
            Serial.println("WiFi restored.");
          }
        } else if (!isRecovering) {
          if (wifiDisconnectTime == 0) wifiDisconnectTime = millis();
          if (millis() - wifiDisconnectTime > 30000) {
            Serial.println("WiFi lost >30s. Restarting.");
            startWiFi();
          }
        }
      }
    }

    // Lantronix Discovery (30718)
    int discSize = udpDiscovery.parsePacket();
    if (discSize > 0) {
      uint8_t buffer[16];
      int len = udpDiscovery.read(buffer, 16);
      if (len >= 4 && buffer[3] == 0xF6) {
        uint8_t mac[6]; WiFi.macAddress(mac);
        IPAddress ip = (WiFi.status() == WL_CONNECTED) ? WiFi.localIP() : WiFi.softAPIP();
        uint8_t response[104]; memset(response, 0, 104);
        response[3] = 0xF7;
        for (int i = 0; i < 4; i++) response[4+i]  = ip[i];
        for (int i = 0; i < 6; i++) response[16+i] = mac[i];
        udpDiscovery.beginPacket(udpDiscovery.remoteIP(), udpDiscovery.remotePort());
        udpDiscovery.write(response, 104); udpDiscovery.endPacket();
      }
    }

    // SynScan UDP
    if (udp.parsePacket())      handleUdpPacket(udp);
    if (udp11881.parsePacket()) handleUdpPacket(udp11881);

    // TCP
    if (activeMode == MODE_NONE && tcpServer.hasClient()) {
      tcpClient  = tcpServer.accept();
      activeMode = MODE_WIFI_TCP;
      if (!config.usb_bridge) Serial.println("TCP Session Started.");
    }
    if (activeMode == MODE_WIFI_TCP && !tcpClient.connected()) {
      tcpClient.stop();
      activeMode = MODE_NONE;
      if (!config.usb_bridge) Serial.println("TCP Session Ended.");
    }
    if (activeMode == MODE_WIFI_UDP && (millis() - lastUdpPacket > 5000)) {
      activeMode = MODE_NONE;
    }
  }

  // ---- Data forwarding: Client → Serial2 (USB has priority) ----
  if (config.usb_bridge && !btMode && Serial.available()) {
    while (Serial.available()) Serial2.write(Serial.read());
  } else if (activeMode == MODE_WIFI_TCP && tcpClient.available()) {
    while (tcpClient.available()) Serial2.write(tcpClient.read());
  } else if (activeMode == MODE_BT && btSerial.available()) {
    while (btSerial.available()) Serial2.write(btSerial.read());
  }

  // ---- Data forwarding: Serial2 → Client (both modes) ----
  bool hasClient = (activeMode != MODE_NONE) || (config.usb_bridge && !btMode);
  if (Serial2.available() && hasClient) {
    int serIdx = 0;
    bool ignore = config.echo_filter;
    while (true) {
      if (Serial2.available()) {
        uint8_t data = Serial2.read();
        if (ignore && (data == '=' || data == '!')) ignore = false;
        if (!ignore) {
          serBuf[serIdx++] = data;
          if (serIdx >= BUFFER_SIZE) break;
        }
      } else {
        delay(config.packet_timeout);
        if (!Serial2.available()) break;
      }
    }
    if (serIdx > 0) {
      if (config.usb_bridge && !btMode)
        Serial.write(serBuf, serIdx);
      if (activeMode == MODE_WIFI_TCP && tcpClient.connected())
        tcpClient.write(serBuf, serIdx);
      else if (activeMode == MODE_WIFI_UDP && currentUdpSource) {
        currentUdpSource->beginPacket(udpRemoteIP, udpRemotePort);
        currentUdpSource->write(serBuf, serIdx);
        currentUdpSource->endPacket();
      } else if (activeMode == MODE_BT && btSerial.connected())
        btSerial.write(serBuf, serIdx);
    }
  }
}
