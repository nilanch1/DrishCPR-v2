#include "SystemManager.h"
#include "WebServerSetup.h"  // Include after SystemManager.h to break circular dependency


bool fileUploadInProgress = false;

// Static constant definitions moved from header
const float SystemManager::SPIFFS_DANGER_THRESHOLD = 85.0;
const float SystemManager::SPIFFS_SAFE_THRESHOLD = 75.0;
const unsigned long SystemManager::POT_READ_INTERVAL = 25;
const unsigned long SystemManager::DATA_SEND_INTERVAL = 500;
const unsigned long SystemManager::ANIM_SEND_INTERVAL = 50;
const unsigned long SystemManager::CSV_WRITE_INTERVAL = 50;
const unsigned long SystemManager::MIN_AUDIO_GAP = 2000;
const int SystemManager::MAX_WS_CLIENTS = 4;

// Implementation
SystemManager::SystemManager() {
    metricsCalculator = nullptr;
    dbManager = nullptr;
    networkManager = nullptr;
    wifiConfigManager = nullptr;
    cloudManager = nullptr;
    webServerSetup = nullptr;
}

SystemManager::~SystemManager() {
    if (csvFileOpen && csvFile) {
        csvFile.close();
    }
    
    delete metricsCalculator;
    delete dbManager;
    delete networkManager;
    delete wifiConfigManager;
    delete cloudManager;
    delete webServerSetup;
}

void SystemManager::initialize() {
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
    cloudManager = new CloudManager();
    cloudManager->initialize();
    
    // Initialize WiFi Configuration Manager
    wifiConfigManager = new WiFiConfigManager(nullptr); // Server will be set later
    wifiConfigManager->begin();

    // Give WiFi time to connect if credentials exist
    delay(5000);
    
    // Check if WiFi connected and update NetworkManager
    if (wifiConfigManager->isWiFiConnected()) {
        Serial.println("WiFi connection established during setup");
        networkManager->handleTasks();
        
        // Wait for time sync if WiFi is connected
        Serial.println("Waiting for time synchronization...");
        int timeWaitCount = 0;
        while (time(nullptr) < 8 * 3600 * 2 && timeWaitCount < 20) {
            delay(500);
            timeWaitCount++;
            Serial.print(".");
        }
        if (time(nullptr) >= 8 * 3600 * 2) {
            Serial.println("\nTime synchronized");
        } else {
            Serial.println("\nTime sync timeout, continuing anyway");
        }
    }
    
    // Initialize web server
    webServerSetup = new WebServerSetup(this);
    
    // Update WiFi config manager with server reference
    delete wifiConfigManager;
    wifiConfigManager = new WiFiConfigManager(webServerSetup->getServer());
    wifiConfigManager->begin();
    
    webServerSetup->setup();
    
    // Display access information
    Serial.println("\n" + String("=").substring(0, 60));
    Serial.println("CPR Monitor Access Information");
    Serial.println(String("=").substring(0, 60));
    
    if (wifiConfigManager->isWiFiConnected()) {
        Serial.println("WiFi Connected:");
        Serial.println("   SSID: " + wifiConfigManager->getSSID());
        Serial.println("   IP: " + wifiConfigManager->getLocalIP().toString());
        Serial.println("   Signal: " + String(wifiConfigManager->getRSSI()) + " dBm");
        Serial.println("   Dashboard: http://" + wifiConfigManager->getLocalIP().toString());
    }
    
    if (wifiConfigManager->isHotspotActive()) {
        Serial.println("Hotspot Active:");
        Serial.println("   SSID: " + wifiConfigManager->getAPSSID());
        Serial.println("   Password: cpr12345");
        Serial.println("   IP: " + wifiConfigManager->getAPIP().toString());
        Serial.println("   Dashboard: http://192.168.4.1");
    }
    
    CloudConfig cloudConfig = cloudManager->getConfig();
    Serial.println("Cloud Configuration:");
    if (cloudConfig.enabled) {
        Serial.println("   Status: ENABLED");
        Serial.println("   Provider: " + cloudConfig.provider);
        Serial.println("   Bucket: " + cloudConfig.bucketName);
        Serial.println("   Sync Frequency: " + String(cloudConfig.syncFrequency) + " minutes");
    } else {
        Serial.println("   Status: DISABLED");
    }
    
    Serial.println(String("=").substring(0, 60));
    
    Serial.printf("ESP32 Chip ID: %s\n", chipId.c_str());
    Serial.printf("CSV Filename: %s\n", csvFileName.c_str());
    Serial.printf("Next session will be: %d\n", lastSessionNumber + 1);
}

void SystemManager::loop() {
    unsigned long currentTime = millis();
    
    // Update WiFi Configuration Manager
    wifiConfigManager->loop();
    
    // Perform periodic cloud sync if enabled
    CloudConfig cloudConfig = cloudManager->getConfig();
    if (cloudConfig.enabled && !isRecording) {
        cloudManager->performSync();
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
                static String lastAnimState = "";
                if (status.state != lastAnimState && webServerSetup->getAnimWebSocket()->count() > 0) {
                    broadcastAnimationState(status.state);
                    lastAnimSend = currentTime;
                    lastAnimState = status.state;
                }
            }
            
            // Send metrics data at 2Hz
            if (currentTime - lastDataSend >= DATA_SEND_INTERVAL) {
                if (webServerSetup->getWebSocket()->count() > 0) {
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
    
    // Network monitoring and broadcasting
    static unsigned long lastNetworkBroadcast = 0;
    static bool lastInternetStatus = false;
    static bool lastWifiStatus = false;
    static bool lastCloudSyncStatus = false;
    
    // Check internet connectivity periodically
    networkManager->checkInternetConnectivity();
    
    // Broadcast network status if it changed or every 30 seconds
    bool currentInternetStatus = networkManager->isInternetConnected();
    bool currentWifiStatus = wifiConfigManager->isWiFiConnected();
    bool currentCloudSyncStatus = cloudManager->isSyncInProgress();
    
    if (currentTime - lastNetworkBroadcast >= 30000 || 
        currentInternetStatus != lastInternetStatus ||
        currentWifiStatus != lastWifiStatus ||
        currentCloudSyncStatus != lastCloudSyncStatus) {
        
        broadcastNetworkStatus();
        lastNetworkBroadcast = currentTime;
        lastInternetStatus = currentInternetStatus;
        lastWifiStatus = currentWifiStatus;
        lastCloudSyncStatus = currentCloudSyncStatus;
    }
    
    // Periodic maintenance
    static unsigned long lastCleanup = 0;
    if (currentTime - lastCleanup > 5000) {
        webServerSetup->getWebSocket()->cleanupClients();
        webServerSetup->getAnimWebSocket()->cleanupClients();
        lastCleanup = currentTime;
        
        // Debug CSV status
        if (isRecording && csvFileOpen) {
            Serial.printf("CSV Status: %d records written to %s\n", csvWriteCount, csvFileName.c_str());
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

// Recording control methods
bool SystemManager::startRecording() {
    if (spiffsDangerMode) {
        return false;
    }
    
    if (!isRecording) {
        currentSessionId = lastSessionNumber + 1;
        sessionPrefs.putInt("lastSession", currentSessionId);
        lastSessionNumber = currentSessionId;
        
        metricsCalculator->reset();
        dbManager->startNewSession();
        isRecording = true;
        
        if (!openCSVFile()) {
            Serial.println("Warning: Failed to open CSV file for recording");
        }
        
        Serial.printf("Training session %d started - metrics reset\n", currentSessionId);
        return true;
    }
    return false;
}

bool SystemManager::stopRecording() {
    if (isRecording) {
        dbManager->endCurrentSession();
        isRecording = false;
        closeCSVFile();
        
        Serial.printf("Training session %d stopped\n", currentSessionId);
        return true;
    }
    return false;
}

// File operations
bool SystemManager::deleteCSVFile() {
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
    return true;
}

bool SystemManager::isCSVFileEmpty() {
    if (!SPIFFS.exists(csvFileName)) {
        return true;
    }
    
    File file = SPIFFS.open(csvFileName, "r");
    if (!file) {
        return true;
    }
    
    int dataLines = 0;
    String line;
    bool isFirstLine = true;
    
    while (file.available()) {
        line = file.readStringUntil('\n');
        line.trim();
        
        if (line.length() == 0 || line.startsWith("#")) {
            continue;
        }
        
        if (isFirstLine) {
            isFirstLine = false;
            continue;
        }
        
        dataLines++;
    }
    
    file.close();
    return (dataLines == 0);
}

// Private implementation methods
bool SystemManager::initializeSPIFFSWithRetry() {
    int attempts = 0;
    const int maxAttempts = 3;
    
    while (attempts < maxAttempts) {
        Serial.printf("Initializing SPIFFS (attempt %d/%d)...\n", attempts + 1, maxAttempts);
        
        if (SPIFFS.begin(true)) {
            Serial.println("SPIFFS mounted successfully");
            
            size_t totalBytes = SPIFFS.totalBytes();
            size_t usedBytes = SPIFFS.usedBytes();
            Serial.printf("SPIFFS: %d/%d bytes used (%.1f%%)\n", 
                         usedBytes, totalBytes, (float)usedBytes/totalBytes*100);
            
            return true;
        }
        
        attempts++;
        delay(1000);
    }
    
    Serial.println("Failed to initialize SPIFFS after multiple attempts");
    return false;
}

void SystemManager::initializeChipId() {
    uint64_t chipid = ESP.getEfuseMac();
    chipId = String((uint32_t)(chipid >> 32), HEX) + String((uint32_t)chipid, HEX);
    chipId.toUpperCase();
    
    csvFileName = "/" + chipId + ".csv";
    
    Serial.printf("ESP32 Chip ID: %s\n", chipId.c_str());
    Serial.printf("CSV Filename: %s\n", csvFileName.c_str());
}

void SystemManager::initializeSessionTracking() {
    sessionPrefs.begin("sessions", false);
    lastSessionNumber = sessionPrefs.getInt("lastSession", 0);
    Serial.printf("Last session number: %d\n", lastSessionNumber);
}

void SystemManager::setupCSVSystem() {
    initializeChipId();
    initializeSessionTracking();
    initializeCSVFile();
    Serial.printf("CSV system initialized with chip ID: %s\n", chipId.c_str());
}

void SystemManager::checkRequiredFiles() {
    const String requiredFiles[] = {
        "/index.html",
        "/config.html", 
        "/ssid_config.html",
        "/cloud_config.html",
        "/data.html"
    };
    
    Serial.println("Checking required files...");
    
    for (const String& filename : requiredFiles) {
        if (SPIFFS.exists(filename)) {
            File file = SPIFFS.open(filename, "r");
            if (file) {
                Serial.printf("%s (%d bytes)\n", filename.c_str(), file.size());
                file.close();
            }
        } else {
            Serial.printf("%s (missing)\n", filename.c_str());
        }
    }
}

void SystemManager::checkSPIFFSHealth() {
    if (fileUploadInProgress) {
        return;
    }
    
    static unsigned long lastCheck = 0;
    static unsigned long lastStatusLog = 0;
    static int consecutiveFailures = 0;
    unsigned long now = millis();
    
    if (now - lastCheck < 60000) return;
    lastCheck = now;
    
    bool isMounted = false;
    size_t totalBytes = 0;
    size_t usedBytes = 0;
    
    try {
        totalBytes = SPIFFS.totalBytes();
        usedBytes = SPIFFS.usedBytes();
        
        if (totalBytes > 0) {
            isMounted = true;
            consecutiveFailures = 0;
            
            float usagePercent = (float)usedBytes / totalBytes * 100;
            
            bool previousDangerMode = spiffsDangerMode;
            
            if (!spiffsDangerMode && usagePercent >= SPIFFS_DANGER_THRESHOLD) {
                spiffsDangerMode = true;
                Serial.printf("SPIFFS DANGER MODE ACTIVATED: %.1f%% usage exceeds %.1f%% threshold\n", 
                             usagePercent, SPIFFS_DANGER_THRESHOLD);
                
                if (isRecording) {
                    Serial.println("Auto-stopping recording due to SPIFFS danger mode");
                    stopRecording();
                }
            } else if (spiffsDangerMode && usagePercent <= SPIFFS_SAFE_THRESHOLD) {
                spiffsDangerMode = false;
                Serial.printf("SPIFFS DANGER MODE DEACTIVATED: %.1f%% usage below %.1f%% threshold\n", 
                             usagePercent, SPIFFS_SAFE_THRESHOLD);
            }
            
            if (previousDangerMode != spiffsDangerMode || 
                (now - lastStatusLog > 300000) || 
                usagePercent > 80) {
                
                const char* modeStr = spiffsDangerMode ? "DANGER" : "OK";
                Serial.printf("SPIFFS Health: %.1f%% used (%u/%u bytes) - %s\n", 
                             usagePercent, usedBytes, totalBytes, modeStr);
                lastStatusLog = now;
            }
        } else {
            isMounted = false;
        }
    } catch (...) {
        isMounted = false;
    }
    
    if (!isMounted) {
        consecutiveFailures++;
        Serial.printf("SPIFFS health check failed (consecutive failures: %d)\n", consecutiveFailures);
        
        if (consecutiveFailures >= 2) {
            Serial.println("Attempting SPIFFS remount due to repeated failures...");
            
            if (csvFileOpen && csvFile) {
                csvFile.close();
                csvFileOpen = false;
            }
            
            SPIFFS.end();
            delay(500);
            
            if (initializeSPIFFSWithRetry()) {
                Serial.println("SPIFFS remounted successfully");
                consecutiveFailures = 0;
                
                if (!csvFileName.isEmpty()) {
                    initializeCSVFile();
                }
            }
        }
    }
}

void SystemManager::initializeCSVFile() {
    if (chipId.isEmpty()) {
        Serial.println("ERROR: Chip ID not initialized!");
        return;
    }
    
    if (!SPIFFS.exists(csvFileName)) {
        File file = SPIFFS.open(csvFileName, "w");
        if (file) {
            file.println("ChipID,SessionID,Timestamp,RawValue,ScaledValue,State,IsGood,CompressionPeak,RecoilMin,Rate,CCF");
            file.flush();
            file.close();
            Serial.printf("Created new CSV file: %s with headers\n", csvFileName.c_str());
        }
    }
}

bool SystemManager::openCSVFile() {
    if (csvFileOpen) {
        return true;
    }
    
    csvFile = SPIFFS.open(csvFileName, "a");
    if (csvFile) {
        csvFileOpen = true;
        csvWriteCount = 0;
        
        unsigned long now = millis();
        csvFile.printf("# Session %d started at %lu\n", currentSessionId, now);
        csvFile.flush();
        
        return true;
    }
    return false;
}

void SystemManager::closeCSVFile() {
    if (csvFileOpen && csvFile) {
        unsigned long now = millis();
        csvFile.printf("# Session %d ended at %lu\n", currentSessionId, now);
        csvFile.flush();
        csvFile.close();
        csvFileOpen = false;
        csvWriteCount = 0;
        
        // Trigger cloud sync if enabled
        CloudConfig cloudConfig = cloudManager->getConfig();
        if (cloudConfig.enabled) {
            Serial.println("Triggering cloud sync after session end...");
        }
    }
}

void SystemManager::writeCSVData(int sessionId, unsigned long timestamp, int rawValue, const CPRStatus& status) {
    if (!csvFileOpen || !csvFile) {
        return;
    }
    
    int scaledValue = map(rawValue, 0, 4095, 0, 1023);
    
    String state = status.state;
    bool isGood = false;
    float compressionPeak = 0;
    float recoilMin = 0;
    
    if (state == "compression") {
        isGood = status.currentCompression.isGood;
        compressionPeak = status.currentCompression.peakValue;
    } else if (state == "recoil") {
        isGood = status.currentRecoil.isGood;
        recoilMin = status.currentRecoil.minValue;
    }
    
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
    
    if (csvWriteCount % 20 == 0 || (millis() - lastCSVWrite) > 1000) {
        csvFile.flush();
    }
}

void SystemManager::handleCSVLogging(unsigned long currentTime, int potValue, const CPRStatus& status) {
    if (spiffsDangerMode) {
        return;
    }
    
    if (isRecording && csvFileOpen && (currentTime - lastCSVWrite >= CSV_WRITE_INTERVAL)) {
        writeCSVData(currentSessionId, currentTime, potValue, status);
        lastCSVWrite = currentTime;
    }
}

void SystemManager::updateStatusLED(const String& state) {
    static unsigned long lastLEDUpdate = 0;
    static bool ledState = false;
    unsigned long now = millis();
    
    if (state == "compression") {
        if (now - lastLEDUpdate > 100) {
            ledState = !ledState;
            digitalWrite(LED_PIN, ledState ? HIGH : LOW);
            lastLEDUpdate = now;
        }
    } else if (state == "recoil") {
        digitalWrite(LED_PIN, HIGH);
    } else {
        digitalWrite(LED_PIN, LOW);
    }
}

void SystemManager::processAudioAlerts(const std::vector<String>& alerts) {
    if (alerts.empty() || isCurrentlyPlayingAudio) return;
    
    unsigned long now = millis();
    if (now - lastAudioEndTime < MIN_AUDIO_GAP) return;
    
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

void SystemManager::playAlertAudio(const String& alert) {
    if (isCurrentlyPlayingAudio) return;
    
    unsigned long duration = 500;
    unsigned int frequency = 1000;
    
    if (alert == "rateTooLow") frequency = 800;
    else if (alert == "rateTooHigh") frequency = 1200;
    else if (alert == "depthTooLow") frequency = 600;
    else if (alert == "depthTooHigh") frequency = 1400;
    else if (alert == "incompleteRecoil") frequency = 900;
    
    tone(AUDIO_PIN, frequency, duration);
    
    isCurrentlyPlayingAudio = true;
    lastAudioEndTime = millis() + duration;
}

void SystemManager::broadcastStateUpdate(const CPRStatus& status) {
    AsyncWebSocket* webSocket = webServerSetup->getWebSocket();
    if (webSocket->count() == 0) return;
    
    JsonDocument doc;
    doc["type"] = "metrics";
    doc["timestamp"] = status.timestamp;
    doc["state"] = status.state;
    doc["rate"] = status.currentRate;
    doc["value"] = status.rawValue;
    doc["peak"] = status.peakValue;
    
    doc["good_compressions"] = status.peaks.good;
    doc["total_compressions"] = status.peaks.total;
    doc["compression_ratio"] = status.peaks.ratio;
    doc["good_recoils"] = status.troughs.goodRecoil;
    doc["total_recoils"] = status.troughs.total;
    doc["recoil_ratio"] = status.troughs.ratio;
    doc["ccf"] = status.ccf;
    doc["cycles"] = status.cycles;
    
    JsonArray alerts = doc["alerts"].to<JsonArray>();
    for (const String& alert : status.alerts) {
        alerts.add(alert);
    }
    
    String message;
    serializeJson(doc, message);
    webSocket->textAll(message);
}

void SystemManager::broadcastAnimationState(const String& state) {
    AsyncWebSocket* animWebSocket = webServerSetup->getAnimWebSocket();
    if (animWebSocket->count() == 0) return;
    
    JsonDocument doc;
    doc["type"] = "animation";
    doc["state"] = state;
    doc["timestamp"] = millis();
    
    String message;
    serializeJson(doc, message);
    animWebSocket->textAll(message);
}

void SystemManager::broadcastNetworkStatus() {
    AsyncWebSocket* webSocket = webServerSetup->getWebSocket();
    if (webSocket->count() == 0) return;
    
    CloudConfig cloudConfig = cloudManager->getConfig();
    
    JsonDocument doc;
    doc["type"] = "network_status";
    doc["wifi_connected"] = wifiConfigManager->isWiFiConnected();
    doc["wifi_ssid"] = wifiConfigManager->getSSID();
    doc["wifi_rssi"] = wifiConfigManager->getRSSI();
    doc["hotspot_active"] = wifiConfigManager->isHotspotActive();
    doc["hotspot_ssid"] = wifiConfigManager->getAPSSID();
    doc["cloud_enabled"] = cloudConfig.enabled;
    doc["cloud_sync_in_progress"] = cloudManager->isSyncInProgress();
    doc["timestamp"] = millis();
    
    if (wifiConfigManager->isWiFiConnected()) {
        doc["ip_address"] = wifiConfigManager->getLocalIP().toString();
    }
    
    String message;
    serializeJson(doc, message);
    webSocket->textAll(message);
}

void SystemManager::broadcastDangerStatus() {
    AsyncWebSocket* webSocket = webServerSetup->getWebSocket();
    if (webSocket->count() == 0) return;
    
    unsigned long now = millis();
    if (spiffsDangerMode && (now - lastDangerBlink >= 1000)) {
        dangerBlinkState = !dangerBlinkState;
        lastDangerBlink = now;
        
        CloudConfig cloudConfig = cloudManager->getConfig();
        
        JsonDocument doc;
        doc["type"] = "spiffs_danger";
        doc["danger_mode"] = true;
        doc["blink_state"] = dangerBlinkState;
        doc["message"] = "Please enable Cloud Upload. No further operations possible.";
        doc["cloud_enabled"] = cloudConfig.enabled;
        doc["timestamp"] = now;
        
        String message;
        serializeJson(doc, message);
        webSocket->textAll(message);
    }
}

// WebSocket event handlers
void SystemManager::onWebSocketEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len) {
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

void SystemManager::onAnimWebSocketEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len) {
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