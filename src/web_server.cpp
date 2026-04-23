#include "web_server.h"

#include <ArduinoJson.h>
#include <DNSServer.h>
#include <ESPAsyncWebServer.h>
#include <LittleFS.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include <BLEDevice.h> 
#include <BLEServer.h> 
#include "freertos/FreeRTOS.h"

namespace {
constexpr const char* kApSsid = "MD"; 
constexpr const char* kApPass = "MA#2580456"; 

constexpr size_t kMaxConfigPayloadBytes = 500 * 1024;
constexpr uint32_t kApIdleTimeoutMs = 600000;
constexpr byte kDnsPort = 53;

IPAddress kApIp(192, 168, 4, 1);
IPAddress kApGateway(192, 168, 4, 1);
IPAddress kApMask(255, 255, 255, 0);

AsyncWebServer gServer(80);
DNSServer gDns;

enum class ApCommand : uint8_t {
  None,
  Enable,
  Disable,
};

portMUX_TYPE gApCommandMux = portMUX_INITIALIZER_UNLOCKED;

bool gRoutesConfigured = false;
bool gServerStarted = false;
bool gApEnabled = false;
bool gClientConnected = false;
bool gClientEverConnected = false;

uint32_t gLastClientSeenMs = 0;
uint32_t gLastApEnableMs = 0;

ApCommand gPendingApCommand = ApCommand::None;

config::RuntimeStatus (*gStatusProvider)() = nullptr;
String (*gConfigProvider)() = nullptr;
bool (*gRemoteActionCb)(uint8_t, String*) = nullptr;
web_server::ConfigSaveStatus (*gConfigSaveCb)(const String&, String*) = nullptr;

void releaseBleMemory() {
    Serial.println("[MEM] Preparing to stop BLE safely...");

    // Stop BLE advertising immediately. The current BLE library version
    // may not expose a global getServer() accessor.
    BLEDevice::stopAdvertising();

    // Wait to let the BLE stack process pending events before deinitialization.
    delay(1000);

    Serial.println("[MEM] De-initializing BLE...");
    BLEDevice::deinit(true);
}
void sendCompressedOrPlain(AsyncWebServerRequest* request, const char* plainPath,
                           const char* gzPath, const char* contentType) {
  if (LittleFS.exists(gzPath)) {
    AsyncWebServerResponse* response = request->beginResponse(LittleFS, gzPath, contentType);
    response->addHeader("Content-Encoding", "gzip");
    request->send(response);
    return;
  }

  if (LittleFS.exists(plainPath)) {
    request->send(LittleFS, plainPath, contentType);
    return;
  }

  request->send(404, "text/plain", "Asset not found");
}

void sendCaptiveRedirect(AsyncWebServerRequest* request) {
  request->redirect("http://192.168.4.1/");
}

void requestApCommand(ApCommand command) {
  portENTER_CRITICAL(&gApCommandMux);
  gPendingApCommand = command;
  portEXIT_CRITICAL(&gApCommandMux);
}

ApCommand takePendingApCommand() {
  portENTER_CRITICAL(&gApCommandMux);
  const ApCommand command = gPendingApCommand;
  gPendingApCommand = ApCommand::None;
  portEXIT_CRITICAL(&gApCommandMux);
  return command;
}

void configureRoutes() {
  gServer.on("/", HTTP_GET, [](AsyncWebServerRequest* request) {
    sendCompressedOrPlain(request, "/index.html", "/index.html.gz", "text/html");
  });

  gServer.on("/style.css", HTTP_GET, [](AsyncWebServerRequest* request) {
    sendCompressedOrPlain(request, "/style.css", "/style.css.gz", "text/css");
  });

  gServer.on("/app.js", HTTP_GET, [](AsyncWebServerRequest* request) {
    sendCompressedOrPlain(request, "/app.js", "/app.js.gz", "application/javascript");
  });
  gServer.on("/api/restart", HTTP_POST, [](AsyncWebServerRequest* request) {
      request->send(200, "application/json", "{\"ok\":true}");
      delay(500);
      ESP.restart();
    });
  gServer.on("/generate_204", HTTP_ANY, sendCaptiveRedirect);
  gServer.on("/hotspot-detect.html", HTTP_ANY, sendCaptiveRedirect);
  gServer.on("/connecttest.txt", HTTP_ANY, sendCaptiveRedirect);
  gServer.on("/ncsi.txt", HTTP_ANY, sendCaptiveRedirect);
  gServer.on("/fwlink", HTTP_ANY, sendCaptiveRedirect);

  gServer.on("/api/config", HTTP_GET, [](AsyncWebServerRequest* request) {
    if (gConfigProvider == nullptr) {
      request->send(500, "application/json", "{\"error\":\"config provider unavailable\"}");
      return;
    }
    const String payload = gConfigProvider();
    request->send(200, "application/json", payload);
  });

  gServer.on(
      "/api/config", HTTP_POST,
      [](AsyncWebServerRequest* request) {
        if (request->_tempObject != nullptr &&
            request->_tempObject != reinterpret_cast<void*>(1)) {
          delete static_cast<String*>(request->_tempObject);
        }
        request->_tempObject = nullptr;
      },
      nullptr,
      [](AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total) {
        if (index == 0) {
          if (total > kMaxConfigPayloadBytes) {
            request->_tempObject = reinterpret_cast<void*>(1);
            request->send(413, "application/json", "{\"error\":\"payload too large\"}");
            return;
          }

          auto* payload = new String();
          payload->reserve(total + 1);
          request->_tempObject = payload;
        }

        if (request->_tempObject == reinterpret_cast<void*>(1)) {
          return;
        }

        String* payload = static_cast<String*>(request->_tempObject);
        payload->concat(reinterpret_cast<const char*>(data), len);

        if ((index + len) != total) {
          return;
        }

        String err;
        web_server::ConfigSaveStatus status = web_server::ConfigSaveStatus::InternalError;
        if (gConfigSaveCb != nullptr) {
          status = gConfigSaveCb(*payload, &err);
        }

        delete payload;
        request->_tempObject = nullptr;

        switch (status) {
          case web_server::ConfigSaveStatus::Ok:
            request->send(200, "application/json", "{\"ok\":true}");
            break;

          case web_server::ConfigSaveStatus::InvalidPayload: {
            const String body = String("{\"error\":\"") + err + "\"}";
            request->send(400, "application/json", body);
            break;
          }

          case web_server::ConfigSaveStatus::NotEnoughSpace: {
            const String body = String("{\"error\":\"") + err + "\"}";
            request->send(507, "application/json", body);
            break;
          }

          case web_server::ConfigSaveStatus::InternalError:
          default: {
            const String body = String("{\"error\":\"") + err + "\"}";
            request->send(500, "application/json", body);
            break;
          }
        }
      });

  gServer.on("/api/status", HTTP_GET, [](AsyncWebServerRequest* request) {
    config::RuntimeStatus status;
    if (gStatusProvider != nullptr) {
      status = gStatusProvider();
    }

    JsonDocument doc;
    doc["battery"] = status.batteryVoltage;
    doc["battery_voltage"] = status.batteryVoltage;
    doc["battery_percent"] = status.batteryPercent;
    doc["charging"] = status.charging;
    doc["low_battery"] = status.lowBattery;
    doc["ble_connected"] = status.bleConnected;
    doc["active_profile"] = status.activeProfileName;
    doc["ap_enabled"] = status.apEnabled;
    doc["wifi_client_connected"] = status.wifiClientConnected;

    String payload;
    serializeJson(doc, payload);
    request->send(200, "application/json", payload);
  });

  gServer.on(
      "/api/action", HTTP_POST,
      [](AsyncWebServerRequest* request) {
        if (request->_tempObject != nullptr &&
            request->_tempObject != reinterpret_cast<void*>(1)) {
          delete static_cast<String*>(request->_tempObject);
        }
        request->_tempObject = nullptr;
      },
      nullptr,
      [](AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total) {
        if (index == 0) {
          if (total > 1024) {
            request->_tempObject = reinterpret_cast<void*>(1);
            request->send(413, "application/json", "{\"error\":\"payload too large\"}");
            return;
          }

          auto* payload = new String();
          payload->reserve(total + 1);
          request->_tempObject = payload;
        }

        if (request->_tempObject == reinterpret_cast<void*>(1)) {
          return;
        }

        String* payload = static_cast<String*>(request->_tempObject);
        payload->concat(reinterpret_cast<const char*>(data), len);

        if ((index + len) != total) {
          return;
        }

        JsonDocument doc;
        DeserializationError de = deserializeJson(doc, *payload);

        delete payload;
        request->_tempObject = nullptr;

        if (de || !doc["button_id"].is<uint8_t>()) {
          request->send(400, "application/json", "{\"error\":\"invalid action payload\"}");
          return;
        }

        const uint8_t buttonId = doc["button_id"].as<uint8_t>();
        String err;
        if (gRemoteActionCb == nullptr || !gRemoteActionCb(buttonId, &err)) {
          const String body = String("{\"error\":\"") + err + "\"}";
          request->send(400, "application/json", body);
          return;
        }

        request->send(200, "application/json", "{\"ok\":true}");
      });

  gServer.onNotFound([](AsyncWebServerRequest* request) {
    if (request->url().startsWith("/api/")) {
      request->send(404, "application/json", "{\"error\":\"endpoint not found\"}");
      return;
    }
    sendCaptiveRedirect(request);
  });
}

void startApInfra() {
  if (gApEnabled) return;

  releaseBleMemory();
  // زيادة وقت الانتظار للسماح لمهام البلوتوث بالتوقف التام قبل تشغيل الواي فاي
  delay(500); 

  Serial.printf("[AP] Starting AP infra, heap before: %u\n", ESP.getFreeHeap());

  WiFi.persistent(false);
  WiFi.disconnect(true, true);
  WiFi.softAPdisconnect(true);
  delay(100);

  WiFi.mode(WIFI_AP);
  WiFi.setSleep(false); 
  delay(100);

  if (!WiFi.softAPConfig(kApIp, kApGateway, kApMask)) {
    Serial.println("[AP] ERROR: softAPConfig failed");
    gApEnabled = false;
    return;
  }

  if (!WiFi.softAP(kApSsid, kApPass, 6, false, 4)) {
    Serial.println("[AP] ERROR: Failed to start softAP");
    gApEnabled = false;
    return;
  }

  Serial.printf("[AP] WiFi started. IP: %s, Heap: %u\n", WiFi.softAPIP().toString().c_str(), ESP.getFreeHeap());

  if (!gServerStarted) {
    Serial.println("[AP] Starting AsyncWebServer...");
    gServer.begin();
    gServerStarted = true;
  }

  gDns.stop();
  delay(50);
  if (gDns.start(kDnsPort, "*", kApIp)) {
      Serial.println("[AP] DNS server started successfully");
  } else {
      Serial.println("[AP] ERROR: DNS server failed to start");
  }

  gApEnabled = true;
  gLastClientSeenMs = millis();
  gClientConnected = false;
}

void stopApInfra() {
  if (!gApEnabled) return;

  Serial.println("[AP] Stopping AP infra...");
  gDns.stop();
  gServer.end(); 
  gServerStarted = false;
  
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_OFF);
  delay(100);

  gApEnabled = false;
  gClientConnected = false;
  
  Serial.println("[AP] AP stopped. System needs reboot to restore BLE safely.");
}

} // namespace

namespace web_server {

void begin(bool enableApOnBoot) {
  static bool initialized = false;
  if (initialized) return;
  initialized = true;

  if (!gRoutesConfigured) {
    configureRoutes();
    gRoutesConfigured = true;
  }

  if (enableApOnBoot) {
    requestApCommand(ApCommand::Enable);
  }
}

void loop() {
  const ApCommand command = takePendingApCommand();

  if (command == ApCommand::Disable) {
    if (gApEnabled) stopApInfra();
  } else if (command == ApCommand::Enable) {
    if (!gApEnabled) startApInfra();
    else gLastClientSeenMs = millis();
  }

  if (!gApEnabled) return;

  gDns.processNextRequest();

  uint32_t now = millis();
  uint8_t stationNum = WiFi.softAPgetStationNum();
  bool connectedNow = (stationNum > 0);

  if (connectedNow != gClientConnected) {
    gClientConnected = connectedNow;
    Serial.printf("[WEB] Station state: %s, Count: %u\n", connectedNow ? "Connected" : "Disconnected", stationNum);
  }

  if (gClientConnected) {
    gLastClientSeenMs = now;
  } else if ((now - gLastClientSeenMs) >= kApIdleTimeoutMs) {
    Serial.println("[WEB] AP Timeout, disabling...");
    stopApInfra();
  }
}

void setStatusProvider(config::RuntimeStatus (*provider)()) { gStatusProvider = provider; }
void setConfigProvider(String (*provider)()) { gConfigProvider = provider; }
void setRemoteActionCallback(bool (*cb)(uint8_t, String*)) { gRemoteActionCb = cb; }
void setConfigSaveCallback(ConfigSaveStatus (*cb)(const String&, String*)) { gConfigSaveCb = cb; }
bool isApEnabled() { return gApEnabled; }
bool isClientConnected() { return gClientConnected; }
void enableAp() { 
    const uint32_t now = millis();
    if ((now - gLastApEnableMs) < 1000) return;
    gLastApEnableMs = now;
    requestApCommand(ApCommand::Enable); 
}
void disableAp() { requestApCommand(ApCommand::Disable); }

} // namespace web_server