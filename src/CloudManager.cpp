#include "CloudManager.h"

// Declare the external variable
extern bool fileUploadInProgress;

CloudManager::CloudManager() {
}

void CloudManager::initialize() {
    prefs.begin("cloud", false);
    
    config.provider = prefs.getString("provider", "");
    config.accessKey = prefs.getString("accessKey", "");
    config.secretKey = prefs.getString("secretKey", "");
    config.bucketName = prefs.getString("bucket", "");
    config.endpointUrl = prefs.getString("endpoint", "");
    config.syncFrequency = prefs.getInt("frequency", 60);
    config.enabled = prefs.getBool("enabled", false);
    config.lastSyncTime = prefs.getULong("lastSync", 0);
    config.syncedSessions = prefs.getInt("syncedSessions", 0);
    
    Serial.println("Cloud configuration loaded:");
    Serial.printf("  Provider: %s\n", config.provider.c_str());
    Serial.printf("  Enabled: %s\n", config.enabled ? "Yes" : "No");
    Serial.printf("  Sync Frequency: %d minutes\n", config.syncFrequency);
    Serial.printf("  Last Sync: %lu\n", config.lastSyncTime);
}

void CloudManager::saveConfig() {
    prefs.putString("provider", config.provider);
    prefs.putString("accessKey", config.accessKey);
    prefs.putString("secretKey", config.secretKey);
    prefs.putString("bucket", config.bucketName);
    prefs.putString("endpoint", config.endpointUrl);
    prefs.putInt("frequency", config.syncFrequency);
    prefs.putBool("enabled", config.enabled);
    prefs.putULong("lastSync", config.lastSyncTime);
    prefs.putInt("syncedSessions", config.syncedSessions);
    
    Serial.println("Cloud configuration saved");
}

bool CloudManager::updateConfig(const JsonDocument& configDoc) {
    String provider = configDoc["provider"] | "";
    String accessKey = configDoc["access_key"] | "";
    String secretKey = configDoc["secret_key"] | "";
    String bucket = configDoc["bucket"] | "";
    String endpoint = configDoc["endpoint"] | "";
    int frequency = configDoc["frequency"].as<int>();
    
    if (frequency == 0) frequency = 60;
    
    if (provider.isEmpty() || accessKey.isEmpty() || secretKey.isEmpty() || bucket.isEmpty()) {
        return false;
    }
    
    if (provider != "digitalocean" && provider != "aws") {
        return false;
    }
    
    if (frequency < 5 || frequency > 1440) {
        return false;
    }
    
    config.provider = provider;
    config.accessKey = accessKey;
    config.secretKey = secretKey;
    config.bucketName = bucket;
    config.endpointUrl = endpoint;
    config.syncFrequency = frequency;
    config.enabled = true;
    
    saveConfig();
    return true;
}

void CloudManager::performSync() {
    if (syncInProgress || !config.enabled) {
        return;
    }

    unsigned long now = millis();
    unsigned long syncInterval = config.syncFrequency * 60000UL;

    if (now - config.lastSyncTime < syncInterval) return;
    if (now - lastSyncAttempt < CLOUD_SYNC_RETRY_INTERVAL) return;

    lastSyncAttempt = now;
    syncInProgress = true;
    fileUploadInProgress = true; // Set the global flag

    Serial.println("Starting cloud sync...");

    // Get chip ID for filename
    uint64_t chipid = ESP.getEfuseMac();
    String chipId = String((uint32_t)(chipid >> 32), HEX) + String((uint32_t)chipid, HEX);
    chipId.toUpperCase();
    String csvFileName = "/" + chipId + ".csv";

    if (SPIFFS.exists(csvFileName)) {
        if (isFileEmpty(csvFileName)) {
            Serial.println("📄 CSV file is empty - skipping upload");
            config.lastSyncTime = now;
            saveConfig();
        } else {
            String cloudFileName = chipId + "_" + String(config.syncedSessions + 1) + ".csv";
            if (uploadFile(cloudFileName, csvFileName)) {
                config.lastSyncTime = now;
                config.syncedSessions++;
                saveConfig();
                Serial.println("☁️ Cloud sync completed successfully");
            } else {
                Serial.println("❌ Cloud sync failed");
            }
        }
    } else {
        Serial.println("📄 No CSV file found - skipping upload");
        config.lastSyncTime = now;
        saveConfig();
    }

    syncInProgress = false;
    fileUploadInProgress = false; // Clear the global flag
}

bool CloudManager::isFileEmpty(const String& filePath) {
    if (!SPIFFS.exists(filePath)) {
        return true;
    }
    
    File file = SPIFFS.open(filePath, "r");
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

bool CloudManager::uploadFile(const String& fileName, const String& localFilePath) {
    if (!config.enabled || config.provider.isEmpty()) {
        Serial.println("☁️ Cloud upload disabled or not configured");
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
    client.setInsecure();
    client.setTimeout(30000);

    HTTPClient http;
    String host = config.bucketName + "." + config.endpointUrl;
    String uri = "/" + fileName;
    String uploadUrl = "https://" + host + uri;

    if (!http.begin(client, uploadUrl)) {
        Serial.println("❌ Failed to begin HTTP connection");
        f.close();
        return false;
    }

    String authHeader = generateAWSv4Signature("PUT", uri, host, "text/csv", "",
                                               config.accessKey, config.secretKey, true);

    String datetime = getAWSDateTime();

    http.addHeader("Authorization", authHeader);
    http.addHeader("x-amz-date", datetime);
    http.addHeader("x-amz-content-sha256", "UNSIGNED-PAYLOAD");
    http.addHeader("Host", host);
    http.addHeader("Content-Type", "text/csv");
    http.addHeader("Content-Length", String(fileSize));

    Serial.println("🚀 Starting upload (streaming)...");

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
    } else {
        Serial.println("❌ Upload failed - keeping local file");
    }

    return uploadResult;
}

bool CloudManager::testConnection() {
    if (config.provider.isEmpty() || config.accessKey.isEmpty()) {
        return false;
    }
    
    String testContent = "test," + String(millis()) + "\n";
    uint64_t chipid = ESP.getEfuseMac();
    String chipId = String((uint32_t)(chipid >> 32), HEX) + String((uint32_t)chipid, HEX);
    chipId.toUpperCase();
    String testFileName = chipId + "_test_" + String(millis()) + ".csv";
    
    return uploadFile(testFileName, testContent);
}

unsigned long CloudManager::getNextSyncIn() const {
    if (!config.enabled) return 0;
    unsigned long syncInterval = config.syncFrequency * 60000UL;
    unsigned long timeSinceLastSync = millis() - config.lastSyncTime;
    if (timeSinceLastSync >= syncInterval) return 0;
    return syncInterval - timeSinceLastSync;
}

// Helper functions implementation
void CloudManager::hmacSha256(const uint8_t* key, size_t keyLen, const uint8_t* data, size_t dataLen, uint8_t* result) {
    mbedtls_md_context_t ctx;
    mbedtls_md_type_t md_type = MBEDTLS_MD_SHA256;
    
    mbedtls_md_init(&ctx);
    mbedtls_md_setup(&ctx, mbedtls_md_info_from_type(md_type), 1);
    mbedtls_md_hmac_starts(&ctx, key, keyLen);
    mbedtls_md_hmac_update(&ctx, data, dataLen);
    mbedtls_md_hmac_finish(&ctx, result);
    mbedtls_md_free(&ctx);
}

String CloudManager::bytesToHex(const uint8_t* bytes, size_t length) {
    String hex = "";
    for (size_t i = 0; i < length; i++) {
        if (bytes[i] < 16) hex += "0";
        hex += String(bytes[i], HEX);
    }
    hex.toLowerCase();
    return hex;
}

String CloudManager::sha256(const String& data) {
    mbedtls_sha256_context ctx;
    uint8_t result[32];
    
    mbedtls_sha256_init(&ctx);
    mbedtls_sha256_starts(&ctx, 0);
    mbedtls_sha256_update(&ctx, (const unsigned char*)data.c_str(), data.length());
    mbedtls_sha256_finish(&ctx, result);
    mbedtls_sha256_free(&ctx);
    
    return bytesToHex(result, 32);
}

String CloudManager::getAWSDateTime() {
    time_t now = time(nullptr);
    struct tm* timeinfo = gmtime(&now);
    char datetime[20];
    strftime(datetime, sizeof(datetime), "%Y%m%dT%H%M%SZ", timeinfo);
    return String(datetime);
}

String CloudManager::getAWSDate() {
    time_t now = time(nullptr);
    struct tm* timeinfo = gmtime(&now);
    char date[10];
    strftime(date, sizeof(date), "%Y%m%d", timeinfo);
    return String(date);
}

String CloudManager::generateAWSv4Signature(const String& method, const String& uri, const String& host,
                             const String& contentType, const String& payload,
                             const String& accessKey, const String& secretKey,
                             bool unsignedPayload) {
    String datetime = getAWSDateTime();
    String date = getAWSDate();

    String region = "us-east-1"; // default
    int dotIndex = host.indexOf(".");
    if (dotIndex > 0) {
        region = host.substring(dotIndex + 1, host.indexOf(".", dotIndex + 1));
    }

    String service = "s3";

    String payloadHash = unsignedPayload ? "UNSIGNED-PAYLOAD" : sha256(payload);

    String canonicalHeaders = "host:" + host + "\n" +
                             "x-amz-content-sha256:" + payloadHash + "\n" +
                             "x-amz-date:" + datetime + "\n";

    String signedHeaders = "host;x-amz-content-sha256;x-amz-date";

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