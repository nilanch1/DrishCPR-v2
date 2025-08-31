#ifndef NETWORK_MANAGER_H
#define NETWORK_MANAGER_H

#include <WiFi.h>
#include <Preferences.h>
#include <ArduinoJson.h>
#include <WiFiClient.h>
#include <HTTPClient.h>

struct WiFiConfig {
    String ssid;
    String password;
    bool isConfigured;
};

class NetworkManager {
private:
    Preferences preferences;
    WiFiConfig wifiConfig;
    
    // AP Configuration
    const char* apSSID = "CPR_Trainer";
    const char* apPassword = "cprtraining";
    IPAddress apIP;
    IPAddress apGateway;
    IPAddress apSubnet;
    
    // WiFi status tracking
    bool isAPMode;
    bool isSTAConnected;
    unsigned long lastConnectionAttempt;
    unsigned long connectionTimeout;
    int reconnectAttempts;
    
    // NEW: Internet connectivity tracking
    bool internetConnected;
    unsigned long lastInternetCheck;
    const unsigned long INTERNET_CHECK_INTERVAL = 30000; // Check every 30 seconds
    
    void loadWiFiConfig();
    void saveWiFiConfig();

public:
    NetworkManager();
    ~NetworkManager();
    
    // Access Point functions
    bool setupAP();
    void stopAP();
    String getAPSSID() const { return String(apSSID); }
    IPAddress getAPIP() const { return apIP; }
    
    // Station functions
    bool connectToWiFi();
    bool connectToWiFi(const String& ssid, const String& password);
    void disconnectWiFi();
    bool isWiFiConnected() const { return isSTAConnected; }
    String getWiFiSSID() const { return wifiConfig.ssid; }
    
    // Configuration management
    bool saveWiFiCredentials(const String& ssid, const String& password);
    WiFiConfig getWiFiConfig() const { return wifiConfig; }
    bool hasWiFiCredentials() const { return wifiConfig.isConfigured; }
    void clearWiFiCredentials();
    
    // Network status
    String getConnectionStatus() const;
    String getNetworkInfo() const;
    int getSignalStrength() const;
    
    // NEW: Internet connectivity functions
    bool isInternetConnected() const { return internetConnected; }
    void checkInternetConnectivity();
    bool pingGoogle(); // Alternative ping method
    
    // Task handling
    void handleTasks();
    
    // AP+STA mode management
    bool enableAPSTA();
    void switchToAPMode();
    void switchToSTAMode();
    bool isInAPMode() const { return isAPMode; }
    
    // Network scanning
    String scanNetworks();
};

#endif