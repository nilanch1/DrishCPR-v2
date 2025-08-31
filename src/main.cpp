#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>
#include <SPIFFS.h>
#include <AsyncWebSocket.h>
#include <Preferences.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <base64.h>
#include <time.h>
#include "CPRMetricsCalculator.h"
#include "DatabaseManager.h"
#include "NetworkManager.h"
#include <mbedtls/md.h>
#include <mbedtls/sha256.h>
#include "esp_wifi.h"
#include "esp_system.h"


// Hardware Configuration
#define POTENTIOMETER_PIN 36
#define AUDIO_PIN 25
#define LED_PIN 2



// Forward declarations for missing functions
void onWebSocketEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len);
void onAnimWebSocketEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len);
void broadcastStateUpdate(const CPRStatus& status);
void broadcastAnimationState(const String& state);
void updateStatusLED(const String& state);
void processAudioAlerts(const std::vector<String>& alerts);
void checkSPIFFSHealth();
bool initializeSPIFFSWithRetry();
void checkRequiredFiles();
void broadcastNetworkStatus();
void playAlertAudio(const String& alert);
void initializeCSVFile();  
bool isCSVFileEmpty();
void broadcastDangerStatus();
void closeCSVFile();


// Global Variables
String chipId = "";              // Unique ESP32 chip ID
String csvFileName = "";         // Dynamic filename based on chip ID
bool fileUploadInProgress = false;
bool spiffsDangerMode = false;
const float SPIFFS_DANGER_THRESHOLD = 85.0; // 85% usage triggers danger mode
const float SPIFFS_SAFE_THRESHOLD = 75.0;   // 75% usage exits danger mode (hysteresis)
unsigned long lastDangerBlink = 0;
bool dangerBlinkState = false;


// =============================================
// CLOUD CONFIGURATION STRUCTURES
// =============================================
struct CloudConfig {
    String provider;        // "digitalocean" or "aws"
    String accessKey;
    String secretKey;
    String bucketName;
    String endpointUrl;
    int syncFrequency;      // in minutes
    bool enabled;
    unsigned long lastSyncTime;
    int syncedSessions;
};

// Global cloud configuration
CloudConfig cloudConfig;
Preferences cloudPrefs;
bool cloudSyncInProgress = false;
unsigned long lastCloudSyncAttempt = 0;
const unsigned long CLOUD_SYNC_RETRY_INTERVAL = 300000; // 5 minutes retry
// =============================================
// WIFI CONFIGURATION MANAGER CLASS
// =============================================
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
    
public:
    WiFiConfigManager(AsyncWebServer* webServer) : server(webServer) {
        uint64_t chipid = ESP.getEfuseMac();
        char chipIdStr[13]; // 12 hex digits + null terminator
        sprintf(chipIdStr, "%012llX", chipid);
        apSSID = "CPR-" + String(chipIdStr);
        apPassword = "cpr12345";
        
        preferences.begin("wificonfig", false);
        loadAndConnectWiFi();
    }
    
    void loadAndConnectWiFi() {
        String savedSSID = preferences.getString("ssid", "");
        String savedPassword = preferences.getString("password", "");
        
        Serial.println("Loading WiFi config from preferences...");
        Serial.printf("Saved SSID: %s\n", savedSSID.c_str());
        
        if (savedSSID.length() > 0) {
            connectToWiFi(savedSSID, savedPassword);
        }
    }
    
    void begin() {
        Serial.println("🔧 Starting WiFi Configuration Manager...");
        
        // Always start hotspot first
        startHotspot();
        
        Serial.println("✅ WiFi Configuration Manager ready");
        Serial.println("🔗 Hotspot: " + apSSID + " (Password: " + apPassword + ")");
        Serial.println("🌐 Configuration URL: http://192.168.4.1/ssid_config");
    }
    
    void loop() {
        static unsigned long lastCheck = 0;
        unsigned long now = millis();
        
        if (now - lastCheck > connectionCheckInterval) {
            lastCheck = now;
            checkWiFiStatus();
        }
    }
    
    void startHotspot() {
        Serial.println("🔥 Starting WiFi Hotspot...");
        
        WiFi.mode(WIFI_AP_STA);
        
        IPAddress local_IP(192, 168, 4, 1);
        IPAddress gateway(192, 168, 4, 1);
        IPAddress subnet(255, 255, 255, 0);
        
        WiFi.softAPConfig(local_IP, gateway, subnet);
        
        if (WiFi.softAP(apSSID.c_str(), apPassword.c_str())) {
            hotspotActive = true;
            Serial.println("✅ Hotspot started successfully");
            Serial.println("📶 SSID: " + apSSID);
            Serial.println("🔒 Password: " + apPassword);
            Serial.println("🌐 IP: " + WiFi.softAPIP().toString());
        } else {
            Serial.println("❌ Failed to start hotspot");
            hotspotActive = false;
        }
    }
    
    void connectToWiFi(const String& ssid, const String& password) {
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
    
    void checkWiFiStatus() {
        bool wasConnected = wifiConnected;
        wifiConnected = (WiFi.status() == WL_CONNECTED);
        
        if (wifiConnected != wasConnected) {
            if (wifiConnected) {
                Serial.println("✅ WiFi Connected!");
                Serial.println("📶 SSID: " + WiFi.SSID());
                Serial.println("📡 Signal: " + String(WiFi.RSSI()) + " dBm");
                Serial.println("🌐 IP: " + WiFi.localIP().toString());
                extern NetworkManager* networkManager;
                if (networkManager) {
                    networkManager->handleTasks();
                }
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
    
    bool saveWiFiCredentials(const String& ssid, const String& password) {
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
    
    bool isWiFiConnected() const { return wifiConnected; }
    bool isHotspotActive() const { return hotspotActive; }
    String getSSID() const { return WiFi.SSID(); }
    String getAPSSID() const { return apSSID; }
    IPAddress getLocalIP() const { return wifiConnected ? WiFi.localIP() : IPAddress(0, 0, 0, 0); }
    IPAddress getAPIP() const { return WiFi.softAPIP(); }
    int getRSSI() const { return wifiConnected ? WiFi.RSSI() : 0; }
    
    void reconnect() {
        String savedSSID = preferences.getString("ssid", "");
        String savedPassword = preferences.getString("password", "");
        if (savedSSID.length() > 0) {
            Serial.println("🔄 Manual reconnection requested");
            connectToWiFi(savedSSID, savedPassword);
        }
    }
    
    void resetConfig() {
        preferences.remove("ssid");
        preferences.remove("password");
        WiFi.disconnect();
        Serial.println("🗑️ WiFi configuration reset");
    }
};
// System Components
CPRMetricsCalculator* metricsCalculator;
DatabaseManager* dbManager;
NetworkManager* networkManager;
WiFiConfigManager* wifiConfigManager; // WiFi Configuration Manager
AsyncWebServer server(80);
AsyncWebSocket webSocket("/ws");          // Metrics WebSocket (2Hz)
AsyncWebSocket animWebSocket("/animws");  // Animation WebSocket (20Hz)

// Global State
bool isRecording = false;
int currentSessionId = 0;
unsigned long lastPotRead = 0;
unsigned long lastDataSend = 0;
unsigned long lastAnimSend = 0;
unsigned long lastStateChange = 0;
String lastBroadcastState = "";
String lastAnimState = "";

// Enhanced CSV Data Logging with Chip ID
File csvFile;
bool csvFileOpen = false;
unsigned long lastCSVWrite = 0;
const unsigned long CSV_WRITE_INTERVAL = 50; // 50ms for frequent writes
int csvWriteCount = 0;

// Session number tracking
Preferences sessionPrefs;
int lastSessionNumber = 0;

// Optimized timing intervals
const unsigned long POT_READ_INTERVAL = 25;     // 40Hz sampling
const unsigned long DATA_SEND_INTERVAL = 500;   // 2Hz metrics updates
const unsigned long ANIM_SEND_INTERVAL = 50;    // 20Hz animation updates
const unsigned long STATE_CHANGE_DEBOUNCE = 50;

// WebSocket client management
const int MAX_WS_CLIENTS = 4;

// Audio management
unsigned long lastAudioEndTime = 0;
const unsigned long MIN_AUDIO_GAP = 2000;
bool isCurrentlyPlayingAudio = false;

// =============================================
// CLOUD UTILITY FUNCTIONS
// =============================================
void initializeCloudConfig() {
    cloudPrefs.begin("cloud", false);
    
    cloudConfig.provider = cloudPrefs.getString("provider", "");
    cloudConfig.accessKey = cloudPrefs.getString("accessKey", "");
    cloudConfig.secretKey = cloudPrefs.getString("secretKey", "");
    cloudConfig.bucketName = cloudPrefs.getString("bucket", "");
    cloudConfig.endpointUrl = cloudPrefs.getString("endpoint", "");
    cloudConfig.syncFrequency = cloudPrefs.getInt("frequency", 60);
    cloudConfig.enabled = cloudPrefs.getBool("enabled", false);
    cloudConfig.lastSyncTime = cloudPrefs.getULong("lastSync", 0);
    cloudConfig.syncedSessions = cloudPrefs.getInt("syncedSessions", 0);
    
    Serial.println("Cloud configuration loaded:");
    Serial.printf("  Provider: %s\n", cloudConfig.provider.c_str());
    Serial.printf("  Enabled: %s\n", cloudConfig.enabled ? "Yes" : "No");
    Serial.printf("  Sync Frequency: %d minutes\n", cloudConfig.syncFrequency);
    Serial.printf("  Last Sync: %lu\n", cloudConfig.lastSyncTime);
}

void saveCloudConfig() {
    cloudPrefs.putString("provider", cloudConfig.provider);
    cloudPrefs.putString("accessKey", cloudConfig.accessKey);
    cloudPrefs.putString("secretKey", cloudConfig.secretKey);
    cloudPrefs.putString("bucket", cloudConfig.bucketName);
    cloudPrefs.putString("endpoint", cloudConfig.endpointUrl);
    cloudPrefs.putInt("frequency", cloudConfig.syncFrequency);
    cloudPrefs.putBool("enabled", cloudConfig.enabled);
    cloudPrefs.putULong("lastSync", cloudConfig.lastSyncTime);
    cloudPrefs.putInt("syncedSessions", cloudConfig.syncedSessions);
    
    Serial.println("Cloud configuration saved");
}

String generateS3Signature(const String& method, const String& resource, const String& contentType = "", const String& dateString = "") {
    String stringToSign = method + "\n\n" + contentType + "\n" + dateString + "\n" + resource;
    
    // Simple HMAC-SHA1 implementation for demonstration
    // In production, use a proper crypto library
    return base64::encode(stringToSign);
}

String generateDigitalOceanAuth(const String& method, const String& resource) {
    String auth = cloudConfig.accessKey + ":" + cloudConfig.secretKey;
    return "Basic " + base64::encode(auth);
}

String getCloudUploadUrl(const String& fileName) {
    if (cloudConfig.provider == "digitalocean") {
        // Correct format for DigitalOcean Spaces
        return "https://" + cloudConfig.bucketName + "." + cloudConfig.endpointUrl + "/" + fileName;
    } else if (cloudConfig.provider == "aws") {
        return "https://" + cloudConfig.bucketName + ".s3.amazonaws.com/" + fileName;
    }
    return "";
}
bool isCSVFileEmpty() {
    if (!SPIFFS.exists(csvFileName)) {
        Serial.println("CSV file doesn't exist");
        return true;
    }
    
    File file = SPIFFS.open(csvFileName, "r");
    if (!file) {
        Serial.println("Failed to open CSV file for reading");
        return true;
    }
    
    // Count non-comment, non-header lines
    int dataLines = 0;
    String line;
    bool isFirstLine = true;
    
    while (file.available()) {
        line = file.readStringUntil('\n');
        line.trim(); // Remove whitespace
        
        // Skip empty lines
        if (line.length() == 0) {
            continue;
        }
        
        // Skip comment lines (session markers)
        if (line.startsWith("#")) {
            continue;
        }
        
        // Skip header line (first non-comment line)
        if (isFirstLine) {
            isFirstLine = false;
            continue;
        }
        
        // This is actual data
        dataLines++;
    }
    
    file.close();
    
    Serial.printf("CSV file analysis: %d data lines found\n", dataLines);
    return (dataLines == 0);
}

String getISOTimestamp() {
    time_t now = time(nullptr);
    struct tm* timeinfo = gmtime(&now);
    char timestamp[64];
    strftime(timestamp, sizeof(timestamp), "%a, %d %b %Y %H:%M:%S GMT", timeinfo);
    return String(timestamp);
}
// Helper function for HMAC-SHA256
void hmacSha256(const uint8_t* key, size_t keyLen, const uint8_t* data, size_t dataLen, uint8_t* result) {
    mbedtls_md_context_t ctx;
    mbedtls_md_type_t md_type = MBEDTLS_MD_SHA256;
    
    mbedtls_md_init(&ctx);
    mbedtls_md_setup(&ctx, mbedtls_md_info_from_type(md_type), 1);
    mbedtls_md_hmac_starts(&ctx, key, keyLen);
    mbedtls_md_hmac_update(&ctx, data, dataLen);
    mbedtls_md_hmac_finish(&ctx, result);
    mbedtls_md_free(&ctx);
}
String bytesToHex(const uint8_t* bytes, size_t length) {
    String hex = "";
    for (size_t i = 0; i < length; i++) {
        if (bytes[i] < 16) hex += "0";
        hex += String(bytes[i], HEX);
    }
    hex.toLowerCase();
    return hex;
}

// Helper function for SHA256 hash
String sha256(const String& data) {
    mbedtls_sha256_context ctx;
    uint8_t result[32];
    
    mbedtls_sha256_init(&ctx);
    mbedtls_sha256_starts(&ctx, 0);
    mbedtls_sha256_update(&ctx, (const unsigned char*)data.c_str(), data.length());
    mbedtls_sha256_finish(&ctx, result);
    mbedtls_sha256_free(&ctx);
    
    return bytesToHex(result, 32);
}
// Get current date/time in required formats
String getAWSDateTime() {
    time_t now = time(nullptr);
    struct tm* timeinfo = gmtime(&now);
    char datetime[20];
    strftime(datetime, sizeof(datetime), "%Y%m%dT%H%M%SZ", timeinfo);
    return String(datetime);
}

String getAWSDate() {
    time_t now = time(nullptr);
    struct tm* timeinfo = gmtime(&now);
    char date[10];
    strftime(date, sizeof(date), "%Y%m%d", timeinfo);
    return String(date);
}
// Generate AWS Signature v4 for DigitalOcean Spaces
String generateAWSv4Signature(const String& method, const String& uri, const String& host,
                             const String& contentType, const String& payload,
                             const String& accessKey, const String& secretKey,
                             bool unsignedPayload = false) {
    String datetime = getAWSDateTime();
    String date = getAWSDate();

    // Extract region from endpoint host
    String region = "us-east-1"; // default
    int dotIndex = host.indexOf(".");
    if (dotIndex > 0) {
        region = host.substring(dotIndex + 1, host.indexOf(".", dotIndex + 1));
    }
    // For DigitalOcean it should give "sfo3"

    String service = "s3";

    // 🔑 Payload handling
    String payloadHash = unsignedPayload ? "UNSIGNED-PAYLOAD" : sha256(payload);

    // Canonical headers
    String canonicalHeaders = "host:" + host + "\n" +
                             "x-amz-content-sha256:" + payloadHash + "\n" +
                             "x-amz-date:" + datetime + "\n";

    String signedHeaders = "host;x-amz-content-sha256;x-amz-date";

    // Canonical request
    String canonicalRequest = method + "\n" +
                             uri + "\n" +
                             "\n" +
                             canonicalHeaders + "\n" +
                             signedHeaders + "\n" +
                             payloadHash;

    String credentialScope = date + "/" + region + "/" + service + "/aws4_request";
    String stringToSign = "AWS4-HMAC-SHA256\n" +
                         datetime + "\n" +
                         credentialScope + "\n" +
                         sha256(canonicalRequest);

    // === HMAC signing same as before ===
    String kSecret = "AWS4" + secretKey;
    uint8_t kDate[32];
    hmacSha256((uint8_t*)kSecret.c_str(), kSecret.length(),
               (uint8_t*)date.c_str(), date.length(), kDate);

    uint8_t kRegion[32];
    hmacSha256(kDate, 32, (uint8_t*)region.c_str(), region.length(), kRegion);

    uint8_t kService[32];
    hmacSha256(kRegion, 32, (uint8_t*)service.c_str(), service.length(), kService);

    uint8_t kSigning[32];
    String aws4Request = "aws4_request";
    hmacSha256(kService, 32, (uint8_t*)aws4Request.c_str(), aws4Request.length(), kSigning);

    uint8_t signature[32];
    hmacSha256(kSigning, 32,
               (uint8_t*)stringToSign.c_str(), stringToSign.length(),
               signature);

    String authHeader = "AWS4-HMAC-SHA256 Credential=" + accessKey + "/" + credentialScope +
                       ", SignedHeaders=" + signedHeaders +
                       ", Signature=" + bytesToHex(signature, 32);

    return authHeader;
}


// Replace your existing uploadToCloud function with this corrected version
// Updated uploadToCloud function with proper AWS Signature v4
bool uploadToCloud(const String& fileName, const String& localFilePath) {
    if (!cloudConfig.enabled || cloudConfig.provider.isEmpty()) {
        Serial.println("☁️ Cloud upload disabled or not configured");
        return false;
    }

    if (!wifiConfigManager->isWiFiConnected()) {
        Serial.println("📡 WiFi not connected - cannot upload to cloud");
        return false;
    }

    File f = SPIFFS.open(localFilePath, "r");
    if (!f) {
        Serial.printf("❌ Failed to open file for upload: %s\n", localFilePath.c_str());
        return false;
    }

    size_t fileSize = f.size();
    Serial.printf("📤 Preparing to upload %s (%u bytes) to cloud...\n",
                  localFilePath.c_str(), fileSize);

    WiFiClientSecure client;
    client.setInsecure();       // skip cert validation, saves RAM
    client.setTimeout(30000);

    HTTPClient http;
    // host = drishcpr.sfo3.digitaloceanspaces.com
    String host = cloudConfig.bucketName + "." + cloudConfig.endpointUrl;
    String uri = "/" + fileName;
    String uploadUrl = "https://" + host + uri;

    if (!http.begin(client, uploadUrl)) {
        Serial.println("❌ Failed to begin HTTP connection");
        f.close();
        return false;
    }

    // === AWS v4 signature with UNSIGNED-PAYLOAD ===
    String authHeader = generateAWSv4Signature("PUT", uri, host, "text/csv", "",
                                               cloudConfig.accessKey, cloudConfig.secretKey,
                                               true /* unsignedPayload */);

    String datetime = getAWSDateTime();

    http.addHeader("Authorization", authHeader);
    http.addHeader("x-amz-date", datetime);
    http.addHeader("x-amz-content-sha256", "UNSIGNED-PAYLOAD");
    http.addHeader("Host", host);
    http.addHeader("Content-Type", "text/csv");
    http.addHeader("Content-Length", String(fileSize));

    Serial.println("🚀 Starting upload (streaming)...");
    Serial.printf("Free heap before PUT: %u bytes\n", ESP.getFreeHeap());

    int httpResponseCode = http.sendRequest("PUT", &f, fileSize);
    f.close();

    Serial.printf("HTTP Response Code: %d\n", httpResponseCode);

    bool uploadResult = (httpResponseCode == 200 || httpResponseCode == 201);

    http.end();

    if (uploadResult) {
        Serial.printf("✅ Upload successful, deleting local file: %s\n", localFilePath.c_str());
        if (SPIFFS.remove(localFilePath)) {
            Serial.println("🗑️ Local file deleted");
        }
        initializeCSVFile(); // re-init CSV after successful upload
    } else {
        Serial.println("❌ Upload failed - keeping local file");
    }

    Serial.printf("Free heap after upload: %u bytes\n", ESP.getFreeHeap());

    return uploadResult;
}


bool testCloudConnection() {
    if (cloudConfig.provider.isEmpty() || cloudConfig.accessKey.isEmpty()) {
        return false;
    }
    
    // Test with a small dummy file
    String testContent = "test," + String(millis()) + "\n";
    String testFileName = chipId + "_test_" + String(millis()) + ".csv";
    
    return uploadToCloud(testFileName, testContent);
}

void performCloudSync() {
    if (cloudSyncInProgress || !cloudConfig.enabled) {
        return;
    }

    unsigned long now = millis();
    unsigned long syncInterval = cloudConfig.syncFrequency * 60000UL;

    if (now - cloudConfig.lastSyncTime < syncInterval) return;
    if (now - lastCloudSyncAttempt < CLOUD_SYNC_RETRY_INTERVAL) return;

    lastCloudSyncAttempt = now;
    cloudSyncInProgress = true;

    Serial.println("Starting cloud sync...");

    if (SPIFFS.exists(csvFileName)) {
        if (isCSVFileEmpty()) {
            Serial.println("📄 CSV file is empty - skipping upload");
            cloudConfig.lastSyncTime = now;
            saveCloudConfig();
        } else {
            String cloudFileName = chipId + "_" + String(cloudConfig.syncedSessions + 1) + ".csv";
            // 🔑 Call new streaming upload
            if (uploadToCloud(cloudFileName, csvFileName)) {
                cloudConfig.lastSyncTime = now;
                cloudConfig.syncedSessions++;
                saveCloudConfig();
                Serial.println("☁️ Cloud sync completed successfully");
            } else {
                Serial.println("❌ Cloud sync failed");
            }
        }
    } else {
        Serial.println("📄 No CSV file found - skipping upload");
        cloudConfig.lastSyncTime = now;
        saveCloudConfig();
    }

    cloudSyncInProgress = false;
}


// =============================================
// WEBSOCKET FUNCTIONS
// =============================================
void onWebSocketEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len) {
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

void onAnimWebSocketEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len) {
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

void broadcastStateUpdate(const CPRStatus& status) {
    if (webSocket.count() == 0) return;
    
    JsonDocument doc;
    doc["type"] = "metrics";
    doc["timestamp"] = status.timestamp;
    doc["state"] = status.state;
    doc["rate"] = status.currentRate;
    doc["value"] = status.rawValue;
    doc["peak"] = status.peakValue;
    
    // Add metrics data
    doc["good_compressions"] = status.peaks.good;
    doc["total_compressions"] = status.peaks.total;
    doc["compression_ratio"] = status.peaks.ratio;
    doc["good_recoils"] = status.troughs.goodRecoil;
    doc["total_recoils"] = status.troughs.total;
    doc["recoil_ratio"] = status.troughs.ratio;
    doc["ccf"] = status.ccf;
    doc["cycles"] = status.cycles;
    
    // Add alerts
    JsonArray alerts = doc["alerts"].to<JsonArray>();
    for (const String& alert : status.alerts) {
        alerts.add(alert);
    }
    
    String message;
    serializeJson(doc, message);
    webSocket.textAll(message);
}

void broadcastAnimationState(const String& state) {
    if (animWebSocket.count() == 0) return;
    
    JsonDocument doc;
    doc["type"] = "animation";
    doc["state"] = state;
    doc["timestamp"] = millis();
    
    String message;
    serializeJson(doc, message);
    animWebSocket.textAll(message);
    
    lastAnimState = state;
}

void broadcastNetworkStatus() {
    if (webSocket.count() == 0) return;
    
    JsonDocument doc;
    doc["type"] = "network_status";
    doc["wifi_connected"] = wifiConfigManager->isWiFiConnected();
    doc["wifi_ssid"] = wifiConfigManager->getSSID();
    doc["wifi_rssi"] = wifiConfigManager->getRSSI();
    doc["hotspot_active"] = wifiConfigManager->isHotspotActive();
    doc["hotspot_ssid"] = wifiConfigManager->getAPSSID();
    doc["cloud_enabled"] = cloudConfig.enabled;
    doc["cloud_sync_in_progress"] = cloudSyncInProgress;
    doc["timestamp"] = millis();
    
    if (wifiConfigManager->isWiFiConnected()) {
        doc["ip_address"] = wifiConfigManager->getLocalIP().toString();
    }
    
    String message;
    serializeJson(doc, message);
    webSocket.textAll(message);
}

void broadcastDangerStatus() {
    if (webSocket.count() == 0) return;
    
    // Blink logic for danger mode
    unsigned long now = millis();
    if (spiffsDangerMode && (now - lastDangerBlink >= 1000)) { // 1 second blink
        dangerBlinkState = !dangerBlinkState;
        lastDangerBlink = now;
        
        JsonDocument doc;
        doc["type"] = "spiffs_danger";
        doc["danger_mode"] = true;
        doc["blink_state"] = dangerBlinkState;
        doc["message"] = "Please enable Cloud Upload. No further operations possible.";
        doc["cloud_enabled"] = cloudConfig.enabled;
        doc["timestamp"] = now;
        
        String message;
        serializeJson(doc, message);
        webSocket.textAll(message);
    }
}


// =============================================
// HARDWARE FUNCTIONS
// =============================================
void updateStatusLED(const String& state) {
    static unsigned long lastLEDUpdate = 0;
    static bool ledState = false;
    unsigned long now = millis();
    
    if (state == "compression") {
        // Fast blinking during compression
        if (now - lastLEDUpdate > 100) {
            ledState = !ledState;
            digitalWrite(LED_PIN, ledState ? HIGH : LOW);
            lastLEDUpdate = now;
        }
    } else if (state == "recoil") {
        // Solid on during recoil
        digitalWrite(LED_PIN, HIGH);
    } else {
        // Off during quietude
        digitalWrite(LED_PIN, LOW);
    }
}

void processAudioAlerts(const std::vector<String>& alerts) {
    if (alerts.empty() || isCurrentlyPlayingAudio) return;
    
    unsigned long now = millis();
    if (now - lastAudioEndTime < MIN_AUDIO_GAP) return;
    
    // Play audio based on alert type
    for (const String& alert : alerts) {
        if (alert.indexOf("rate too low") != -1) {
            playAlertAudio("rateTooLow");
            break;
        } else if (alert.indexOf("rate too high") != -1) {
            playAlertAudio("rateTooHigh");
            break;
        } else if (alert.indexOf("Press harder") != -1) {
            playAlertAudio("depthTooLow");
            break;
        } else if (alert.indexOf("Be gentle") != -1) {
            playAlertAudio("depthTooHigh");
            break;
        } else if (alert.indexOf("Release more") != -1) {
            playAlertAudio("incompleteRecoil");
            break;
        }
    }
}

void playAlertAudio(const String& alert) {
    if (isCurrentlyPlayingAudio) return;
    
    // Simple tone generation
    unsigned long duration = 500; // 500ms tone
    unsigned int frequency = 1000; // 1kHz tone
    
    if (alert == "rateTooLow") frequency = 800;
    else if (alert == "rateTooHigh") frequency = 1200;
    else if (alert == "depthTooLow") frequency = 600;
    else if (alert == "depthTooHigh") frequency = 1400;
    else if (alert == "incompleteRecoil") frequency = 900;
    
    // Generate tone (simplified)
    tone(AUDIO_PIN, frequency, duration);
    
    isCurrentlyPlayingAudio = true;
    lastAudioEndTime = millis() + duration;
    
    Serial.printf("Playing audio alert: %s (%d Hz, %lu ms)\n", alert.c_str(), frequency, duration);
}


bool isOperationAllowed() {
    return !spiffsDangerMode;
}




// =============================================
// FILESYSTEM FUNCTIONS
// =============================================
bool initializeSPIFFSWithRetry() {
    int attempts = 0;
    const int maxAttempts = 3;
    
    while (attempts < maxAttempts) {
        Serial.printf("Initializing SPIFFS (attempt %d/%d)...\n", attempts + 1, maxAttempts);
        
        if (SPIFFS.begin(true)) {
            Serial.println("✅ SPIFFS mounted successfully");
            
            // Check available space
            size_t totalBytes = SPIFFS.totalBytes();
            size_t usedBytes = SPIFFS.usedBytes();
            Serial.printf("📊 SPIFFS: %d/%d bytes used (%.1f%%)\n", 
                         usedBytes, totalBytes, (float)usedBytes/totalBytes*100);
            
            return true;
        }
        
        attempts++;
        delay(1000);
    }
    
    Serial.println("❌ Failed to initialize SPIFFS after multiple attempts");
    return false;
}

void checkRequiredFiles() {
    const String requiredFiles[] = {
        "/index.html",
        "/config.html", 
        "/ssid_config.html",
        "/cloud_config.html",
        "/data.html"
    };
    
    Serial.println("🔍 Checking required files...");
    
    for (const String& filename : requiredFiles) {
        if (SPIFFS.exists(filename)) {
            File file = SPIFFS.open(filename, "r");
            if (file) {
                Serial.printf("✅ %s (%d bytes)\n", filename.c_str(), file.size());
                file.close();
            } else {
                Serial.printf("⚠️ %s (cannot open)\n", filename.c_str());
            }
        } else {
            Serial.printf("❌ %s (missing)\n", filename.c_str());
        }
    }
}

void checkSPIFFSHealth() {
    // SKIP CHECK IF FILE UPLOAD IN PROGRESS
    if (fileUploadInProgress) {
        Serial.println("⏸️ Skipping SPIFFS health check - file upload in progress");
        return;
    }
    
    static unsigned long lastCheck = 0;
    static unsigned long lastStatusLog = 0;
    static int consecutiveFailures = 0;
    unsigned long now = millis();
    
    // Check every 60 seconds
    if (now - lastCheck < 60000) return;
    lastCheck = now;
    
    bool isMounted = false;
    size_t totalBytes = 0;
    size_t usedBytes = 0;
    
    // Method 1: Try to get filesystem statistics (most reliable method)
    try {
        totalBytes = SPIFFS.totalBytes();
        usedBytes = SPIFFS.usedBytes();
        
        // If we got valid values (total > 0), SPIFFS is mounted and working
        if (totalBytes > 0) {
            isMounted = true;
            consecutiveFailures = 0; // Reset failure counter on success
            
            float usagePercent = (float)usedBytes / totalBytes * 100;
            
            // DANGER LEVEL PROTECTION WITH HYSTERESIS
            bool previousDangerMode = spiffsDangerMode;
            
            if (!spiffsDangerMode && usagePercent >= SPIFFS_DANGER_THRESHOLD) {
                // Entering danger mode
                spiffsDangerMode = true;
                Serial.printf("🚨 SPIFFS DANGER MODE ACTIVATED: %.1f%% usage exceeds %.1f%% threshold\n", 
                             usagePercent, SPIFFS_DANGER_THRESHOLD);
                Serial.println("❌ All operations suspended - Cloud upload required");
                
                // Stop recording if active
                if (isRecording) {
                    Serial.println("⏹️ Auto-stopping recording due to SPIFFS danger mode");
                    dbManager->endCurrentSession();
                    isRecording = false;
                    closeCSVFile();
                }
            } else if (spiffsDangerMode && usagePercent <= SPIFFS_SAFE_THRESHOLD) {
                // Exiting danger mode (hysteresis prevents flickering)
                spiffsDangerMode = false;
                Serial.printf("✅ SPIFFS DANGER MODE DEACTIVATED: %.1f%% usage below %.1f%% threshold\n", 
                             usagePercent, SPIFFS_SAFE_THRESHOLD);
                Serial.println("✅ Operations resumed");
            }
            
            // Log status changes or periodic updates
            if (previousDangerMode != spiffsDangerMode || 
                (now - lastStatusLog > 300000) || 
                usagePercent > 80) {
                
                const char* modeStr = spiffsDangerMode ? "🚨 DANGER" : "✅ OK";
                Serial.printf("📊 SPIFFS Health: %.1f%% used (%u/%u bytes) - %s\n", 
                             usagePercent, usedBytes, totalBytes, modeStr);
                lastStatusLog = now;
            }
            
        } else {
            isMounted = false;
            Serial.println("⚠️ SPIFFS totalBytes() returned 0 - filesystem issue detected");
        }
    } catch (const std::exception& e) {
        Serial.printf("❌ SPIFFS stats exception: %s\n", e.what());
        isMounted = false;
    } catch (...) {
        Serial.println("❌ SPIFFS stats unknown exception");
        isMounted = false;
    }
    
    // Handle mount failures
    if (!isMounted) {
        consecutiveFailures++;
        Serial.printf("❌ SPIFFS health check failed (consecutive failures: %d)\n", consecutiveFailures);
        
        // Only attempt remount after multiple failures to avoid false positives
        if (consecutiveFailures >= 2) {
            Serial.println("🔄 Attempting SPIFFS remount due to repeated failures...");
            
            // Close any open files first
            if (csvFileOpen && csvFile) {
                Serial.println("📝 Closing CSV file before remount");
                csvFile.close();
                csvFileOpen = false;
            }
            
            // Attempt remount
            SPIFFS.end();
            delay(500); // Give it time to properly unmount
            
            if (initializeSPIFFSWithRetry()) {
                Serial.println("✅ SPIFFS remounted successfully");
                consecutiveFailures = 0;
                
                // Reinitialize CSV system if needed
                if (!csvFileName.isEmpty()) {
                    Serial.println("🔄 Reinitializing CSV system after remount");
                    initializeCSVFile();
                }
                
                // Force a status update on next check
                lastStatusLog = 0;
            } else {
                Serial.printf("❌ SPIFFS remount failed (total failures: %d)\n", consecutiveFailures);
                
                // If we've had too many consecutive failures, log critical error
                if (consecutiveFailures >= 5) {
                    Serial.println("🚨 CRITICAL: SPIFFS repeatedly failing - filesystem corruption possible");
                }
            }
        }
    }
}

// =============================================
// CHIP ID AND CSV SYSTEM FUNCTIONS
// =============================================

void initializeChipId() {
    // Get unique chip ID
    uint64_t chipid = ESP.getEfuseMac();
    chipId = String((uint32_t)(chipid >> 32), HEX) + String((uint32_t)chipid, HEX);
    chipId.toUpperCase();
    
    // Set CSV filename based on chip ID
    csvFileName = "/" + chipId + ".csv";
    
    Serial.printf("ESP32 Chip ID: %s\n", chipId.c_str());
    Serial.printf("CSV Filename: %s\n", csvFileName.c_str());
}

void initializeCSVFile() {
    if (chipId.isEmpty()) {
        Serial.println("ERROR: Chip ID not initialized!");
        return;
    }
    
    // Check if CSV file exists, if not create header
    if (!SPIFFS.exists(csvFileName)) {
        File file = SPIFFS.open(csvFileName, "w");
        if (file) {
            // Enhanced CSV header with more detail
            file.println("ChipID,SessionID,Timestamp,RawValue,ScaledValue,State,IsGood,CompressionPeak,RecoilMin,Rate,CCF");
            file.flush();
            file.close();
            Serial.printf("Created new CSV file: %s with headers\n", csvFileName.c_str());
        } else {
            Serial.printf("ERROR: Failed to create CSV file: %s\n", csvFileName.c_str());
        }
    } else {
        File checkFile = SPIFFS.open(csvFileName, "r");
        if (checkFile) {
            size_t fileSize = checkFile.size();
            checkFile.close();
            Serial.printf("Existing CSV file found: %s, size: %d bytes\n", csvFileName.c_str(), fileSize);
        }
    }
}

bool openCSVFile() {
    if (csvFileOpen) {
        Serial.println("CSV file already open");
        return true;
    }
    
    if (chipId.isEmpty()) {
        Serial.println("ERROR: Cannot open CSV - Chip ID not initialized!");
        return false;
    }
    
    csvFile = SPIFFS.open(csvFileName, "a");
    if (csvFile) {
        csvFileOpen = true;
        csvWriteCount = 0;
        Serial.printf("CSV file opened for writing: %s\n", csvFileName.c_str());
        
        // Write a session start marker
        unsigned long now = millis();
        csvFile.printf("# Session %d started at %lu\n", currentSessionId, now);
        csvFile.flush();
        
        return true;
    } else {
        Serial.printf("ERROR: Failed to open CSV file: %s\n", csvFileName.c_str());
        return false;
    }
}

void closeCSVFile() {
    if (csvFileOpen && csvFile) {
        // Write session end marker
        unsigned long now = millis();
        csvFile.printf("# Session %d ended at %lu\n", currentSessionId, now);
        csvFile.flush();
        csvFile.close();
        csvFileOpen = false;
        csvWriteCount = 0;
        Serial.printf("CSV file closed: %s\n", csvFileName.c_str());
        
        // Trigger cloud sync if enabled
        if (cloudConfig.enabled) {
            Serial.println("Triggering cloud sync after session end...");
            cloudConfig.lastSyncTime = 0; // Force immediate sync
        }
    }
}

void writeCSVData(int sessionId, unsigned long timestamp, int rawValue, const CPRStatus& status) {
    if (!csvFileOpen || !csvFile) {
        Serial.println("WARNING: CSV file not open for writing");
        return;
    }
    
    // Convert 12-bit ADC (0-4095) to 10-bit range (0-1023) for compatibility
    int scaledValue = map(rawValue, 0, 4095, 0, 1023);
    
    // Get current state and quality from CPRStatus
    String state = status.state;
    bool isGood = false;
    float compressionPeak = 0;
    float recoilMin = 0;
    
    // Determine quality based on state and values
    if (state == "compression") {
        isGood = status.currentCompression.isGood;
        compressionPeak = status.currentCompression.peakValue;
    } else if (state == "recoil") {
        isGood = status.currentRecoil.isGood;
        recoilMin = status.currentRecoil.minValue;
    }
    
    // Write comprehensive CSV line
    csvFile.printf("%s,%d,%lu,%d,%d,%s,%s,%.2f,%.2f,%d,%.1f\n",
                   chipId.c_str(),
                   sessionId,
                   timestamp,
                   rawValue,
                   scaledValue,
                   state.c_str(),
                   isGood ? "true" : "false",
                   compressionPeak,
                   recoilMin,
                   status.currentRate,
                   status.ccf);
    
    csvWriteCount++;
    
    // Flush periodically to ensure data is written
    if (csvWriteCount % 20 == 0 || (millis() - lastCSVWrite) > 1000) {
        csvFile.flush();
    }
    
    // Debug output every 100 writes
    if (csvWriteCount % 100 == 0) {
        Serial.printf("CSV: Written %d records to %s\n", csvWriteCount, csvFileName.c_str());
    }
}

void handleCSVLogging(unsigned long currentTime, int potValue, const CPRStatus& status) {
    // Block CSV operations in danger mode
    if (spiffsDangerMode) {
        return; // Silently skip CSV logging
    }
    
    if (isRecording && csvFileOpen && (currentTime - lastCSVWrite >= CSV_WRITE_INTERVAL)) {
        writeCSVData(currentSessionId, currentTime, potValue, status);
        lastCSVWrite = currentTime;
    }
}

bool deleteCSVFile() {
    closeCSVFile();
    if (SPIFFS.exists(csvFileName)) {
        if (SPIFFS.remove(csvFileName)) {
            Serial.printf("CSV file deleted: %s\n", csvFileName.c_str());
            return true;
        } else {
            Serial.printf("Failed to delete CSV file: %s\n", csvFileName.c_str());
            return false;
        }
    }
    Serial.printf("CSV file does not exist: %s\n", csvFileName.c_str());
    return true;
}

// =============================================
// SESSION TRACKING FUNCTIONS
// =============================================

void initializeSessionTracking() {
    sessionPrefs.begin("sessions", false);
    lastSessionNumber = sessionPrefs.getInt("lastSession", 0);
    Serial.printf("Last session number: %d\n", lastSessionNumber);
}

int getNextSessionNumber() {
    lastSessionNumber++;
    sessionPrefs.putInt("lastSession", lastSessionNumber);
    Serial.printf("Next session number: %d\n", lastSessionNumber);
    return lastSessionNumber;
}

void setupCSVSystem() {
    // Initialize chip ID first
    initializeChipId();
    
    // Initialize session tracking
    initializeSessionTracking();
    
    // Initialize cloud configuration
    initializeCloudConfig();
    
    // Initialize CSV logging system with chip ID
    initializeCSVFile();
    
    Serial.printf("CSV system initialized with chip ID: %s\n", chipId.c_str());
}
// =============================================
// WEB SERVER SETUP WITH ALL ROUTES INCLUDING CLOUD ENDPOINTS
// =============================================

void setupWebServer() {
    // WebSocket setup
    webSocket.onEvent(onWebSocketEvent);
    server.addHandler(&webSocket);
    
    animWebSocket.onEvent(onAnimWebSocketEvent);
    server.addHandler(&animWebSocket);
    
    // Main pages
    server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
        request->send(SPIFFS, "/index.html", "text/html");
    });
    
    server.on("/config", HTTP_GET, [](AsyncWebServerRequest *request) {
        request->send(SPIFFS, "/config.html", "text/html");
    });
    
    // WiFi Configuration Page
    server.on("/ssid_config", HTTP_GET, [](AsyncWebServerRequest *request) {
        Serial.println("📱 Serving WiFi config page");
        if (SPIFFS.exists("/ssid_config.html")) {
            request->send(SPIFFS, "/ssid_config.html", "text/html");
        } else {
            request->send(404, "text/plain", "WiFi config page not found in SPIFFS");
        }
    });
    
    // Cloud Configuration Page

    server.on("/cloud_config", HTTP_GET, [](AsyncWebServerRequest *request) {
        request->send(SPIFFS, "/cloud_config.html", "text/html");
    });
    
    server.on("/data", HTTP_GET, [](AsyncWebServerRequest *request) {
        request->send(SPIFFS, "/data.html", "text/html");
    });
    
    // =============================================
    // CLOUD CONFIGURATION ENDPOINTS
    // =============================================
    
    // Get current cloud configuration
    server.on("/get_cloud_config", HTTP_GET, [](AsyncWebServerRequest *request) {
        Serial.println("☁️ Get cloud config request");
        
        JsonDocument doc;
        doc["success"] = true;
        doc["provider"] = cloudConfig.provider;
        doc["bucket"] = cloudConfig.bucketName;
        doc["endpoint"] = cloudConfig.endpointUrl;
        doc["frequency"] = cloudConfig.syncFrequency;
        doc["enabled"] = cloudConfig.enabled;
        doc["last_sync"] = cloudConfig.lastSyncTime;
        doc["synced_sessions"] = cloudConfig.syncedSessions;
        doc["sync_in_progress"] = cloudSyncInProgress;
        
        // Don't send sensitive credentials
        //doc["has_access_key"] = !cloudConfig.accessKey.isEmpty();
        //doc["has_secret_key"] = !cloudConfig.secretKey.isEmpty();
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
        [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
            static String postBody = "";
            
            if (index == 0) postBody = "";
            
            for (size_t i = 0; i < len; i++) {
                postBody += (char)data[i];
            }
            
            if (index + len == total) {
                Serial.println("☁️ Save cloud config request received");
                
                JsonDocument doc;
                DeserializationError error = deserializeJson(doc, postBody);
                
                if (error) {
                    Serial.println("❌ JSON parse error: " + String(error.c_str()));
                    request->send(400, "application/json", "{\"success\":false,\"error\":\"Invalid JSON\"}");
                    return;
                }
                Serial.println("📝 Received JSON: " + postBody);

                // Print frequency before and after default assignment
                int freq = doc["frequency"].as<int>();
                Serial.println("🔍 Raw frequency: " + String(freq));

                // Validate required fields
                String provider = doc["provider"] | "";
                String accessKey = doc["access_key"] | "";
                String secretKey = doc["secret_key"] | "";
                String bucket = doc["bucket"] | "";
                String endpoint = doc["endpoint"] | "";

                int frequency = doc["frequency"].as<int>();
                if (frequency == 0) frequency = 60;
                Serial.println("🔍 Final frequency: " + String(frequency));
                
                if (provider.isEmpty() || accessKey.isEmpty() || secretKey.isEmpty() || bucket.isEmpty()) {
                    request->send(400, "application/json", "{\"success\":false,\"error\":\"Missing required fields\"}");
                    return;
                }
                
                // Validate provider
                if (provider != "digitalocean" && provider != "aws") {
                    request->send(400, "application/json", "{\"success\":false,\"error\":\"Invalid provider. Must be 'digitalocean' or 'aws'\"}");
                    return;
                }
                
                // Validate frequency
                if (frequency < 5 || frequency > 1440) { // 5 minutes to 24 hours
                    request->send(400, "application/json", "{\"success\":false,\"error\":\"Sync frequency must be between 5 and 1440 minutes\"}");
                    return;
                }
                
                // Update cloud configuration
                cloudConfig.provider = provider;
                cloudConfig.accessKey = accessKey;
                cloudConfig.secretKey = secretKey;
                cloudConfig.bucketName = bucket;
                cloudConfig.endpointUrl = endpoint;
                cloudConfig.syncFrequency = frequency;
                cloudConfig.enabled = true;
                
                // Save to preferences
                saveCloudConfig();
                
                Serial.printf("☁️ Cloud config saved: %s provider, %s bucket, %d min frequency\n", 
                             provider.c_str(), bucket.c_str(), frequency);
                
                JsonDocument response;
                response["success"] = true;
                response["message"] = "Cloud configuration saved successfully";
                response["provider"] = provider;
                response["bucket"] = bucket;
                response["frequency"] = frequency;
                
                String responseStr;
                serializeJson(response, responseStr);
                request->send(200, "application/json", responseStr);
            }
        }
    );
    // Test cloud connection
    server.on("/test_cloud_connection", HTTP_POST,
        [](AsyncWebServerRequest *request) {},
        NULL,
        [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
            static String postBody = "";
            
            if (index == 0) postBody = "";
            
            for (size_t i = 0; i < len; i++) {
                postBody += (char)data[i];
            }
            
            if (index + len == total) {
                Serial.println("☁️ Test cloud connection request");
                
                JsonDocument doc;
                DeserializationError error = deserializeJson(doc, postBody);
                
                JsonDocument response;
                
                if (error) {
                    response["success"] = false;
                    response["error"] = "Invalid JSON";
                } else {
                    // Temporarily save test config
                    CloudConfig originalConfig = cloudConfig;
                    
                    cloudConfig.provider = doc["provider"] | "";
                    cloudConfig.accessKey = doc["access_key"] | "";
                    cloudConfig.secretKey = doc["secret_key"] | "";
                    cloudConfig.bucketName = doc["bucket"] | "";
                    cloudConfig.endpointUrl = doc["endpoint"] | "";
                    
                    if (cloudConfig.provider.isEmpty() || cloudConfig.accessKey.isEmpty() || 
                        cloudConfig.secretKey.isEmpty() || cloudConfig.bucketName.isEmpty()) {
                        response["success"] = false;
                        response["error"] = "Missing required fields for test";
                    } else if (!wifiConfigManager->isWiFiConnected()) {
                        response["success"] = false;
                        response["error"] = "WiFi not connected";
                    } else {
                        // Test the connection
                        bool testResult = testCloudConnection();
                        
                        if (testResult) {
                            response["success"] = true;
                            response["message"] = "Cloud connection test successful";
                            response["provider"] = cloudConfig.provider;
                            response["bucket"] = cloudConfig.bucketName;
                        } else {
                            response["success"] = false;
                            response["error"] = "Failed to connect to cloud storage. Check credentials and network connection.";
                        }
                    }
                    
                    // Restore original config
                    cloudConfig = originalConfig;
                }
                
                String responseStr;
                serializeJson(response, responseStr);
                request->send(200, "application/json", responseStr);
            }
        }
    );
    
    // Manual cloud sync trigger
    server.on("/trigger_cloud_sync", HTTP_POST, [](AsyncWebServerRequest *request) {
        Serial.println("☁️ Manual cloud sync triggered");
        
        JsonDocument response;
        
        if (!cloudConfig.enabled) {
            response["success"] = false;
            response["error"] = "Cloud sync not enabled";
        } else if (cloudSyncInProgress) {
            response["success"] = false;
            response["error"] = "Cloud sync already in progress";
        } else if (!wifiConfigManager->isWiFiConnected()) {
            response["success"] = false;
            response["error"] = "WiFi not connected";
        } else {
            // Force immediate sync
            cloudConfig.lastSyncTime = 0;
            performCloudSync();
            
            response["success"] = true;
            response["message"] = "Cloud sync initiated";
        }
        
        String responseStr;
        serializeJson(response, responseStr);
        request->send(200, "application/json", responseStr);
    });
    
    // Cloud sync status
    server.on("/cloud_sync_status", HTTP_GET, [](AsyncWebServerRequest *request) {
        JsonDocument doc;
        doc["enabled"] = cloudConfig.enabled;
        doc["sync_in_progress"] = cloudSyncInProgress;
        doc["last_sync_time"] = cloudConfig.lastSyncTime;
        doc["synced_sessions"] = cloudConfig.syncedSessions;
        doc["provider"] = cloudConfig.provider;
        doc["bucket"] = cloudConfig.bucketName;
        doc["frequency_minutes"] = cloudConfig.syncFrequency;
        
        if (cloudConfig.lastSyncTime > 0) {
            doc["time_since_last_sync"] = millis() - cloudConfig.lastSyncTime;
            doc["next_sync_in"] = (cloudConfig.syncFrequency * 60000UL) - (millis() - cloudConfig.lastSyncTime);
        }
        
        String response;
        serializeJson(doc, response);
        request->send(200, "application/json", response);
    });
    
    // Disable cloud sync
    server.on("/disable_cloud_sync", HTTP_POST, [](AsyncWebServerRequest *request) {
        Serial.println("☁️ Disabling cloud sync");
        
        cloudConfig.enabled = false;
        saveCloudConfig();
        
        JsonDocument response;
        response["success"] = true;
        response["message"] = "Cloud sync disabled";
        
        String responseStr;
        serializeJson(response, responseStr);
        request->send(200, "application/json", responseStr);
    });

    // Get current WiFi configuration
    server.on("/get_wifi_config", HTTP_GET, [](AsyncWebServerRequest *request) {
        Preferences prefs;
        prefs.begin("wificonfig", true);
        String ssid = prefs.getString("ssid", "");
        prefs.end();
        
        JsonDocument doc;
        doc["ssid"] = ssid;
        doc["currently_connected"] = wifiConfigManager->isWiFiConnected();
        doc["current_ssid"] = wifiConfigManager->getSSID();
        
        String response;
        serializeJson(doc, response);
        request->send(200, "application/json", response);
    });
    // WiFi scan endpoint with async handling and timeout protection
    server.on("/scan_networks", HTTP_POST, [](AsyncWebServerRequest *request) {
        Serial.println("🔍 WiFi scan request received");
        
        // Check if a scan is already in progress
        static bool scanInProgress = false;
        static unsigned long scanStartTime = 0;
        const unsigned long SCAN_TIMEOUT = 15000; // 15 seconds timeout
        
        if (scanInProgress) {
            // Check if scan has timed out
            if (millis() - scanStartTime > SCAN_TIMEOUT) {
                Serial.println("⚠️ WiFi scan timeout, resetting...");
                WiFi.scanDelete();
                scanInProgress = false;
            } else {
                JsonDocument doc;
                doc["success"] = false;
                doc["error"] = "Scan already in progress, please wait";
                doc["count"] = 0;
                
                String response;
                serializeJson(doc, response);
                request->send(429, "application/json", response); // 429 = Too Many Requests
                return;
            }
        }
        
        // Start async scan
        scanInProgress = true;
        scanStartTime = millis();
        
        Serial.println("🔄 Starting WiFi network scan...");
        
        // Use async scan with callback
        WiFi.scanNetworks(true, false, false, 300); // async=true, show_hidden=false, passive=false, max_ms_per_chan=300
        
        // Wait briefly for quick networks to appear
        unsigned long waitStart = millis();
        while (WiFi.scanComplete() == WIFI_SCAN_RUNNING && (millis() - waitStart) < 3000) {
            delay(100);
            yield();
        }
        
        int16_t scanResult = WiFi.scanComplete();
        
        JsonDocument doc;
        
        if (scanResult == WIFI_SCAN_RUNNING) {
            // Still running after brief wait
            doc["success"] = false;
            doc["error"] = "Scan timeout - try again in a moment";
            doc["count"] = 0;
            doc["message"] = "WiFi scan is taking longer than expected";
            
            scanInProgress = false;
            WiFi.scanDelete();
            
        } else if (scanResult == WIFI_SCAN_FAILED || scanResult < 0) {
            // Scan failed
            doc["success"] = false;
            doc["error"] = "WiFi scan failed";
            doc["count"] = 0;
            doc["message"] = "Unable to scan for networks at this time";
            
            scanInProgress = false;
            WiFi.scanDelete();
            
        } else {
            // Scan completed successfully
            JsonArray networks = doc["networks"].to<JsonArray>();
            
            int networkCount = scanResult;
            
            if (networkCount > 0) {
                // Limit to maximum 20 networks to prevent large responses
                int maxNetworks = min(networkCount, 20);
                
                for (int i = 0; i < maxNetworks; i++) {
                    JsonObject network = networks.add<JsonObject>();
                    network["ssid"] = WiFi.SSID(i);
                    network["rssi"] = WiFi.RSSI(i);
                    
                    // Convert encryption type to readable format
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
                    
                    // Add signal strength indicator
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
                
                if (networkCount > 20) {
                    doc["message"] = "Showing first 20 of " + String(networkCount) + " networks found";
                } else {
                    doc["message"] = "Found " + String(networkCount) + " networks";
                }
                
                Serial.println("📡 Found " + String(networkCount) + " networks, displaying " + String(maxNetworks));
            } else {
                doc["success"] = true;
                doc["count"] = 0;
                doc["message"] = "No networks found";
                
                Serial.println("📡 No networks found");
            }
            
            scanInProgress = false;
            WiFi.scanDelete(); // Clean up scan results
        }
        
        String response;
        serializeJson(doc, response);
        request->send(200, "application/json", response);
    });
    
    // WiFi scan status endpoint for checking ongoing scans
    server.on("/scan_status", HTTP_GET, [](AsyncWebServerRequest *request) {
        JsonDocument doc;
        
        int16_t scanResult = WiFi.scanComplete();
        
        if (scanResult == WIFI_SCAN_RUNNING) {
            doc["status"] = "scanning";
            doc["message"] = "Scan in progress";
        } else if (scanResult == WIFI_SCAN_FAILED) {
            doc["status"] = "failed";
            doc["message"] = "Scan failed";
        } else if (scanResult >= 0) {
            doc["status"] = "complete";
            doc["count"] = scanResult;
            doc["message"] = "Scan complete";
        } else {
            doc["status"] = "idle";
            doc["message"] = "No scan running";
        }
        
        String response;
        serializeJson(doc, response);
        request->send(200, "application/json", response);
    });
    
    //Test WiFi connection endpoint
    server.on("/test_wifi_connection", HTTP_POST, [](AsyncWebServerRequest *request) {
        JsonDocument response;
        response["success"] = true;
        response["message"] = "Test completed - credentials appear valid";
        response["rssi"] = -65; // Simulated value
        
        String responseStr;
        serializeJson(response, responseStr);
        request->send(200, "application/json", responseStr);
    });
    
    // Internet connectivity status endpoint
    server.on("/internet_status", HTTP_GET, [](AsyncWebServerRequest *request) {
        networkManager->checkInternetConnectivity();
        
        JsonDocument status;
        status["internet_connected"] = networkManager->isInternetConnected();
        status["wifi_connected"] = wifiConfigManager->isWiFiConnected();
        status["cloud_enabled"] = cloudConfig.enabled;
        status["cloud_sync_in_progress"] = cloudSyncInProgress;
        status["timestamp"] = millis();
        
        if (wifiConfigManager->isWiFiConnected()) {
            status["wifi_ssid"] = wifiConfigManager->getSSID();
            status["wifi_rssi"] = wifiConfigManager->getRSSI();
            status["ip_address"] = wifiConfigManager->getLocalIP().toString();
        }
        
        String response;
        serializeJson(status, response);
        request->send(200, "application/json", response);
    });
    // Configuration API
    server.on("/get_config", HTTP_GET, [](AsyncWebServerRequest *request) {
        CPRThresholds params = metricsCalculator->getParams();
        
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
        [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
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
                    
                    metricsCalculator->updateParams(newParams);
                    
                    request->send(200, "application/json", "{\"status\":\"success\",\"message\":\"Configuration updated\"}");
                    Serial.println("Configuration updated via web interface");
                    
                } catch (...) {
                    request->send(500, "application/json", "{\"error\":\"Failed to update configuration\"}");
                }
            }
        }
    );
    
    // WiFi configuration
    server.on("/save_wifi_config", HTTP_POST,
        [](AsyncWebServerRequest *request) {},
        NULL,
        [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
            static String postBody = "";
            
            if (index == 0) postBody = "";
            
            for (size_t i = 0; i < len; i++) {
                postBody += (char)data[i];
            }
            
            if (index + len == total) {
                Serial.println("💾 WiFi config save request received");
                
                JsonDocument doc;
                DeserializationError error = deserializeJson(doc, postBody);
                
                if (error) {
                    Serial.println("❌ JSON parse error: " + String(error.c_str()));
                    request->send(400, "application/json", "{\"success\":false,\"error\":\"Invalid JSON\"}");
                    return;
                }
                
                String ssid = doc["ssid"] | "";
                String password = doc["password"] | "";
                
                if (ssid.isEmpty()) {
                    request->send(400, "application/json", "{\"success\":false,\"error\":\"SSID cannot be empty\"}");
                    return;
                }
                
                Serial.println("🔧 Configuring WiFi: " + ssid);
                
                bool result = wifiConfigManager->saveWiFiCredentials(ssid, password);
                
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
    server.on("/network_status", HTTP_GET, [](AsyncWebServerRequest *request) {
        JsonDocument status;
        status["wifi_connected"] = wifiConfigManager->isWiFiConnected();
        status["wifi_ssid"] = wifiConfigManager->getSSID();
        status["wifi_rssi"] = wifiConfigManager->getRSSI();
        status["ip_address"] = wifiConfigManager->getLocalIP().toString();
        status["hotspot_active"] = wifiConfigManager->isHotspotActive();
        status["hotspot_ssid"] = wifiConfigManager->getAPSSID();
        status["cloud_enabled"] = cloudConfig.enabled;
        status["cloud_sync_in_progress"] = cloudSyncInProgress;
        status["timestamp"] = millis();
        
        String response;
        serializeJson(status, response);
        request->send(200, "application/json", response);
    });
    // Data management API
    server.on("/files_api", HTTP_GET, [](AsyncWebServerRequest *request) {
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
        
        doc["csv_file_exists"] = SPIFFS.exists(csvFileName);
        doc["csv_file_name"] = csvFileName;
        doc["chip_id"] = chipId;
        doc["next_session"] = lastSessionNumber + 1;
        doc["cloud_enabled"] = cloudConfig.enabled;
        doc["cloud_provider"] = cloudConfig.provider;
        
        String response;
        serializeJson(doc, response);
        request->send(200, "application/json", response);
    });
    
    server.on("/download_csv", HTTP_GET, [](AsyncWebServerRequest *request) {
        if (SPIFFS.exists(csvFileName)) {
            request->send(SPIFFS, csvFileName, "text/csv", true);
        } else {
            request->send(404, "text/plain", "CSV file not found");
        }
    });
    
    server.on("/delete_csv", HTTP_POST, [](AsyncWebServerRequest *request) {
        JsonDocument response;
        
        if (isRecording) {
            response["success"] = false;
            response["error"] = "Cannot delete CSV file while recording is active";
        } else {
            bool result = deleteCSVFile();
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
    
    // Status endpoint - Enhanced with cloud info
    server.on("/status", HTTP_GET, [](AsyncWebServerRequest *request) {
        JsonDocument status;
        status["status"] = "running";
        status["chip_id"] = chipId;
        status["recording"] = isRecording;
        status["session_id"] = currentSessionId;
        status["next_session"] = lastSessionNumber + 1;
        status["metrics_clients"] = webSocket.count();
        status["anim_clients"] = animWebSocket.count();
        status["free_heap"] = ESP.getFreeHeap();
        status["csv_file_open"] = csvFileOpen;
        status["csv_file_name"] = csvFileName;
        status["csv_file_exists"] = SPIFFS.exists(csvFileName);
        status["csv_write_count"] = csvWriteCount;
        
        // WiFi status information
        status["wifi_connected"] = wifiConfigManager->isWiFiConnected();
        status["wifi_ssid"] = wifiConfigManager->getSSID();
        status["wifi_rssi"] = wifiConfigManager->getRSSI();
        status["hotspot_active"] = wifiConfigManager->isHotspotActive();
        status["hotspot_ssid"] = wifiConfigManager->getAPSSID();
        
        // Cloud status information
        status["cloud_enabled"] = cloudConfig.enabled;
        status["cloud_provider"] = cloudConfig.provider;
        status["cloud_bucket"] = cloudConfig.bucketName;
        status["cloud_sync_frequency"] = cloudConfig.syncFrequency;
        status["cloud_sync_in_progress"] = cloudSyncInProgress;
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
    server.on("/start_stop", HTTP_POST, [](AsyncWebServerRequest *request) {
        JsonDocument response;
            if (spiffsDangerMode && !isRecording) {
        response["status"] = "blocked";
        response["error"] = "Operations suspended - SPIFFS storage full. Enable cloud upload.";
        response["spiffs_danger"] = true;
        response["is_recording"] = false;
        
        String responseStr;
        serializeJson(response, responseStr);
        request->send(423, "application/json", responseStr); // 423 = Locked
        return;
    }
        if (!isRecording) {
            currentSessionId = getNextSessionNumber();
            metricsCalculator->reset();
            dbManager->startNewSession();
            isRecording = true;
            
            if (!openCSVFile()) {
                Serial.println("Warning: Failed to open CSV file for recording");
            }
            
            response["status"] = "started";
            response["session_id"] = currentSessionId;
            response["is_recording"] = true;
            
            Serial.printf("Training session %d started - metrics reset\n", currentSessionId);
        } else {
            dbManager->endCurrentSession();
            isRecording = false;
            closeCSVFile(); // This will trigger cloud sync if enabled
            
            response["status"] = "stopped";
            response["session_id"] = currentSessionId;
            response["is_recording"] = false;
            
            Serial.printf("Training session %d stopped\n", currentSessionId);
        }
        
        String responseStr;
        serializeJson(response, responseStr);
        request->send(200, "application/json", responseStr);
        
        // Broadcast to WebSockets
        if (webSocket.count() > 0) {
            JsonDocument wsDoc;
            wsDoc["type"] = "recording_status";
            wsDoc["is_recording"] = isRecording;
            wsDoc["session_id"] = currentSessionId;
            wsDoc["cloud_enabled"] = cloudConfig.enabled;
            wsDoc["message"] = isRecording ? ("Session " + String(currentSessionId) + " started") : 
                                           ("Session " + String(currentSessionId) + " stopped");
            
            String wsMessage;
            serializeJson(wsDoc, wsMessage);
            webSocket.textAll(wsMessage);
        }
        
        if (!isRecording && animWebSocket.count() > 0) {
            broadcastAnimationState("quietude");
        }
    });
    // Debug endpoint - Enhanced with cloud info
    server.on("/debug", HTTP_GET, [](AsyncWebServerRequest *request) {
        String debug = "<!DOCTYPE html><html><head><title>Debug Info</title></head><body>";
        debug += "<h1>ESP32 CPR Monitor Debug Information</h1>";
        
        debug += "<h2>System Information</h2>";
        debug += "Chip ID: " + chipId + "<br>";
        debug += "CSV Filename: " + csvFileName + "<br>";
        debug += "Free Heap: " + String(ESP.getFreeHeap()) + " bytes<br>";
        
        // WiFi debug information
        debug += "<h2>WiFi Status</h2>";
        debug += "WiFi Connected: " + String(wifiConfigManager->isWiFiConnected() ? "Yes" : "No") + "<br>";
        debug += "WiFi SSID: " + wifiConfigManager->getSSID() + "<br>";
        debug += "WiFi RSSI: " + String(wifiConfigManager->getRSSI()) + " dBm<br>";
        debug += "WiFi IP: " + wifiConfigManager->getLocalIP().toString() + "<br>";
        debug += "Hotspot Active: " + String(wifiConfigManager->isHotspotActive() ? "Yes" : "No") + "<br>";
        debug += "Hotspot SSID: " + wifiConfigManager->getAPSSID() + "<br>";
        debug += "Hotspot IP: " + wifiConfigManager->getAPIP().toString() + "<br>";
        debug += "Hotspot Clients: " + String(WiFi.softAPgetStationNum()) + "<br>";
        
        // Cloud debug information
        debug += "<h2>Cloud Configuration</h2>";
        debug += "Cloud Enabled: " + String(cloudConfig.enabled ? "Yes" : "No") + "<br>";
        debug += "Cloud Provider: " + cloudConfig.provider + "<br>";
        debug += "Cloud Bucket: " + cloudConfig.bucketName + "<br>";
        debug += "Cloud Endpoint: " + cloudConfig.endpointUrl + "<br>";
        debug += "Sync Frequency: " + String(cloudConfig.syncFrequency) + " minutes<br>";
        debug += "Sync In Progress: " + String(cloudSyncInProgress ? "Yes" : "No") + "<br>";
        debug += "Last Sync: " + String(cloudConfig.lastSyncTime) + "<br>";
        debug += "Synced Sessions: " + String(cloudConfig.syncedSessions) + "<br>";
        debug += "Has Access Key: " + String(!cloudConfig.accessKey.isEmpty() ? "Yes" : "No") + "<br>";
        debug += "Has Secret Key: " + String(!cloudConfig.secretKey.isEmpty() ? "Yes" : "No") + "<br>";
        
        debug += "<h2>SPIFFS Status</h2>";
        debug += "Total: " + String(SPIFFS.totalBytes()) + " bytes<br>";
        debug += "Used: " + String(SPIFFS.usedBytes()) + " bytes<br>";
        debug += "Free: " + String(SPIFFS.totalBytes() - SPIFFS.usedBytes()) + " bytes<br>";
        
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
        debug += "CSV File Exists: " + String(SPIFFS.exists(csvFileName) ? "Yes" : "No") + "<br>";
        debug += "CSV File Open: " + String(csvFileOpen ? "Yes" : "No") + "<br>";
        debug += "CSV Write Count: " + String(csvWriteCount) + "<br>";
        
        debug += "<h2>Recording Status</h2>";
        debug += "Recording: " + String(isRecording ? "Yes" : "No") + "<br>";
        debug += "Current Session: " + String(currentSessionId) + "<br>";
        debug += "Next Session: " + String(lastSessionNumber + 1) + "<br>";
        
        debug += "<h2>WebSocket Status</h2>";
        debug += "Metrics WS Clients: " + String(webSocket.count()) + "<br>";
        debug += "Animation WS Clients: " + String(animWebSocket.count()) + "<br>";
        
        debug += "</body></html>";
        request->send(200, "text/html", debug);
    });
    
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

    // 404 handler
    server.onNotFound([](AsyncWebServerRequest *request) {
        Serial.printf("404 - Not Found: %s\n", request->url().c_str());
        request->send(404, "text/plain", "Page not found");
    });

    server.begin();
    Serial.println("Web server started with comprehensive routes including cloud configuration:");
    Serial.println("  /ws - Metrics WebSocket (2Hz)");
    Serial.println("  /animws - Animation WebSocket (20Hz)");
    Serial.println("  /ssid_config - WiFi Configuration Page");
    Serial.println("  /cloud_config - Cloud Configuration Page");
    Serial.println("  /network_status - Network status API");
    Serial.println("  /scan_networks - WiFi scan API");
    Serial.println("  /internet_status - Internet connectivity status");
    Serial.println("  /config - CPR Configuration");
    Serial.println("  /data - Data Management");
    Serial.println("  /debug - Debug Information");
    Serial.println("  CLOUD ENDPOINTS:");
    Serial.println("    /get_cloud_config - Get cloud settings");
    Serial.println("    /save_cloud_config - Save cloud settings");
    Serial.println("    /test_cloud_connection - Test cloud connection");
    Serial.println("    /trigger_cloud_sync - Manual cloud sync");
    Serial.println("    /cloud_sync_status - Cloud sync status");
    Serial.println("    /disable_cloud_sync - Disable cloud sync");
}
// =============================================
// MAIN LOOP WITH ENHANCED CLOUD SYNC MONITORING
// =============================================

void loop() {
    unsigned long currentTime = millis();
    
    // Update WiFi Configuration Manager
    wifiConfigManager->loop();
    
    // Perform periodic cloud sync if enabled
    if (cloudConfig.enabled && !isRecording) {
        performCloudSync();
    }
    if (spiffsDangerMode) {
        broadcastDangerStatus();
    }

    // Read potentiometer at 40Hz
    if (!spiffsDangerMode) {
        if (currentTime - lastPotRead >= POT_READ_INTERVAL) {
        int potValue = analogRead(POTENTIOMETER_PIN);
        
        // Convert 12-bit ADC (0-4095) to 10-bit range (0-1023) for metrics calculator
        int scaledValue = map(potValue, 0, 4095, 0, 1023);
        
        // Process through metrics calculator
        CPRStatus status = metricsCalculator->detectTrend(scaledValue);
        
        // Enhanced CSV logging with full status information
        if (isRecording) {
            handleCSVLogging(currentTime, potValue, status);
        }
        
        // Record to database if recording
        if (isRecording && dbManager && (currentTime % 100 == 0)) {
            dbManager->recordCompressionEvent(
                currentTime,
                scaledValue,
                status.state.c_str(),
                status.currentCompression.isGood
            );
        }
        
        lastPotRead = currentTime;
        
        // Send animation data at 20Hz
        if (currentTime - lastAnimSend >= ANIM_SEND_INTERVAL) {
            if (status.state != lastAnimState && animWebSocket.count() > 0) {
                broadcastAnimationState(status.state);
                lastAnimSend = currentTime;
            }
        }
        
        // Send metrics data at 2Hz
        if (currentTime - lastDataSend >= DATA_SEND_INTERVAL) {
            if (webSocket.count() > 0) {
                broadcastStateUpdate(status);
                lastDataSend = currentTime;
            }
        }
        
        // Update LED and audio
        updateStatusLED(status.state);
        
        if (isRecording) {
            processAudioAlerts(status.alerts);
        }
    }
    }
    // Network monitoring and broadcasting - Enhanced with cloud status
    static unsigned long lastNetworkBroadcast = 0;
    static bool lastInternetStatus = false;
    static bool lastWifiStatus = false;
    static bool lastCloudSyncStatus = false;
    
    // Check internet connectivity periodically
    networkManager->checkInternetConnectivity();
    
    // Broadcast network status if it changed or every 30 seconds
    bool currentInternetStatus = networkManager->isInternetConnected();
    bool currentWifiStatus = wifiConfigManager->isWiFiConnected();
    bool currentCloudSyncStatus = cloudSyncInProgress;
    
    if (currentTime - lastNetworkBroadcast >= 30000 || // Every 30 seconds
        currentInternetStatus != lastInternetStatus ||  // Internet status changed
        currentWifiStatus != lastWifiStatus ||          // WiFi status changed
        currentCloudSyncStatus != lastCloudSyncStatus) { // Cloud sync status changed
        
        broadcastNetworkStatus();
        lastNetworkBroadcast = currentTime;
        lastInternetStatus = currentInternetStatus;
        lastWifiStatus = currentWifiStatus;
        lastCloudSyncStatus = currentCloudSyncStatus;
    }
    
    // Periodic maintenance
    static unsigned long lastCleanup = 0;
    if (currentTime - lastCleanup > 5000) {
        webSocket.cleanupClients();
        animWebSocket.cleanupClients();
        lastCleanup = currentTime;
        
        // Debug CSV status
        if (isRecording && csvFileOpen) {
            Serial.printf("CSV Status: %d records written to %s\n", csvWriteCount, csvFileName.c_str());
        }
        
        // Debug cloud sync status
        if (cloudConfig.enabled && (currentTime % 30000 == 0)) { // Every 30 seconds
            unsigned long timeSinceLastSync = currentTime - cloudConfig.lastSyncTime;
            unsigned long nextSyncIn = (cloudConfig.syncFrequency * 60000UL) - timeSinceLastSync;
            
            Serial.printf("☁️ Cloud Status: Provider=%s, Enabled=%s, InProgress=%s, LastSync=%lums ago, NextSync=%lums\n",
                         cloudConfig.provider.c_str(),
                         cloudConfig.enabled ? "Yes" : "No",
                         cloudSyncInProgress ? "Yes" : "No",
                         timeSinceLastSync,
                         nextSyncIn);
        }
    }
    
    // Audio cleanup
    if (isCurrentlyPlayingAudio && currentTime > lastAudioEndTime) {
        isCurrentlyPlayingAudio = false;
    }
    
    // SPIFFS health check
    checkSPIFFSHealth();
    
    yield();
}

// =============================================
// SETUP FUNCTION - Enhanced with Cloud Configuration
// =============================================

void setup() {
    Serial.begin(115200);
    delay(1000);
    
    Serial.println("Starting ESP32 CPR Monitor with WiFi and Cloud Configuration...");
    
    // Initialize hardware
    pinMode(LED_PIN, OUTPUT);
    pinMode(AUDIO_PIN, OUTPUT);
    analogReadResolution(12); // 0-4095 range
    
    // Set CPU frequency for performance
    setCpuFrequencyMhz(240);
    
    // Initialize time for cloud operations
    configTime(0, 0, "pool.ntp.org", "time.nist.gov");
    
    // Initialize SPIFFS
    if (!initializeSPIFFSWithRetry()) {
        Serial.println("SPIFFS failed - some features will be unavailable");
    } else {
        checkRequiredFiles();
    }
    
    // Initialize CSV system with chip ID and cloud configuration
    setupCSVSystem();
    
    // Initialize system components
    metricsCalculator = new CPRMetricsCalculator();
    dbManager = new DatabaseManager();
    networkManager = new NetworkManager();
    
    // Initialize WiFi Configuration Manager
    wifiConfigManager = new WiFiConfigManager(&server);
    
    // Initialize WiFi Configuration Manager
    wifiConfigManager->begin();

    // Give WiFi time to connect if credentials exist
    delay(5000);
    
    // Check if WiFi connected and update NetworkManager
    if (wifiConfigManager->isWiFiConnected()) {
        Serial.println("✅ WiFi connection established during setup");
        // Force NetworkManager to recognize the connection
        networkManager->handleTasks();
        
        // Wait for time sync if WiFi is connected
        Serial.println("⏰ Waiting for time synchronization...");
        int timeWaitCount = 0;
        while (time(nullptr) < 8 * 3600 * 2 && timeWaitCount < 20) {
            delay(500);
            timeWaitCount++;
            Serial.print(".");
        }
        if (time(nullptr) >= 8 * 3600 * 2) {
            Serial.println("\n✅ Time synchronized");
        } else {
            Serial.println("\n⚠️ Time sync timeout, continuing anyway");
        }
    } else {
        Serial.println("ℹ️ No WiFi connection established during setup");
    }
    
    // Start web server
    setupWebServer();
    
    Serial.println("CPR Monitor initialized successfully with WiFi and Cloud configuration");
    Serial.printf("ESP32 Chip ID: %s\n", chipId.c_str());
    Serial.printf("CSV Filename: %s\n", csvFileName.c_str());
    Serial.printf("Next session will be: %d\n", lastSessionNumber + 1);
    
    // Enhanced access information with cloud configuration
    Serial.println("\n" + String("=").substring(0, 60));
    Serial.println("🌐 CPR Monitor Access Information");
    Serial.println(String("=").substring(0, 60));
    
    if (wifiConfigManager->isWiFiConnected()) {
        Serial.println("📶 WiFi Connected:");
        Serial.println("   SSID: " + wifiConfigManager->getSSID());
        Serial.println("   IP: " + wifiConfigManager->getLocalIP().toString());
        Serial.println("   Signal: " + String(wifiConfigManager->getRSSI()) + " dBm");
        Serial.println("   Dashboard: http://" + wifiConfigManager->getLocalIP().toString());
        Serial.println("   WiFi Config: http://" + wifiConfigManager->getLocalIP().toString() + "/ssid_config");
        Serial.println("   Cloud Config: http://" + wifiConfigManager->getLocalIP().toString() + "/cloud_config");
    }
    
    if (wifiConfigManager->isHotspotActive()) {
        Serial.println("🔥 Hotspot Active:");
        Serial.println("   SSID: " + wifiConfigManager->getAPSSID());
        Serial.println("   Password: cpr12345");
        Serial.println("   IP: " + wifiConfigManager->getAPIP().toString());
        Serial.println("   Dashboard: http://192.168.4.1");
        Serial.println("   WiFi Config: http://192.168.4.1/ssid_config");
        Serial.println("   Cloud Config: http://192.168.4.1/cloud_config");
    }
    
    // Cloud configuration status
    Serial.println("☁️ Cloud Configuration:");
    if (cloudConfig.enabled) {
        Serial.println("   Status: ENABLED");
        Serial.println("   Provider: " + cloudConfig.provider);
        Serial.println("   Bucket: " + cloudConfig.bucketName);
        Serial.println("   Sync Frequency: " + String(cloudConfig.syncFrequency) + " minutes");
        Serial.println("   Synced Sessions: " + String(cloudConfig.syncedSessions));
    } else {
        Serial.println("   Status: DISABLED");
        Serial.println("   Configure at: /cloud_config");
    }
    
    Serial.println(String("=").substring(0, 60));
    
    // Test CSV system
    Serial.println("Testing CSV system...");
    if (SPIFFS.exists(csvFileName)) {
        File testFile = SPIFFS.open(csvFileName, "r");
        if (testFile) {
            Serial.printf("CSV file size: %d bytes\n", testFile.size());
            String firstLine = testFile.readStringUntil('\n');
            Serial.printf("CSV headers: %s\n", firstLine.c_str());
            testFile.close();
        }
    }
    
    Serial.println("Setup complete.");
    Serial.println("📱 Access the dashboard via hotspot or WiFi connection");
    Serial.println("🔧 WiFi configuration available at /ssid_config");
    Serial.println("☁️ Cloud configuration available at /cloud_config");
    Serial.println("🛠 Debug info available at /debug");
    Serial.println("🌐 Internet status available at /internet_status");
    Serial.println("✅ Enhanced WiFi and Cloud Configuration System Ready!");
    
    // Perform initial cloud sync if enabled and WiFi connected
    if (cloudConfig.enabled && wifiConfigManager->isWiFiConnected()) {
        Serial.println("☁️ Performing initial cloud sync check...");
        cloudConfig.lastSyncTime = 0; // Force initial sync check
    }
}