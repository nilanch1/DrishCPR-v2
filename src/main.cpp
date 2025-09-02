#include "WiFiConfigManager.h"
#include "CloudManager.h"
#include "WebServerSetup.h"
#include "SystemManager.h"

// Hardware Configuration
#define POTENTIOMETER_PIN 36
#define AUDIO_PIN 25
#define LED_PIN 2

// Global system components
SystemManager* systemManager;

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
    
    // Initialize system manager (handles all subsystems)
    systemManager = new SystemManager();
    systemManager->initialize();
    
    Serial.println("Setup complete.");
    Serial.println("📱 Access the dashboard via hotspot or WiFi connection");
    Serial.println("🔧 WiFi configuration available at /ssid_config");
    Serial.println("☁️ Cloud configuration available at /cloud_config");
    Serial.println("🛠 Debug info available at /debug");
    Serial.println("🌐 Internet status available at /internet_status");
    Serial.println("✅ Enhanced WiFi and Cloud Configuration System Ready!");
}

void loop() {
    systemManager->loop();
}