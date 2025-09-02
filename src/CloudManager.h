#ifndef CLOUD_MANAGER_H
#define CLOUD_MANAGER_H

#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <Preferences.h>
#include <SPIFFS.h>
#include <ArduinoJson.h>
#include <base64.h>
#include <time.h>
#include <mbedtls/md.h>
#include <mbedtls/sha256.h>

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

class CloudManager {
private:
    CloudConfig config;
    Preferences prefs;
    bool syncInProgress = false;
    unsigned long lastSyncAttempt = 0;
    static const unsigned long CLOUD_SYNC_RETRY_INTERVAL = 300000; // 5 minutes retry

    // Private helper methods
    String generateAWSv4Signature(const String& method, const String& uri, const String& host,
                                 const String& contentType, const String& payload,
                                 const String& accessKey, const String& secretKey,
                                 bool unsignedPayload = false);
    String sha256(const String& data);
    String bytesToHex(const uint8_t* bytes, size_t length);
    void hmacSha256(const uint8_t* key, size_t keyLen, const uint8_t* data, size_t dataLen, uint8_t* result);
    String getAWSDateTime();
    String getAWSDate();
    String getUploadUrl(const String& fileName);
    bool isFileEmpty(const String& filePath);

public:
    CloudManager();
    void initialize();
    void performSync();
    bool testConnection();
    bool uploadFile(const String& fileName, const String& localFilePath);
    
    // Configuration management
    void saveConfig();
    bool updateConfig(const JsonDocument& configDoc);
    CloudConfig getConfig() const { return config; }
    bool isSyncInProgress() const { return syncInProgress; }
    
    // Status
    unsigned long getTimeSinceLastSync() const { return millis() - config.lastSyncTime; }
    unsigned long getNextSyncIn() const;
};

#endif