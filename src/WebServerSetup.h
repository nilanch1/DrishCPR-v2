#ifndef WEB_SERVER_SETUP_H
#define WEB_SERVER_SETUP_H

#include <ESPAsyncWebServer.h>
#include <AsyncWebSocket.h>
#include <ArduinoJson.h>
#include <SPIFFS.h>

// Forward declaration instead of full include
class SystemManager;

class WebServerSetup {
private:
    SystemManager* systemManager;
    AsyncWebServer server;
    AsyncWebSocket webSocket;
    AsyncWebSocket animWebSocket;

public:
    WebServerSetup(SystemManager* sysManager);
    void setup();
    
    // Accessors
    AsyncWebServer* getServer() { return &server; }
    AsyncWebSocket* getWebSocket() { return &webSocket; }
    AsyncWebSocket* getAnimWebSocket() { return &animWebSocket; }

private:
    // Route setup methods (declarations only)
    void setupMainRoutes();
    void setupCloudRoutes();
    void setupWiFiRoutes();
    void setupDataRoutes();
    void setupConfigRoutes();
    void setupStatusRoutes();
    void setupStaticRoutes();
    
    // WebSocket event handlers
    static void onWebSocketEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len);
    static void onAnimWebSocketEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len);
};

#endif