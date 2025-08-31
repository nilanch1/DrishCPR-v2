#include "DatabaseManager.h"
#include <time.h>

DatabaseManager::DatabaseManager() {
    currentSessionId = 0;
    dbInitialized = false;
    nextEventId = 1;
}

DatabaseManager::~DatabaseManager() {
    close();
}

bool DatabaseManager::initialize() {
    if (dbInitialized) {
        return true;
    }
    
    Serial.println("Initializing file-based database...");
    
    if (!SPIFFS.begin(true)) {
        Serial.println("Failed to mount SPIFFS");
        return false;
    }
    
    loadSessionsFromFile();
    
    dbInitialized = true;
    Serial.println("Database initialized successfully");
    return true;
}

void DatabaseManager::close() {
    if (dbInitialized) {
        saveSessionsToFile();
        dbInitialized = false;
    }
}

bool DatabaseManager::loadSessionsFromFile() {
    sessions.clear();
    events.clear();
    
    // Load sessions
    if (SPIFFS.exists(sessionFile)) {
        File file = SPIFFS.open(sessionFile, "r");
        if (file) {
            JsonDocument doc;
            deserializeJson(doc, file);
            file.close();
            
            JsonArray sessionsArray = doc["sessions"];
            for (JsonObject obj : sessionsArray) {
                SessionData session;
                session.sessionId = obj["sessionId"];
                session.startTime = obj["startTime"].as<String>();
                session.endTime = obj["endTime"].as<String>();
                session.rateAvg = obj["rateAvg"];
                session.depthAvg = obj["depthAvg"];
                session.goodCompressions = obj["goodCompressions"];
                session.totalCompressions = obj["totalCompressions"];
                session.syncStatus = obj["syncStatus"];
                sessions.push_back(session);
            }
        }
    }
    
    // Load events
    if (SPIFFS.exists(eventsFile)) {
        File file = SPIFFS.open(eventsFile, "r");
        if (file) {
            JsonDocument doc;
            deserializeJson(doc, file);
            file.close();
            
            JsonArray eventsArray = doc["events"];
            for (JsonObject obj : eventsArray) {
                CompressionEvent event;
                event.id = obj["id"];
                event.sessionId = obj["sessionId"];
                event.timestamp = obj["timestamp"].as<String>();
                event.value = obj["value"];
                event.state = obj["state"].as<String>();
                event.isGood = obj["isGood"];
                events.push_back(event);
                
                nextEventId = max(nextEventId, event.id + 1);
            }
        }
    }
    
    Serial.printf("Loaded %d sessions and %d events\n", sessions.size(), events.size());
    return true;
}

bool DatabaseManager::saveSessionsToFile() {
    // Save sessions
    JsonDocument doc;
    JsonArray sessionsArray = doc["sessions"].to<JsonArray>();
    
    for (const auto& session : sessions) {
        JsonObject obj = sessionsArray.add<JsonObject>();
        obj["sessionId"] = session.sessionId;
        obj["startTime"] = session.startTime;
        obj["endTime"] = session.endTime;
        obj["rateAvg"] = session.rateAvg;
        obj["depthAvg"] = session.depthAvg;
        obj["goodCompressions"] = session.goodCompressions;
        obj["totalCompressions"] = session.totalCompressions;
        obj["syncStatus"] = session.syncStatus;
    }
    
    File file = SPIFFS.open(sessionFile, "w");
    if (file) {
        serializeJson(doc, file);
        file.close();
    } else {
        Serial.println("Failed to save sessions file");
        return false;
    }
    
    // Save events (limit to last 1000 events to prevent file getting too large)
    JsonDocument eventsDoc;
    JsonArray eventsArray = eventsDoc["events"].to<JsonArray>();
    
    size_t startIndex = events.size() > 1000 ? events.size() - 1000 : 0;
    for (size_t i = startIndex; i < events.size(); i++) {
        JsonObject obj = eventsArray.add<JsonObject>();
        obj["id"] = events[i].id;
        obj["sessionId"] = events[i].sessionId;
        obj["timestamp"] = events[i].timestamp;
        obj["value"] = events[i].value;
        obj["state"] = events[i].state;
        obj["isGood"] = events[i].isGood;
    }
    
    file = SPIFFS.open(eventsFile, "w");
    if (file) {
        serializeJson(eventsDoc, file);
        file.close();
        return true;
    } else {
        Serial.println("Failed to save events file");
        return false;
    }
}

int DatabaseManager::startNewSession() {
    if (!dbInitialized && !initialize()) {
        return -1;
    }
    
    // Find next session ID
    int maxSessionId = 0;
    for (const auto& session : sessions) {
        maxSessionId = max(maxSessionId, session.sessionId);
    }
    currentSessionId = maxSessionId + 1;
    
    // Create new session
    SessionData newSession;
    newSession.sessionId = currentSessionId;
    
    time_t now = time(nullptr);
    struct tm* timeinfo = localtime(&now);
    char timestamp[32];
    strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", timeinfo);
    newSession.startTime = String(timestamp);
    
    newSession.endTime = "";
    newSession.rateAvg = 0;
    newSession.depthAvg = 0;
    newSession.goodCompressions = 0;
    newSession.totalCompressions = 0;
    newSession.syncStatus = 0;
    
    sessions.push_back(newSession);
    saveSessionsToFile();
    
    Serial.printf("Started new session: %d\n", currentSessionId);
    return currentSessionId;
}

void DatabaseManager::endCurrentSession() {
    if (currentSessionId <= 0 || !dbInitialized) {
        return;
    }
    
    // Find and update the current session
    for (auto& session : sessions) {
        if (session.sessionId == currentSessionId) {
            time_t now = time(nullptr);
            struct tm* timeinfo = localtime(&now);
            char timestamp[32];
            strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", timeinfo);
            session.endTime = String(timestamp);
            
            saveSessionsToFile();
            Serial.printf("Ended session: %d\n", currentSessionId);
            break;
        }
    }
    
    currentSessionId = 0;
}

bool DatabaseManager::recordCompressionEvent(unsigned long timestamp, float value, 
                                           const String& state, bool isGood) {
    if (currentSessionId <= 0 || !dbInitialized) {
        return false;
    }
    
    CompressionEvent event;
    event.id = nextEventId++;
    event.sessionId = currentSessionId;
    
    // Convert timestamp to readable format
    time_t timeSeconds = timestamp / 1000;
    int milliseconds = timestamp % 1000;
    struct tm* timeinfo = localtime(&timeSeconds);
    char timeStr[32];
    snprintf(timeStr, sizeof(timeStr), "%04d-%02d-%02d %02d:%02d:%02d.%03d",
             timeinfo->tm_year + 1900, timeinfo->tm_mon + 1, timeinfo->tm_mday,
             timeinfo->tm_hour, timeinfo->tm_min, timeinfo->tm_sec, milliseconds);
    
    event.timestamp = String(timeStr);
    event.value = value;
    event.state = state;
    event.isGood = isGood;
    
    events.push_back(event);
    
    // Save periodically (every 100 events) to prevent data loss
    if (events.size() % 100 == 0) {
        saveSessionsToFile();
    }
    
    return true;
}

std::vector<SessionData> DatabaseManager::getUnSyncedSessions() {
    std::vector<SessionData> unsynced;
    
    for (const auto& session : sessions) {
        if (session.syncStatus == 0 && !session.endTime.isEmpty()) {
            unsynced.push_back(session);
        }
    }
    
    return unsynced;
}

std::vector<SessionData> DatabaseManager::getAllSessions(int limit) {
    std::vector<SessionData> result;
    
    // Return last 'limit' sessions
    int startIndex = sessions.size() > limit ? sessions.size() - limit : 0;
    for (size_t i = startIndex; i < sessions.size(); i++) {
        result.push_back(sessions[i]);
    }
    
    return result;
}

std::vector<CompressionEvent> DatabaseManager::getSessionEvents(int sessionId) {
    std::vector<CompressionEvent> sessionEvents;
    
    for (const auto& event : events) {
        if (event.sessionId == sessionId) {
            sessionEvents.push_back(event);
        }
    }
    
    return sessionEvents;
}

std::tuple<bool, String> DatabaseManager::needsSync(int rowThreshold, int timeThresholdHours) {
    if (!dbInitialized) {
        return std::make_tuple(false, "Database not initialized");
    }
    
    int unsyncedCount = 0;
    for (const auto& session : sessions) {
        if (session.syncStatus == 0 && !session.endTime.isEmpty()) {
            unsyncedCount++;
        }
    }
    
    if (unsyncedCount >= rowThreshold) {
        return std::make_tuple(true, String(unsyncedCount) + " unsynced sessions");
    }
    
    if (unsyncedCount > 0) {
        return std::make_tuple(true, "Has unsynced sessions");
    }
    
    return std::make_tuple(false, "No sync needed");
}

bool DatabaseManager::markSessionsAsSynced(const std::vector<int>& sessionIds) {
    if (!dbInitialized || sessionIds.empty()) {
        return false;
    }
    
    for (auto& session : sessions) {
        for (int id : sessionIds) {
            if (session.sessionId == id) {
                session.syncStatus = 1;
            }
        }
    }
    
    saveSessionsToFile();
    Serial.printf("Marked %d sessions as synced\n", sessionIds.size());
    return true;
}

String DatabaseManager::createBackup() {
    if (!dbInitialized) {
        return "";
    }
    
    time_t now = time(nullptr);
    struct tm* timeinfo = localtime(&now);
    char backupName[64];
    snprintf(backupName, sizeof(backupName), "/backup_%04d%02d%02d_%02d%02d%02d.json",
             timeinfo->tm_year + 1900, timeinfo->tm_mon + 1, timeinfo->tm_mday,
             timeinfo->tm_hour, timeinfo->tm_min, timeinfo->tm_sec);
    
    // Create combined backup file
    JsonDocument doc;
    
    JsonArray sessionsArray = doc["sessions"].to<JsonArray>();
    for (const auto& session : sessions) {
        JsonObject obj = sessionsArray.add<JsonObject>();
        obj["sessionId"] = session.sessionId;
        obj["startTime"] = session.startTime;
        obj["endTime"] = session.endTime;
        obj["rateAvg"] = session.rateAvg;
        obj["depthAvg"] = session.depthAvg;
        obj["goodCompressions"] = session.goodCompressions;
        obj["totalCompressions"] = session.totalCompressions;
        obj["syncStatus"] = session.syncStatus;
    }
    
    JsonArray eventsArray = doc["events"].to<JsonArray>();
    for (const auto& event : events) {
        JsonObject obj = eventsArray.add<JsonObject>();
        obj["id"] = event.id;
        obj["sessionId"] = event.sessionId;
        obj["timestamp"] = event.timestamp;
        obj["value"] = event.value;
        obj["state"] = event.state;
        obj["isGood"] = event.isGood;
    }
    
    File backup = SPIFFS.open(backupName, "w");
    if (backup) {
        serializeJson(doc, backup);
        backup.close();
        Serial.printf("Created backup: %s\n", backupName);
        return String(backupName);
    }
    
    return "";
}

int DatabaseManager::getTotalSessions() {
    return sessions.size();
}

int DatabaseManager::getTotalEvents() {
    return events.size();
}

SessionData DatabaseManager::getLatestSession() {
    if (!sessions.empty()) {
        return sessions.back();
    }
    return SessionData{};
}