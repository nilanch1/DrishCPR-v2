
#include "WebServerSetup.h"
#include "SystemManager.h"
#include "CPRMetricsCalculator.h"
#include "CloudManager.h"
#include "WiFiConfigManager.h"
#include <Preferences.h>

// Implementation of constructor
WebServerSetup::WebServerSetup(SystemManager* sysManager) : 
    systemManager(sysManager),
    server(80),
    webSocket("/ws"),
    animWebSocket("/animws") {
}

void WebServerSetup::setup() {
    // WebSocket setup
    webSocket.onEvent(onWebSocketEvent);
    server.addHandler(&webSocket);
    
    animWebSocket.onEvent(onAnimWebSocketEvent);
    server.addHandler(&animWebSocket);
    
    // Setup all route categories
    setupMainRoutes();
    setupCloudRoutes();
    setupWiFiRoutes();
    setupDataRoutes();
    setupConfigRoutes();
    setupStatusRoutes();
    setupStaticRoutes();
    
    // 404 handler
    server.onNotFound([](AsyncWebServerRequest *request) {
        Serial.printf("404 - Not Found: %s\n", request->url().c_str());
        request->send(404, "text/plain", "Page not found");
    });

    server.begin();
    Serial.println("Web server started with comprehensive routes including cloud configuration");
}

void WebServerSetup::setupMainRoutes() {
    // Main pages
    server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
        request->send(SPIFFS, "/index.html", "text/html");
    });
    
    server.on("/config", HTTP_GET, [](AsyncWebServerRequest *request) {
        request->send(SPIFFS, "/config.html", "text/html");
    });
    
    server.on("/ssid_config", HTTP_GET, [](AsyncWebServerRequest *request) {
        Serial.println("Serving WiFi config page");
        if (SPIFFS.exists("/ssid_config.html")) {
            request->send(SPIFFS, "/ssid_config.html", "text/html");
        } else {
            request->send(404, "text/plain", "WiFi config page not found in SPIFFS");
        }
    });
    
    server.on("/cloud_config", HTTP_GET, [](AsyncWebServerRequest *request) {
        request->send(SPIFFS, "/cloud_config.html", "text/html");
    });
    
    server.on("/data", HTTP_GET, [](AsyncWebServerRequest *request) {
        request->send(SPIFFS, "/data.html", "text/html");
    });
}

void WebServerSetup::setupCloudRoutes() {
    // Get current cloud configuration
    server.on("/get_cloud_config", HTTP_GET, [this](AsyncWebServerRequest *request) {
        Serial.println("Get cloud config request");
        
        CloudConfig cloudConfig = systemManager->getCloudManager()->getConfig();
        
        JsonDocument doc;
        doc["success"] = true;
        doc["provider"] = cloudConfig.provider;
        doc["bucket"] = cloudConfig.bucketName;
        doc["endpoint"] = cloudConfig.endpointUrl;
        doc["frequency"] = cloudConfig.syncFrequency;
        doc["enabled"] = cloudConfig.enabled;
        doc["last_sync"] = cloudConfig.lastSyncTime;
        doc["synced_sessions"] = cloudConfig.syncedSessions;
        doc["sync_in_progress"] = systemManager->getCloudManager()->isSyncInProgress();
        doc["access_key"] = cloudConfig.accessKey;
        doc["secret_key"] = cloudConfig.secretKey;
        
        String response;
        serializeJson(doc, response);
        request->send(200, "application/json", response);
    });
    
    // Save cloud configuration
    server.on("/save_cloud_config", HTTP_POST,
        [](AsyncWebServerRequest *request) {},
        NULL,
        [this](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
            static String postBody = "";
            
            if (index == 0) postBody = "";
            
            for (size_t i = 0; i < len; i++) {
                postBody += (char)data[i];
            }
            
            if (index + len == total) {
                Serial.println("Save cloud config request received");
                
                JsonDocument doc;
                DeserializationError error = deserializeJson(doc, postBody);
                
                if (error) {
                    Serial.println("JSON parse error: " + String(error.c_str()));
                    request->send(400, "application/json", "{\"success\":false,\"error\":\"Invalid JSON\"}");
                    return;
                }
                
                if (systemManager->getCloudManager()->updateConfig(doc)) {
                    JsonDocument response;
                    response["success"] = true;
                    response["message"] = "Cloud configuration saved successfully";
                    
                    String responseStr;
                    serializeJson(response, responseStr);
                    request->send(200, "application/json", responseStr);
                } else {
                    request->send(400, "application/json", "{\"success\":false,\"error\":\"Invalid configuration\"}");
                }
            }
        }
    );
    
    // Test cloud connection
    server.on("/test_cloud_connection", HTTP_POST,
        [](AsyncWebServerRequest *request) {},
        NULL,
        [this](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
            static String postBody = "";
            
            if (index == 0) postBody = "";
            
            for (size_t i = 0; i < len; i++) {
                postBody += (char)data[i];
            }
            
            if (index + len == total) {
                Serial.println("Test cloud connection request");
                
                JsonDocument doc;
                DeserializationError error = deserializeJson(doc, postBody);
                
                JsonDocument response;
                
                if (error) {
                    response["success"] = false;
                    response["error"] = "Invalid JSON";
                } else {
                    // Save current config
                    CloudConfig originalConfig = systemManager->getCloudManager()->getConfig();
                    
                    // Test with new config
                    if (systemManager->getCloudManager()->updateConfig(doc)) {
                        if (!systemManager->getWiFiManager()->isWiFiConnected()) {
                            response["success"] = false;
                            response["error"] = "WiFi not connected";
                        } else {
                            bool testResult = systemManager->getCloudManager()->testConnection();
                            
                            if (testResult) {
                                response["success"] = true;
                                response["message"] = "Cloud connection test successful";
                            } else {
                                response["success"] = false;
                                response["error"] = "Failed to connect to cloud storage. Check credentials and network connection.";
                            }
                        }
                    } else {
                        response["success"] = false;
                        response["error"] = "Invalid configuration for test";
                    }
                }
                
                String responseStr;
                serializeJson(response, responseStr);
                request->send(200, "application/json", responseStr);
            }
        }
    );
    
    // Manual cloud sync trigger
    server.on("/trigger_cloud_sync", HTTP_POST, [this](AsyncWebServerRequest *request) {
        Serial.println("Manual cloud sync triggered");
        
        CloudConfig cloudConfig = systemManager->getCloudManager()->getConfig();
        JsonDocument response;
        
        if (!cloudConfig.enabled) {
            response["success"] = false;
            response["error"] = "Cloud sync not enabled";
        } else if (systemManager->getCloudManager()->isSyncInProgress()) {
            response["success"] = false;
            response["error"] = "Cloud sync already in progress";
        } else if (!systemManager->getWiFiManager()->isWiFiConnected()) {
            response["success"] = false;
            response["error"] = "WiFi not connected";
        } else {
            systemManager->getCloudManager()->performSync();
            response["success"] = true;
            response["message"] = "Cloud sync initiated";
        }
        
        String responseStr;
        serializeJson(response, responseStr);
        request->send(200, "application/json", responseStr);
        
        // Broadcast to WebSockets
        if (webSocket.count() > 0) {
            JsonDocument wsDoc;
            wsDoc["type"] = "recording_status";
            wsDoc["is_recording"] = systemManager->getIsRecording();
            wsDoc["session_id"] = systemManager->getCurrentSessionId();
            wsDoc["cloud_enabled"] = systemManager->getCloudManager()->getConfig().enabled;
            wsDoc["message"] = systemManager->getIsRecording() ? 
                              ("Session " + String(systemManager->getCurrentSessionId()) + " started") : 
                              ("Session " + String(systemManager->getCurrentSessionId()) + " stopped");
            
            String wsMessage;
            serializeJson(wsDoc, wsMessage);
            webSocket.textAll(wsMessage);
        }
        
        if (!systemManager->getIsRecording() && animWebSocket.count() > 0) {
            JsonDocument animDoc;
            animDoc["type"] = "animation";
            animDoc["state"] = "quietude";
            animDoc["timestamp"] = millis();
            
            String animMessage;
            serializeJson(animDoc, animMessage);
            animWebSocket.textAll(animMessage);
        }
    });
    
    // Debug endpoint
    server.on("/debug", HTTP_GET, [this](AsyncWebServerRequest *request) {
        String debug = "<!DOCTYPE html><html><head><title>Debug Info</title></head><body>";
        debug += "<h1>ESP32 CPR Monitor Debug Information</h1>";
        
        debug += "<h2>System Information</h2>";
        debug += "Chip ID: " + systemManager->getChipId() + "<br>";
        debug += "CSV Filename: " + systemManager->getCsvFileName() + "<br>";
        debug += "Free Heap: " + String(ESP.getFreeHeap()) + " bytes<br>";
        
        // WiFi debug information
        debug += "<h2>WiFi Status</h2>";
        debug += "WiFi Connected: " + String(systemManager->getWiFiManager()->isWiFiConnected() ? "Yes" : "No") + "<br>";
        debug += "WiFi SSID: " + systemManager->getWiFiManager()->getSSID() + "<br>";
        debug += "WiFi RSSI: " + String(systemManager->getWiFiManager()->getRSSI()) + " dBm<br>";
        debug += "WiFi IP: " + systemManager->getWiFiManager()->getLocalIP().toString() + "<br>";
        debug += "Hotspot Active: " + String(systemManager->getWiFiManager()->isHotspotActive() ? "Yes" : "No") + "<br>";
        debug += "Hotspot SSID: " + systemManager->getWiFiManager()->getAPSSID() + "<br>";
        debug += "Hotspot IP: " + systemManager->getWiFiManager()->getAPIP().toString() + "<br>";
        
        // Cloud debug information
        CloudConfig cloudConfig = systemManager->getCloudManager()->getConfig();
        debug += "<h2>Cloud Configuration</h2>";
        debug += "Cloud Enabled: " + String(cloudConfig.enabled ? "Yes" : "No") + "<br>";
        debug += "Cloud Provider: " + cloudConfig.provider + "<br>";
        debug += "Cloud Bucket: " + cloudConfig.bucketName + "<br>";
        debug += "Cloud Endpoint: " + cloudConfig.endpointUrl + "<br>";
        debug += "Sync Frequency: " + String(cloudConfig.syncFrequency) + " minutes<br>";
        debug += "Sync In Progress: " + String(systemManager->getCloudManager()->isSyncInProgress() ? "Yes" : "No") + "<br>";
        debug += "Last Sync: " + String(cloudConfig.lastSyncTime) + "<br>";
        debug += "Synced Sessions: " + String(cloudConfig.syncedSessions) + "<br>";
        
        debug += "<h2>SPIFFS Status</h2>";
        debug += "Total: " + String(SPIFFS.totalBytes()) + " bytes<br>";
        debug += "Used: " + String(SPIFFS.usedBytes()) + " bytes<br>";
        debug += "Free: " + String(SPIFFS.totalBytes() - SPIFFS.usedBytes()) + " bytes<br>";
        debug += "Danger Mode: " + String(systemManager->isInDangerMode() ? "Yes" : "No") + "<br>";
        
        debug += "<h2>Files in SPIFFS</h2>";
        File root = SPIFFS.open("/");
        File file = root.openNextFile();
        while (file) {
            if (!file.isDirectory()) {
                debug += String(file.name()) + " (" + String(file.size()) + " bytes)<br>";
            }
            file = root.openNextFile();
        }
        
        debug += "<h2>CSV Status</h2>";
        String csvFileName = systemManager->getCsvFileName();
        debug += "CSV File Exists: " + String(SPIFFS.exists(csvFileName) ? "Yes" : "No") + "<br>";
        debug += "CSV File Open: " + String(systemManager->isCsvFileOpen() ? "Yes" : "No") + "<br>";
        debug += "CSV Write Count: " + String(systemManager->getCsvWriteCount()) + "<br>";
        
        debug += "<h2>Recording Status</h2>";
        debug += "Recording: " + String(systemManager->getIsRecording() ? "Yes" : "No") + "<br>";
        debug += "Current Session: " + String(systemManager->getCurrentSessionId()) + "<br>";
        debug += "Next Session: " + String(systemManager->getNextSessionNumber()) + "<br>";
        
        debug += "<h2>WebSocket Status</h2>";
        debug += "Metrics WS Clients: " + String(webSocket.count()) + "<br>";
        debug += "Animation WS Clients: " + String(animWebSocket.count()) + "<br>";
        
        debug += "</body></html>";
        request->send(200, "text/html", debug);
    });
    
    // Cloud sync status
    server.on("/cloud_sync_status", HTTP_GET, [this](AsyncWebServerRequest *request) {
        CloudConfig cloudConfig = systemManager->getCloudManager()->getConfig();
        
        JsonDocument doc;
        doc["enabled"] = cloudConfig.enabled;
        doc["sync_in_progress"] = systemManager->getCloudManager()->isSyncInProgress();
        doc["last_sync_time"] = cloudConfig.lastSyncTime;
        doc["synced_sessions"] = cloudConfig.syncedSessions;
        doc["provider"] = cloudConfig.provider;
        doc["bucket"] = cloudConfig.bucketName;
        doc["frequency_minutes"] = cloudConfig.syncFrequency;
        
        if (cloudConfig.lastSyncTime > 0) {
            doc["time_since_last_sync"] = systemManager->getCloudManager()->getTimeSinceLastSync();
            doc["next_sync_in"] = systemManager->getCloudManager()->getNextSyncIn();
        }
        
        String response;
        serializeJson(doc, response);
        request->send(200, "application/json", response);
    });
    
    // Disable cloud sync
    server.on("/disable_cloud_sync", HTTP_POST, [this](AsyncWebServerRequest *request) {
        Serial.println("Disabling cloud sync");
        
        JsonDocument configDoc;
        configDoc["enabled"] = false;
        systemManager->getCloudManager()->updateConfig(configDoc);
        
        JsonDocument response;
        response["success"] = true;
        response["message"] = "Cloud sync disabled";
        
        String responseStr;
        serializeJson(response, responseStr);
        request->send(200, "application/json", responseStr);
    });
}

void WebServerSetup::setupStaticRoutes() {
    // Static file serving
    server.serveStatic("/", SPIFFS, "/").setDefaultFile("index.html");
    
    // Audio files
    server.on("/rateTooLow.mp3", HTTP_GET, [](AsyncWebServerRequest *request) {
        if (SPIFFS.exists("/rateTooLow.mp3")) {
            AsyncWebServerResponse *response = request->beginResponse(SPIFFS, "/rateTooLow.mp3", "audio/mpeg");
            response->addHeader("Accept-Ranges", "bytes");
            request->send(response);
        } else {
            request->send(404, "text/plain", "Audio file not found");
        }
    });
    
    server.on("/rateTooHigh.mp3", HTTP_GET, [](AsyncWebServerRequest *request) {
        if (SPIFFS.exists("/rateTooHigh.mp3")) {
            AsyncWebServerResponse *response = request->beginResponse(SPIFFS, "/rateTooHigh.mp3", "audio/mpeg");
            response->addHeader("Accept-Ranges", "bytes");
            request->send(response);
        } else {
            request->send(404, "text/plain", "Audio file not found");
        }
    });
    
    server.on("/depthTooLow.mp3", HTTP_GET, [](AsyncWebServerRequest *request) {
        if (SPIFFS.exists("/depthTooLow.mp3")) {
            AsyncWebServerResponse *response = request->beginResponse(SPIFFS, "/depthTooLow.mp3", "audio/mpeg");
            response->addHeader("Accept-Ranges", "bytes");
            request->send(response);
        } else {
            request->send(404, "text/plain", "Audio file not found");
        }
    });
    
    server.on("/depthTooHigh.mp3", HTTP_GET, [](AsyncWebServerRequest *request) {
        if (SPIFFS.exists("/depthTooHigh.mp3")) {
            AsyncWebServerResponse *response = request->beginResponse(SPIFFS, "/depthTooHigh.mp3", "audio/mpeg");
            response->addHeader("Accept-Ranges", "bytes");
            request->send(response);
        } else {
            request->send(404, "text/plain", "Audio file not found");
        }
    });
    
    server.on("/incompleteRecoil.mp3", HTTP_GET, [](AsyncWebServerRequest *request) {
        if (SPIFFS.exists("/incompleteRecoil.mp3")) {
            AsyncWebServerResponse *response = request->beginResponse(SPIFFS, "/incompleteRecoil.mp3", "audio/mpeg");
            response->addHeader("Accept-Ranges", "bytes");
            request->send(response);
        } else {
            request->send(404, "text/plain", "Audio file not found");
        }
    });
    
    // Image files
    server.on("/A4.png", HTTP_GET, [](AsyncWebServerRequest *request) {
        if (SPIFFS.exists("/A4.png")) {
            request->send(SPIFFS, "/A4.png", "image/png");
        } else {
            request->send(404, "text/plain", "Image not found");
        }
    });
    
    server.on("/B4.png", HTTP_GET, [](AsyncWebServerRequest *request) {
        if (SPIFFS.exists("/B4.png")) {
            request->send(SPIFFS, "/B4.png", "image/png");
        } else {
            request->send(404, "text/plain", "Image not found");
        }
    });
}

// WebSocket event handlers
void WebServerSetup::onWebSocketEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len) {
    switch (type) {
        case WS_EVT_CONNECT:
            Serial.printf("WebSocket client #%u connected from %s\n", client->id(), client->remoteIP().toString().c_str());
            break;
        case WS_EVT_DISCONNECT:
            Serial.printf("WebSocket client #%u disconnected\n", client->id());
            break;
        case WS_EVT_DATA:
            // Handle incoming data if needed
            break;
        case WS_EVT_PONG:
        case WS_EVT_ERROR:
            break;
    }
}

void WebServerSetup::onAnimWebSocketEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len) {
    switch (type) {
        case WS_EVT_CONNECT:
            Serial.printf("Animation WebSocket client #%u connected from %s\n", client->id(), client->remoteIP().toString().c_str());
            break;
        case WS_EVT_DISCONNECT:
            Serial.printf("Animation WebSocket client #%u disconnected\n", client->id());
            break;
        case WS_EVT_DATA:
            // Handle incoming data if needed
            break;
        case WS_EVT_PONG:
        case WS_EVT_ERROR:
            break;
    }
}

void WebServerSetup::setupWiFiRoutes() {
    // Get current WiFi configuration
    server.on("/get_wifi_config", HTTP_GET, [this](AsyncWebServerRequest *request) {
        Preferences prefs;
        prefs.begin("wificonfig", true);
        String ssid = prefs.getString("ssid", "");
        prefs.end();
        
        JsonDocument doc;
        doc["ssid"] = ssid;
        doc["currently_connected"] = systemManager->getWiFiManager()->isWiFiConnected();
        doc["current_ssid"] = systemManager->getWiFiManager()->getSSID();
        
        String response;
        serializeJson(doc, response);
        request->send(200, "application/json", response);
    });
    
    // WiFi scan endpoint
    server.on("/scan_networks", HTTP_POST, [this](AsyncWebServerRequest *request) {
        Serial.println("WiFi scan request received");
        
        static bool scanInProgress = false;
        static unsigned long scanStartTime = 0;
        const unsigned long SCAN_TIMEOUT = 15000;
        
        if (scanInProgress) {
            if (millis() - scanStartTime > SCAN_TIMEOUT) {
                Serial.println("WiFi scan timeout, resetting...");
                WiFi.scanDelete();
                scanInProgress = false;
            } else {
                request->send(429, "application/json", "{\"success\":false,\"error\":\"Scan already in progress\"}");
                return;
            }
        }
        
        scanInProgress = true;
        scanStartTime = millis();
        
        Serial.println("Starting WiFi network scan...");
        WiFi.scanNetworks(true, false, false, 300);
        
        unsigned long waitStart = millis();
        while (WiFi.scanComplete() == WIFI_SCAN_RUNNING && (millis() - waitStart) < 3000) {
            delay(100);
            yield();
        }
        
        int16_t scanResult = WiFi.scanComplete();
        JsonDocument doc;
        
        if (scanResult == WIFI_SCAN_RUNNING) {
            doc["success"] = false;
            doc["error"] = "Scan timeout - try again in a moment";
            scanInProgress = false;
            WiFi.scanDelete();
        } else if (scanResult == WIFI_SCAN_FAILED || scanResult < 0) {
            doc["success"] = false;
            doc["error"] = "WiFi scan failed";
            scanInProgress = false;
            WiFi.scanDelete();
        } else {
            JsonArray networks = doc["networks"].to<JsonArray>();
            int networkCount = scanResult;
            
            if (networkCount > 0) {
                int maxNetworks = min(networkCount, 20);
                
                for (int i = 0; i < maxNetworks; i++) {
                    JsonObject network = networks.add<JsonObject>();
                    network["ssid"] = WiFi.SSID(i);
                    network["rssi"] = WiFi.RSSI(i);
                    
                    wifi_auth_mode_t authType = WiFi.encryptionType(i);
                    String authString = "Unknown";
                    switch(authType) {
                        case WIFI_AUTH_OPEN: authString = "Open"; break;
                        case WIFI_AUTH_WEP: authString = "WEP"; break;
                        case WIFI_AUTH_WPA_PSK: authString = "WPA"; break;
                        case WIFI_AUTH_WPA2_PSK: authString = "WPA2"; break;
                        case WIFI_AUTH_WPA_WPA2_PSK: authString = "WPA/WPA2"; break;
                        case WIFI_AUTH_WPA2_ENTERPRISE: authString = "WPA2-Enterprise"; break;
                        case WIFI_AUTH_WPA3_PSK: authString = "WPA3"; break;
                        case WIFI_AUTH_WPA2_WPA3_PSK: authString = "WPA2/WPA3"; break;
                        default: authString = "Encrypted"; break;
                    }
                    
                    network["auth_mode"] = authString;
                    network["channel"] = WiFi.channel(i);
                    
                    int rssi = WiFi.RSSI(i);
                    String signalStrength = "Weak";
                    if (rssi >= -50) signalStrength = "Excellent";
                    else if (rssi >= -60) signalStrength = "Good";
                    else if (rssi >= -70) signalStrength = "Fair";
                    
                    network["signal_strength"] = signalStrength;
                }
                
                doc["success"] = true;
                doc["count"] = networkCount;
                doc["displayed"] = maxNetworks;
            } else {
                doc["success"] = true;
                doc["count"] = 0;
                doc["message"] = "No networks found";
            }
            
            scanInProgress = false;
            WiFi.scanDelete();
        }
        
        String response;
        serializeJson(doc, response);
        request->send(200, "application/json", response);
    });
    
    // Save WiFi configuration
    server.on("/save_wifi_config", HTTP_POST,
        [](AsyncWebServerRequest *request) {},
        NULL,
        [this](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
            static String postBody = "";
            
            if (index == 0) postBody = "";
            
            for (size_t i = 0; i < len; i++) {
                postBody += (char)data[i];
            }
            
            if (index + len == total) {
                Serial.println("WiFi config save request received");
                
                JsonDocument doc;
                DeserializationError error = deserializeJson(doc, postBody);
                
                if (error) {
                    request->send(400, "application/json", "{\"success\":false,\"error\":\"Invalid JSON\"}");
                    return;
                }
                
                String ssid = doc["ssid"] | "";
                String password = doc["password"] | "";
                
                if (ssid.isEmpty()) {
                    request->send(400, "application/json", "{\"success\":false,\"error\":\"SSID cannot be empty\"}");
                    return;
                }
                
                Serial.println("Configuring WiFi: " + ssid);
                
                bool result = systemManager->getWiFiManager()->saveWiFiCredentials(ssid, password);
                
                JsonDocument response;
                response["success"] = result;
                if (result) {
                    response["message"] = "WiFi configuration saved and connection initiated";
                } else {
                    response["error"] = "Failed to save configuration";
                }
                
                String responseStr;
                serializeJson(response, responseStr);
                request->send(200, "application/json", responseStr);
            }
        }
    );
    
    // Network status endpoint
    server.on("/network_status", HTTP_GET, [this](AsyncWebServerRequest *request) {
        CloudConfig cloudConfig = systemManager->getCloudManager()->getConfig();
        
        JsonDocument status;
        status["wifi_connected"] = systemManager->getWiFiManager()->isWiFiConnected();
        status["wifi_ssid"] = systemManager->getWiFiManager()->getSSID();
        status["wifi_rssi"] = systemManager->getWiFiManager()->getRSSI();
        status["ip_address"] = systemManager->getWiFiManager()->getLocalIP().toString();
        status["hotspot_active"] = systemManager->getWiFiManager()->isHotspotActive();
        status["hotspot_ssid"] = systemManager->getWiFiManager()->getAPSSID();
        status["cloud_enabled"] = cloudConfig.enabled;
        status["cloud_sync_in_progress"] = systemManager->getCloudManager()->isSyncInProgress();
        status["timestamp"] = millis();
        
        String response;
        serializeJson(status, response);
        request->send(200, "application/json", response);
    });
    
    // Internet connectivity status endpoint
    server.on("/internet_status", HTTP_GET, [this](AsyncWebServerRequest *request) {
        systemManager->getNetworkManager()->checkInternetConnectivity();
        
        JsonDocument status;
        status["internet_connected"] = systemManager->getNetworkManager()->isInternetConnected();
        status["wifi_connected"] = systemManager->getWiFiManager()->isWiFiConnected();
        status["cloud_enabled"] = systemManager->getCloudManager()->getConfig().enabled;
        status["cloud_sync_in_progress"] = systemManager->getCloudManager()->isSyncInProgress();
        status["timestamp"] = millis();
        
        if (systemManager->getWiFiManager()->isWiFiConnected()) {
            status["wifi_ssid"] = systemManager->getWiFiManager()->getSSID();
            status["wifi_rssi"] = systemManager->getWiFiManager()->getRSSI();
            status["ip_address"] = systemManager->getWiFiManager()->getLocalIP().toString();
        }
        
        String response;
        serializeJson(status, response);
        request->send(200, "application/json", response);
    });
}

void WebServerSetup::setupDataRoutes() {
    // Data management API
    server.on("/files_api", HTTP_GET, [this](AsyncWebServerRequest *request) {
        JsonDocument doc;
        JsonArray filesArray = doc["files"].to<JsonArray>();
        
        File root = SPIFFS.open("/");
        File file = root.openNextFile();
        
        while (file) {
            if (!file.isDirectory()) {
                JsonObject fileObj = filesArray.add<JsonObject>();
                fileObj["name"] = String(file.name());
                fileObj["size"] = file.size();
            }
            file = root.openNextFile();
        }
        
        String csvFileName = systemManager->getCsvFileName();
        CloudConfig cloudConfig = systemManager->getCloudManager()->getConfig();
        
        doc["csv_file_exists"] = SPIFFS.exists(csvFileName);
        doc["csv_file_name"] = csvFileName;
        doc["chip_id"] = systemManager->getChipId();
        doc["next_session"] = systemManager->getNextSessionNumber();
        doc["cloud_enabled"] = cloudConfig.enabled;
        doc["cloud_provider"] = cloudConfig.provider;
        
        String response;
        serializeJson(doc, response);
        request->send(200, "application/json", response);
    });
    
    server.on("/download_csv", HTTP_GET, [this](AsyncWebServerRequest *request) {
        String csvFileName = systemManager->getCsvFileName();
        if (SPIFFS.exists(csvFileName)) {
            request->send(SPIFFS, csvFileName, "text/csv", true);
        } else {
            request->send(404, "text/plain", "CSV file not found");
        }
    });
    
    server.on("/delete_csv", HTTP_POST, [this](AsyncWebServerRequest *request) {
        JsonDocument response;
        
        if (systemManager->getIsRecording()) {
            response["success"] = false;
            response["error"] = "Cannot delete CSV file while recording is active";
        } else {
            bool result = systemManager->deleteCSVFile();
            response["success"] = result;
            if (result) {
                response["message"] = "CSV file deleted successfully";
            } else {
                response["error"] = "Failed to delete CSV file";
            }
        }
        
        String responseStr;
        serializeJson(response, responseStr);
        request->send(200, "application/json", responseStr);
    });
}

void WebServerSetup::setupConfigRoutes() {
    // Configuration API
    server.on("/get_config", HTTP_GET, [this](AsyncWebServerRequest *request) {
        CPRThresholds params = systemManager->getMetricsCalculator()->getParams();
        
        JsonDocument doc;
        doc["status"] = "success";
        
        JsonObject config = doc["config"].to<JsonObject>();
        config["r1"] = params.r1;
        config["r2"] = params.r2;
        config["c1"] = params.c1;
        config["c2"] = params.c2;
        config["f1"] = params.f1;
        config["f2"] = params.f2;
        config["quiet_threshold"] = params.quietThreshold;
        config["smoothing_window"] = params.smoothingWindow;
        config["rate_smoothing_factor"] = params.rateSmoothingFactor;
        
        String response;
        serializeJson(doc, response);
        request->send(200, "application/json", response);
    });
    
    // Configuration POST handler
    server.on("/config", HTTP_POST,
        [](AsyncWebServerRequest *request) {},
        NULL,
        [this](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
            static String postBody = "";
            
            if (index == 0) postBody = "";
            
            for (size_t i = 0; i < len; i++) {
                postBody += (char)data[i];
            }
            
            if (index + len == total) {
                JsonDocument doc;
                DeserializationError error = deserializeJson(doc, postBody);
                
                if (error) {
                    request->send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
                    return;
                }
                
                try {
                    CPRThresholds newParams;
                    newParams.r1 = doc["r1"] | 200;
                    newParams.r2 = doc["r2"] | 300;
                    newParams.c1 = doc["c1"] | 700;
                    newParams.c2 = doc["c2"] | 900;
                    newParams.f1 = doc["f1"] | 100;
                    newParams.f2 = doc["f2"] | 120;
                    newParams.quietThreshold = doc["quiet_threshold"] | 2.0;
                    newParams.smoothingWindow = doc["smoothing_window"] | 3;
                    newParams.rateSmoothingFactor = doc["rate_smoothing_factor"] | 0.3;
                    
                    systemManager->getMetricsCalculator()->updateParams(newParams);
                    
                                        request->send(200, "application/json", "{\"status\":\"success\",\"message\":\"Configuration updated\"}");
                    Serial.println("Configuration updated via web interface");
                    
                } catch (...) {
                    request->send(500, "application/json", "{\"error\":\"Failed to update configuration\"}");
                }
            }
        }
    );
}

void WebServerSetup::setupStatusRoutes() {
    // Status endpoint
    server.on("/status", HTTP_GET, [this](AsyncWebServerRequest *request) {
        CloudConfig cloudConfig = systemManager->getCloudManager()->getConfig();
        String csvFileName = systemManager->getCsvFileName();
        
        JsonDocument status;
        status["status"] = "running";
        status["chip_id"] = systemManager->getChipId();
        status["recording"] = systemManager->getIsRecording();
        status["session_id"] = systemManager->getCurrentSessionId();
        status["next_session"] = systemManager->getNextSessionNumber();
        status["metrics_clients"] = webSocket.count();
        status["anim_clients"] = animWebSocket.count();
        status["free_heap"] = ESP.getFreeHeap();
        status["csv_file_open"] = systemManager->isCsvFileOpen();
        status["csv_file_name"] = csvFileName;
        status["csv_file_exists"] = SPIFFS.exists(csvFileName);
        status["csv_write_count"] = systemManager->getCsvWriteCount();
        
        // WiFi status information
        status["wifi_connected"] = systemManager->getWiFiManager()->isWiFiConnected();
        status["wifi_ssid"] = systemManager->getWiFiManager()->getSSID();
        status["wifi_rssi"] = systemManager->getWiFiManager()->getRSSI();
        status["hotspot_active"] = systemManager->getWiFiManager()->isHotspotActive();
        status["hotspot_ssid"] = systemManager->getWiFiManager()->getAPSSID();
        
        // Cloud status information
        status["cloud_enabled"] = cloudConfig.enabled;
        status["cloud_provider"] = cloudConfig.provider;
        status["cloud_bucket"] = cloudConfig.bucketName;
        status["cloud_sync_frequency"] = cloudConfig.syncFrequency;
        status["cloud_sync_in_progress"] = systemManager->getCloudManager()->isSyncInProgress();
        status["cloud_last_sync"] = cloudConfig.lastSyncTime;
        status["cloud_synced_sessions"] = cloudConfig.syncedSessions;
        
        if (SPIFFS.exists(csvFileName)) {
            File checkFile = SPIFFS.open(csvFileName, "r");
            if (checkFile) {
                status["csv_file_size"] = checkFile.size();
                checkFile.close();
            }
        }
        
        String response;
        serializeJson(status, response);
        request->send(200, "application/json", response);
    });
    
    // Recording control
    server.on("/start_stop", HTTP_POST, [this](AsyncWebServerRequest *request) {
        JsonDocument response;
        
        if (systemManager->isInDangerMode() && !systemManager->getIsRecording()) {
            response["status"] = "blocked";
            response["error"] = "Operations suspended - SPIFFS storage full. Enable cloud upload.";
            response["spiffs_danger"] = true;
            response["is_recording"] = false;
            
            String responseStr;
            serializeJson(response, responseStr);
            request->send(423, "application/json", responseStr);
            return;
        }
        
        if (!systemManager->getIsRecording()) {
            if (systemManager->startRecording()) {
                response["status"] = "started";
                response["session_id"] = systemManager->getCurrentSessionId();
                response["is_recording"] = true;
                
                Serial.printf("Training session %d started - metrics reset\n", systemManager->getCurrentSessionId());
            } else {
                response["status"] = "failed";
                response["error"] = "Failed to start recording";
            }
        } else {
            if (systemManager->stopRecording()) {
                response["status"] = "stopped";
                response["session_id"] = systemManager->getCurrentSessionId();
                response["is_recording"] = false;
                
                Serial.printf("Training session %d stopped\n", systemManager->getCurrentSessionId());
            } else {
                response["status"] = "failed";
                response["error"] = "Failed to stop recording";
            }
        }
        
        String responseStr;
        serializeJson(response, responseStr);
        request->send(200, "application/json", responseStr);
    });
}