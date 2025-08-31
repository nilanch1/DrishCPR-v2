#ifndef DATABASE_MANAGER_H
#define DATABASE_MANAGER_H

#include <Arduino.h>
#include <SPIFFS.h>
#include <ArduinoJson.h>
#include <vector>
#include <tuple>

struct SessionData {
    int sessionId;
    String startTime;
    String endTime;
    float rateAvg;
    float depthAvg;
    int goodCompressions;
    int totalCompressions;
    int syncStatus;
};

struct CompressionEvent {
    int id;
    int sessionId;
    String timestamp;
    float value;
    String state;
    bool isGood;
};

class DatabaseManager {
private:
    int currentSessionId;
    bool dbInitialized;
    String sessionFile = "/sessions.json";
    String eventsFile = "/events.json";
    
    bool saveSessionsToFile();
    bool loadSessionsFromFile();
    std::vector<SessionData> sessions;
    std::vector<CompressionEvent> events;
    int nextEventId;

public:
    DatabaseManager();
    ~DatabaseManager();
    
    bool initialize();
    void close();
    
    // Session management
    int startNewSession();
    void endCurrentSession();
    int getCurrentSessionId() const { return currentSessionId; }
    
    // Data recording
    bool recordCompressionEvent(unsigned long timestamp, float value, const String& state, bool isGood);
    
    // Data retrieval
    std::vector<SessionData> getUnSyncedSessions();
    std::vector<SessionData> getAllSessions(int limit = 100);
    std::vector<CompressionEvent> getSessionEvents(int sessionId);
    
    // Sync management
    std::tuple<bool, String> needsSync(int rowThreshold = 10, int timeThresholdHours = 24);
    bool markSessionsAsSynced(const std::vector<int>& sessionIds);
    
    // Database maintenance
    String createBackup();
    bool cleanupOldData(int keepDays = 30);
    int getDatabaseSize();
    int getRecordCount();
    
    // Statistics
    int getTotalSessions();
    int getTotalEvents();
    SessionData getLatestSession();
};

#endif