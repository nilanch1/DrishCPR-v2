#ifndef WIFI_CONFIG_MANAGER_H
#define WIFI_CONFIG_MANAGER_H

#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <Preferences.h>
#include "esp_wifi.h"

class WiFiConfigManager {
private:
    AsyncWebServer* server;
    String apSSID;
    String apPassword;
    bool wifiConnected = false;
    bool hotspotActive = false;
    unsigned long lastConnectionAttempt = 0;
    unsigned long connectionCheckInterval = 30000;
    Preferences preferences;
    
    void loadAndConnectWiFi();
    
public:
    WiFiConfigManager(AsyncWebServer* webServer);
    void begin();
    void loop();
    void startHotspot();
    void connectToWiFi(const String& ssid, const String& password);
    void checkWiFiStatus();
    bool saveWiFiCredentials(const String& ssid, const String& password);
    void reconnect();
    void resetConfig();
    
    // Getters
    bool isWiFiConnected() const { return wifiConnected; }
    bool isHotspotActive() const { return hotspotActive; }
    String getSSID() const { return WiFi.SSID(); }
    String getAPSSID() const { return apSSID; }
    IPAddress getLocalIP() const { return wifiConnected ? WiFi.localIP() : IPAddress(0, 0, 0, 0); }
    IPAddress getAPIP() const { return WiFi.softAPIP(); }
    int getRSSI() const { return wifiConnected ? WiFi.RSSI() : 0; }
};

#endif