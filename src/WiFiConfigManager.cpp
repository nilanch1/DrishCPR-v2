#include "WiFiConfigManager.h"

WiFiConfigManager::WiFiConfigManager(AsyncWebServer* webServer) : server(webServer) {
    uint64_t chipid = ESP.getEfuseMac();
    char chipIdStr[13];
    sprintf(chipIdStr, "%012llX", chipid);
    apSSID = "CPR-" + String(chipIdStr);
    apPassword = "cpr12345";
    
    preferences.begin("wificonfig", false);
    loadAndConnectWiFi();
}

void WiFiConfigManager::loadAndConnectWiFi() {
    String savedSSID = preferences.getString("ssid", "");
    String savedPassword = preferences.getString("password", "");
    
    Serial.println("Loading WiFi config from preferences...");
    Serial.printf("Saved SSID: %s\n", savedSSID.c_str());
    
    if (savedSSID.length() > 0) {
        connectToWiFi(savedSSID, savedPassword);
    }
}

void WiFiConfigManager::begin() {
    Serial.println("🔧 Starting WiFi Configuration Manager...");
    
    // Always start hotspot first
    startHotspot();
    
    Serial.println("✅ WiFi Configuration Manager ready");
    Serial.println("🔗 Hotspot: " + apSSID + " (Password: " + apPassword + ")");
    Serial.println("🌐 Configuration URL: http://192.168.4.1/ssid_config");
}

void WiFiConfigManager::loop() {
    static unsigned long lastCheck = 0;
    unsigned long now = millis();
    
    if (now - lastCheck > connectionCheckInterval) {
        lastCheck = now;
        checkWiFiStatus();
    }
}

void WiFiConfigManager::startHotspot() {
    Serial.println("🔥 Starting WiFi Hotspot...");
    
    WiFi.mode(WIFI_AP_STA);
    esp_wifi_set_ps(WIFI_PS_NONE); // Disable power save for hotspot
    
    IPAddress local_IP(192, 168, 4, 1);
    IPAddress gateway(192, 168, 4, 1);
    IPAddress subnet(255, 255, 255, 0);
    
    WiFi.softAPConfig(local_IP, gateway, subnet);
    
    if (WiFi.softAP(apSSID.c_str(), apPassword.c_str())) {
        hotspotActive = true;
        Serial.println("✅ Hotspot started successfully");
        Serial.println("📶 SSID: " + apSSID);
        Serial.println("🔑 Password: " + apPassword);
        Serial.println("🌐 IP: " + WiFi.softAPIP().toString());
    } else {
        Serial.println("❌ Failed to start hotspot");
        hotspotActive = false;
    }
}

void WiFiConfigManager::connectToWiFi(const String& ssid, const String& password) {
    if (ssid.length() == 0) {
        Serial.println("❌ No SSID provided");
        return;
    }
    
    Serial.println("🔄 Connecting to WiFi: " + ssid);
    lastConnectionAttempt = millis();
    
    WiFi.mode(WIFI_AP_STA);
    WiFi.begin(ssid.c_str(), password.c_str());
    
    Serial.println("⏳ WiFi connection initiated...");
}

void WiFiConfigManager::checkWiFiStatus() {
    bool wasConnected = wifiConnected;
    wifiConnected = (WiFi.status() == WL_CONNECTED);
    
    if (wifiConnected != wasConnected) {
        if (wifiConnected) {
            Serial.println("✅ WiFi Connected!");
            Serial.println("📶 SSID: " + WiFi.SSID());
            Serial.println("📡 Signal: " + String(WiFi.RSSI()) + " dBm");
            Serial.println("🌐 IP: " + WiFi.localIP().toString());
        } else {
            Serial.println("❌ WiFi Disconnected");
            
            // Try to reconnect with saved credentials
            String savedSSID = preferences.getString("ssid", "");
            String savedPassword = preferences.getString("password", "");
            if (savedSSID.length() > 0) {
                Serial.println("🔄 Attempting to reconnect...");
                WiFi.begin(savedSSID.c_str(), savedPassword.c_str());
            }
        }
    }
}

bool WiFiConfigManager::saveWiFiCredentials(const String& ssid, const String& password) {
    if (ssid.isEmpty()) {
        Serial.println("Cannot save empty SSID");
        return false;
    }
    
    // Save to preferences
    preferences.putString("ssid", ssid);
    preferences.putString("password", password);
    
    Serial.printf("WiFi credentials saved: %s\n", ssid.c_str());
    
    // Attempt connection
    connectToWiFi(ssid, password);
    
    return true;
}

void WiFiConfigManager::reconnect() {
    String savedSSID = preferences.getString("ssid", "");
    String savedPassword = preferences.getString("password", "");
    if (savedSSID.length() > 0) {
        Serial.println("🔄 Manual reconnection requested");
        connectToWiFi(savedSSID, savedPassword);
    }
}

void WiFiConfigManager::resetConfig() {
    preferences.remove("ssid");
    preferences.remove("password");
    WiFi.disconnect();
    Serial.println("🗑️ WiFi configuration reset");
}