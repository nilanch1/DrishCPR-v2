#ifndef SYSTEM_MANAGER_H
#define SYSTEM_MANAGER_H

#include <SPIFFS.h>
#include <AsyncWebSocket.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include "CPRMetricsCalculator.h"
#include "DatabaseManager.h"
#include "NetworkManager.h"
#include "WiFiConfigManager.h"
#include "CloudManager.h"
// Remove: #include "WebServerSetup.h" - use forward declaration instead

// Forward declaration
class WebServerSetup;

// Hardware pins
#define POTENTIOMETER_PIN 36
#define AUDIO_PIN 25
#define LED_PIN 2

class SystemManager {
private:
    // Core system components
    CPRMetricsCalculator* metricsCalculator;
    DatabaseManager* dbManager;
    NetworkManager* networkManager;
    WiFiConfigManager* wifiConfigManager;
    CloudManager* cloudManager;
    WebServerSetup* webServerSetup;  // Forward declared type
    
    // System state
    String chipId;
    String csvFileName;
    bool isRecording = false;
    int currentSessionId = 0;
    bool fileUploadInProgress = false;
    bool spiffsDangerMode = false;
    
    // CSV management
    File csvFile;
    bool csvFileOpen = false;
    int csvWriteCount = 0;
    
    // Session tracking
    Preferences sessionPrefs;
    int lastSessionNumber = 0;
    
    // Timing variables
    unsigned long lastPotRead = 0;
    unsigned long lastDataSend = 0;
    unsigned long lastAnimSend = 0;
    unsigned long lastCSVWrite = 0;
    unsigned long lastDangerBlink = 0;
    bool dangerBlinkState = false;
    
    // Audio management
    unsigned long lastAudioEndTime = 0;
    bool isCurrentlyPlayingAudio = false;
    
    // Static constants (declared here, defined in .cpp)
    static const float SPIFFS_DANGER_THRESHOLD;
    static const float SPIFFS_SAFE_THRESHOLD;
    static const unsigned long POT_READ_INTERVAL;
    static const unsigned long DATA_SEND_INTERVAL;
    static const unsigned long ANIM_SEND_INTERVAL;
    static const unsigned long CSV_WRITE_INTERVAL;
    static const unsigned long MIN_AUDIO_GAP;
    static const int MAX_WS_CLIENTS;

public:
    SystemManager();
    ~SystemManager();
    
    void initialize();
    void loop();
    
    // Public accessors for web server
    WiFiConfigManager* getWiFiManager() const { return wifiConfigManager; }
    CloudManager* getCloudManager() const { return cloudManager; }
    CPRMetricsCalculator* getMetricsCalculator() const { return metricsCalculator; }
    DatabaseManager* getDbManager() const { return dbManager; }
    NetworkManager* getNetworkManager() const { return networkManager; }
    
    // System state accessors
    String getChipId() const { return chipId; }
    String getCsvFileName() const { return csvFileName; }
    bool getIsRecording() const { return isRecording; }
    int getCurrentSessionId() const { return currentSessionId; }
    int getNextSessionNumber() const { return lastSessionNumber + 1; }
    bool isCsvFileOpen() const { return csvFileOpen; }
    int getCsvWriteCount() const { return csvWriteCount; }
    bool isInDangerMode() const { return spiffsDangerMode; }
    
    // Recording control
    bool startRecording();
    bool stopRecording();
    
    // File operations
    bool deleteCSVFile();
    bool isCSVFileEmpty();

private:
    // Initialization methods
    bool initializeSPIFFSWithRetry();
    void initializeChipId();
    void initializeSessionTracking();
    void setupCSVSystem();
    void checkRequiredFiles();
    
    // SPIFFS management
    void checkSPIFFSHealth();
    bool isOperationAllowed();
    
    // CSV management
    void initializeCSVFile();
    bool openCSVFile();
    void closeCSVFile();
    void writeCSVData(int sessionId, unsigned long timestamp, int rawValue, const CPRStatus& status);
    void handleCSVLogging(unsigned long currentTime, int potValue, const CPRStatus& status);
    
    // Hardware control
    void updateStatusLED(const String& state);
    void processAudioAlerts(const std::vector<String>& alerts);
    void playAlertAudio(const String& alert);
    
    // WebSocket broadcasting
    void broadcastStateUpdate(const CPRStatus& status);
    void broadcastAnimationState(const String& state);
    void broadcastNetworkStatus();
    void broadcastDangerStatus();
    
    // WebSocket event handlers
    static void onWebSocketEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len);
    static void onAnimWebSocketEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len);
};

#endif