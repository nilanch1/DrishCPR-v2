#include "NetworkManager.h"
#include <HTTPClient.h>  
extern "C" {
  #include "esp_wifi.h"
}

void NetworkManager::checkInternetConnectivity() {
    // ADD THIS CHECK RIGHT HERE AT THE VERY BEGINNING
    extern bool fileUploadInProgress; // Reference the global variable
    if (fileUploadInProgress) {
        Serial.println("⏸️ Skipping internet connectivity check - file upload in progress");
        return;
    }
    
    // EXISTING CODE CONTINUES BELOW
    unsigned long now = millis();
    
    // Don't check too frequently
    if (now - lastInternetCheck < INTERNET_CHECK_INTERVAL) {
        return;
    }
    
    lastInternetCheck = now;
    
    // Only check if we have WiFi connection
    if (WiFi.status() != WL_CONNECTED) {
        internetConnected = false;
        isSTAConnected = false;
        return;
    }
    
    // Update our tracking
    isSTAConnected = true;
    
    Serial.println("🌐 Checking internet connectivity...");
    
    HTTPClient http;
    http.setTimeout(5000); // 5 second timeout
    http.begin("http://connectivitycheck.gstatic.com/generate_204");
    
    int httpCode = http.GET();
    
    if (httpCode == 204) {
        internetConnected = true;
        //Serial.println("✅ Internet connectivity confirmed");
    } else {
        internetConnected = false;
        Serial.printf("❌ Internet not available. HTTP code: %d\n", httpCode);
    }
    
    http.end();
}

NetworkManager::NetworkManager() {
    apIP = IPAddress(192, 168, 4, 1);
    apGateway = IPAddress(192, 168, 4, 1);
    apSubnet = IPAddress(255, 255, 255, 0);
    isAPMode = false;
    isSTAConnected = (WiFi.status() == WL_CONNECTED); // Check actual WiFi status
    lastConnectionAttempt = 0;
    connectionTimeout = 30000; // 30 seconds
    reconnectAttempts = 0;
    
    // Initialize internet connectivity tracking
    internetConnected = false;
    lastInternetCheck = 0;
    
    preferences.begin("wifi", false);
    loadWiFiConfig();
}

NetworkManager::~NetworkManager() {
    preferences.end();
}

void NetworkManager::loadWiFiConfig() {
    wifiConfig.ssid = preferences.getString("ssid", "");
    wifiConfig.password = preferences.getString("password", "");
    wifiConfig.isConfigured = !wifiConfig.ssid.isEmpty() && !wifiConfig.password.isEmpty();
    
    if (wifiConfig.isConfigured) {
        Serial.printf("Loaded WiFi config: %s\n", wifiConfig.ssid.c_str());
    } else {
        Serial.println("No WiFi credentials saved");
    }
}

void NetworkManager::saveWiFiConfig() {
    preferences.putString("ssid", wifiConfig.ssid);
    preferences.putString("password", wifiConfig.password);
    Serial.printf("WiFi credentials saved for: %s\n", wifiConfig.ssid.c_str());
}

bool NetworkManager::setupAP() {
    Serial.println("Setting up Access Point...");
    
    // Configure AP
    if (!WiFi.softAPConfig(apIP, apGateway, apSubnet)) {
        Serial.println("Failed to configure AP");
        return false;
    }
    
    if (!WiFi.softAP(apSSID, apPassword)) {
        Serial.println("Failed to start AP");
        return false;
    }
    
    isAPMode = true;
    
    Serial.printf("Access Point started: %s\n", apSSID);
    Serial.printf("AP IP address: %s\n", WiFi.softAPIP().toString().c_str());
    Serial.printf("AP Password: %s\n", apPassword);
    
    return true;
}

void NetworkManager::stopAP() {
    if (isAPMode) {
        WiFi.softAPdisconnect(true);
        isAPMode = false;
        Serial.println("Access Point stopped");
    }
}

bool NetworkManager::connectToWiFi() {
    if (!wifiConfig.isConfigured) {
        Serial.println("No WiFi credentials configured");
        return false;
    }
    
    return connectToWiFi(wifiConfig.ssid, wifiConfig.password);
}

bool NetworkManager::connectToWiFi(const String& ssid, const String& password) {
    if (ssid.isEmpty()) {
        Serial.println("Cannot connect: empty SSID");
        return false;
    }
    
    Serial.printf("Connecting to WiFi: %s\n", ssid.c_str());
    
    // Disconnect if already connected
    if (WiFi.status() == WL_CONNECTED) {
        WiFi.disconnect();
        delay(1000);
    }
    
    WiFi.begin(ssid.c_str(), password.c_str());
    
    unsigned long startTime = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - startTime < connectionTimeout) {
        delay(500);
        Serial.print(".");
    }
    
    if (WiFi.status() == WL_CONNECTED) {
        isSTAConnected = true;
        lastConnectionAttempt = millis();
        reconnectAttempts = 0;
        
        Serial.println();
        Serial.printf("WiFi connected successfully!\n");
        Serial.printf("IP address: %s\n", WiFi.localIP().toString().c_str());
        Serial.printf("Signal strength: %d dBm\n", WiFi.RSSI());
        
        return true;
    } else {
        isSTAConnected = false;
        reconnectAttempts++;
        
        Serial.println();
        Serial.printf("WiFi connection failed (attempt %d)\n", reconnectAttempts);
        
        return false;
    }
}

void NetworkManager::disconnectWiFi() {
    if (isSTAConnected) {
        WiFi.disconnect();
        isSTAConnected = false;
        Serial.println("WiFi disconnected");
    }
}

bool NetworkManager::saveWiFiCredentials(const String& ssid, const String& password) {
    if (ssid.isEmpty()) {
        Serial.println("Cannot save empty SSID");
        return false;
    }
    
    // Test connection first
    bool testResult = connectToWiFi(ssid, password);
    
    if (testResult) {
        // Save credentials
        wifiConfig.ssid = ssid;
        wifiConfig.password = password;
        wifiConfig.isConfigured = true;
        saveWiFiConfig();
        
        Serial.printf("WiFi credentials saved and tested successfully: %s\n", ssid.c_str());
        return true;
    } else {
        Serial.printf("Failed to connect with provided credentials: %s\n", ssid.c_str());
        return false;
    }
}

void NetworkManager::clearWiFiCredentials() {
    preferences.remove("ssid");
    preferences.remove("password");
    
    wifiConfig.ssid = "";
    wifiConfig.password = "";
    wifiConfig.isConfigured = false;
    
    Serial.println("WiFi credentials cleared");
}


bool NetworkManager::pingGoogle() {
    if (!isSTAConnected || WiFi.status() != WL_CONNECTED) {
        return false;
    }
    
    WiFiClient client;
    
    // Try to connect to Google's DNS server
    if (client.connect("8.8.8.8", 53)) {
        client.stop();
        Serial.println("✅ Google DNS reachable");
        return true;
    } else {
        Serial.println("❌ Google DNS unreachable");
        return false;
    }
}

String NetworkManager::getConnectionStatus() const {
    JsonDocument doc;
    
    doc["ap_mode"] = isAPMode;
    doc["sta_connected"] = isSTAConnected;
    doc["internet_connected"] = internetConnected;
    
    if (isAPMode) {
        doc["ap_ssid"] = apSSID;
        doc["ap_ip"] = apIP.toString();
        doc["ap_clients"] = WiFi.softAPgetStationNum();
    }
    
    if (isSTAConnected) {
        doc["wifi_ssid"] = WiFi.SSID();
        doc["wifi_ip"] = WiFi.localIP().toString();
        doc["wifi_rssi"] = WiFi.RSSI();
    }
    
    doc["wifi_configured"] = wifiConfig.isConfigured;
    doc["reconnect_attempts"] = reconnectAttempts;
    doc["last_internet_check"] = lastInternetCheck;
    
    String result;
    serializeJson(doc, result);
    return result;
}

String NetworkManager::getNetworkInfo() const {
    String info = "Network Status:\n";
    
    if (isAPMode) {
        info += "- Access Point: " + String(apSSID) + "\n";
        info += "- AP IP: " + apIP.toString() + "\n";
        info += "- Connected clients: " + String(WiFi.softAPgetStationNum()) + "\n";
    }
    
    if (isSTAConnected) {
        info += "- WiFi connected to: " + WiFi.SSID() + "\n";
        info += "- WiFi IP: " + WiFi.localIP().toString() + "\n";
        info += "- Signal: " + String(WiFi.RSSI()) + " dBm\n";
    }
    
    if (internetConnected) {
        info += "- Internet: Connected\n";
    } else {
        info += "- Internet: Disconnected\n";
    }
    
    if (!isSTAConnected && !isAPMode) {
        info += "- No network connection\n";
    }
    
    return info;
}

int NetworkManager::getSignalStrength() const {
    if (isSTAConnected) {
        return WiFi.RSSI();
    }
    return 0;
}

void NetworkManager::handleTasks() {
    // Check WiFi connection status
    if (isSTAConnected && WiFi.status() != WL_CONNECTED) {
        Serial.println("WiFi connection lost");
        isSTAConnected = false;
        internetConnected = false; // Also mark internet as disconnected
        
        // Attempt to reconnect if we have credentials
        if (wifiConfig.isConfigured && millis() - lastConnectionAttempt > 30000) {
            Serial.println("Attempting to reconnect to WiFi...");
            connectToWiFi();
        }
    }
}

bool NetworkManager::enableAPSTA() {
    Serial.println("Enabling AP+STA mode...");
    
    // Set WiFi mode to AP+STA
    WiFi.mode(WIFI_AP_STA);
    esp_wifi_set_ps(WIFI_PS_NONE); // Disable power save for hotspot
    delay(100);
    
    // Setup AP
    if (!setupAP()) {
        Serial.println("Failed to setup AP in AP+STA mode");
        return false;
    }
    
    // Try to connect to WiFi if credentials are available
    if (wifiConfig.isConfigured) {
        Serial.println("Attempting to connect to saved WiFi...");
        connectToWiFi();
    }
    
    return true;
}

void NetworkManager::switchToAPMode() {
    Serial.println("Switching to AP-only mode...");
    
    if (isSTAConnected) {
        disconnectWiFi();
    }
    
    WiFi.mode(WIFI_AP);
    delay(100);
    setupAP();
}

void NetworkManager::switchToSTAMode() {
    Serial.println("Switching to STA-only mode...");
    
    if (isAPMode) {
        stopAP();
    }
    
    WiFi.mode(WIFI_STA);
    delay(100);
    
    if (wifiConfig.isConfigured) {
        connectToWiFi();
    }
}

String NetworkManager::scanNetworks() {
    Serial.println("Scanning for WiFi networks...");
    
    int networkCount = WiFi.scanNetworks();
    
    JsonDocument doc;
    JsonArray networks = doc["networks"].to<JsonArray>();
    
    if (networkCount == 0) {
        doc["count"] = 0;
        doc["message"] = "No networks found";
    } else {
        doc["count"] = networkCount;
        
        for (int i = 0; i < networkCount; i++) {
            JsonObject network = networks.add<JsonObject>();
            network["ssid"] = WiFi.SSID(i);
            network["rssi"] = WiFi.RSSI(i);
            network["encryption"] = (WiFi.encryptionType(i) == WIFI_AUTH_OPEN) ? "Open" : "Encrypted";
        }
    }
    
    WiFi.scanDelete(); // Clean up scan results
    
    String result;
    serializeJson(doc, result);
    return result;
}