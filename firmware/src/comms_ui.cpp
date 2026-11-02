#include "comms_ui.h"
#include "globals.h"
#include <WiFi.h>
#include <WebSocketsServer.h>
#include <ESPmDNS.h>


// ---------------------------------------------------------------------
// WebSocket Server on port 81
// ---------------------------------------------------------------------
WebSocketsServer webSocket = WebSocketsServer(81);

// Track connected clients (for debug logging only)
static uint8_t connectedClients = 0;

// ---------------------------------------------------------------------
// WebSocket Event Handler
// Called by the library whenever a client connects, disconnects,
// or sends data. Runs in the context of webSocket.loop().
// ---------------------------------------------------------------------
static void webSocketEvent(uint8_t clientNum, WStype_t type,
                           uint8_t *payload, size_t length) {
    switch (type) {

        case WStype_DISCONNECTED:
            if (connectedClients > 0) {
                connectedClients--;
            }
            break;

        case WStype_CONNECTED: {
            connectedClients++;

            IPAddress ip = webSocket.remoteIP(clientNum);

            // Send a welcome/handshake string so the dashboard knows
            // it is talking to the correct MachineSense device
            webSocket.sendTXT(
                clientNum,
                "$MachineSense,STATUS,CONNECTED"
            );

            break;
        }

        case WStype_TEXT:
            // We don't expect incoming commands yet
            break;

        default:
            break;
    }
}

// ---------------------------------------------------------------------
// initCommsWiFi() — call once from setup()
// Connects to the local WiFi network and starts the WebSocket server.
// This MUST finish before the FreeRTOS scheduler is active, so it is
// safe to use a blocking while-loop here.
// ---------------------------------------------------------------------
void initCommsWiFi() {

    Serial.println();
    Serial.println("=================================");
    Serial.println("MachineSense Booting Up...");
    Serial.println("=================================");

    // --- WiFi Setup ---
    WiFi.mode(WIFI_STA);
    bool stationConnected = false;
    if (WIFI_SSID[0] != '\0') {
        WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

        Serial.print("Connecting to router Wi-Fi (2.4 GHz)");

        // Do not prevent the radar and detection tasks from starting when the
        // configured access point is unavailable.
        const uint32_t connectStarted = millis();
        while (WiFi.status() != WL_CONNECTED && millis() - connectStarted < 20000) {
            delay(500);
            Serial.print(".");
        }
        stationConnected = WiFi.status() == WL_CONNECTED;
        Serial.println();
        if (!stationConnected) {
            Serial.printf("Router Wi-Fi connection failed (status %d).\n", WiFi.status());
        }
    } else {
        Serial.println("Router Wi-Fi credentials are empty; starting fallback access point.");
    }
    Serial.println();
    if (stationConnected) {
        Serial.print("WiFi Connected! MachineSense ESP32 IP Address: ");
        Serial.println(WiFi.localIP());

        if (MDNS.begin("machinesense")) {
            Serial.println("mDNS started successfully!");
            Serial.println("MachineSense hostname: machinesense.local");
        } else {
            Serial.println("ERROR: mDNS failed to start!");
        }
    } else {
        // A fallback AP guarantees a reachable address even when router
        // credentials are unset or the router cannot be reached.
        const String suffix = WiFi.macAddress().substring(12);
        const String apName = String("MachineSense-") + suffix;
        const char *apPassword = AP_PASSWORD;
        WiFi.mode(WIFI_AP);
        if (WiFi.softAP(apName.c_str(), apPassword)) {
            Serial.println("Fallback Wi-Fi access point started.");
            Serial.print("Connect your computer/phone to: ");
            Serial.println(apName);
            Serial.print("Access point password: ");
            Serial.println(apPassword);
            Serial.print("ESP32 IP address: ");
            Serial.println(WiFi.softAPIP());
        } else {
            Serial.println("ERROR: Could not start fallback Wi-Fi access point.");
        }
    }

    // --- IP Address ---
    Serial.println();
    Serial.println("=================================");
    if (stationConnected) {
        Serial.print("WiFi Connected! MachineSense ESP32 IP Address: ");
        Serial.println(WiFi.localIP());
    } else if (WiFi.softAPIP() != IPAddress(0, 0, 0, 0)) {
        Serial.print("ESP32 IP Address (fallback access point): ");
        Serial.println(WiFi.softAPIP());
    }
    Serial.println("=================================");

    Serial.println("WebSocket Server running on port 81");

    if (stationConnected) {
        Serial.println("Open your browser and go to:");
        Serial.print("http://");
        Serial.println(WiFi.localIP());
    }

    Serial.println("=================================");

    // --- Start WebSocket Server ---
    webSocket.begin();
    webSocket.onEvent(webSocketEvent);
}

// ---------------------------------------------------------------------
// FreeRTOS Task — Communications & UI
// Priority 1 (lowest). Stack recommendation: 8192 bytes.
// ---------------------------------------------------------------------
void vCommsUITask(void *pvParameters) {

    // ---------------------------------------------------------------
    // 1. Initialize local hardware
    // ---------------------------------------------------------------
    pinMode(BUZZER_PIN, OUTPUT);
    pinMode(LED_PIN, OUTPUT);

    digitalWrite(BUZZER_PIN, LOW);
    digitalWrite(LED_PIN, LOW);

    // ---------------------------------------------------------------
    // Alarm state tracking
    // ---------------------------------------------------------------
    bool alarmActive = false;

    TickType_t alarmStartTick = 0;

    const TickType_t alarmDuration = pdMS_TO_TICKS(1500);

    DashboardTelemetry telemetry;

    // ---------------------------------------------------------------
    // Main Task Loop
    // ---------------------------------------------------------------
    for (;;) {

        // -----------------------------------------------------------
        // A. Service the WebSocket event loop
        // -----------------------------------------------------------
        webSocket.loop();

        // -----------------------------------------------------------
        // B. Check for new telemetry from the Detection Task
        // -----------------------------------------------------------
        if (xQueueReceive(
                dashboardQueue,
                &telemetry,
                pdMS_TO_TICKS(50)
            ) == pdTRUE) {

            // -------------------------------------------------------
            // Determine MachineSense system state
            // -------------------------------------------------------
            const char* stateStr =
                (telemetry.state == STATE_CALIBRATING)
                ? "CALIBRATING"
                : "ACTIVE";

            // -------------------------------------------------------
            // Build MachineSense telemetry packet
            //
            // Format:
            // $MachineSense,DATA,
            // breathingFreq,
            // heartbeatFreq,
            // breathingMag,
            // heartbeatMag,
            // confidenceLevel,
            // maxConfidence,
            // timestamp,
            // state,
            // scoreA,
            // scoreB,
            // scoreC,
            // fusedScore
            // -------------------------------------------------------
            char dataPacket[256];

            snprintf(
                dataPacket,
                sizeof(dataPacket),
                "$MachineSense,DATA,%.3f,%.3f,%.1f,%.1f,%d,%d,%lu,%s,%.3f,%.3f,%.3f,%.3f,%d",
                telemetry.breathingFreq,
                telemetry.heartbeatFreq,
                telemetry.breathingMag,
                telemetry.heartbeatMag,
                telemetry.confidenceLevel,
                telemetry.maxConfidence,
                millis(),
                stateStr,
                telemetry.scoreA,
                telemetry.scoreB,
                telemetry.scoreC,
                telemetry.fusedScore,
                telemetry.humanDetected ? 1 : 0
            );

            // -------------------------------------------------------
            // Broadcast telemetry to all connected WebSocket clients
            // -------------------------------------------------------
            webSocket.broadcastTXT(dataPacket);

            // -------------------------------------------------------
            // C. Handle MachineSense alert events
            // -------------------------------------------------------
            if (telemetry.alertTriggered) {

                // Turn ON local alarms
                digitalWrite(LED_PIN, HIGH);
                digitalWrite(BUZZER_PIN, HIGH);

                alarmActive = true;

                alarmStartTick = xTaskGetTickCount();

                // ---------------------------------------------------
                // Build MachineSense alert packet
                // ---------------------------------------------------
                char alertPacket[80];

                snprintf(
                    alertPacket,
                    sizeof(alertPacket),
                    "$MachineSense,ALERT,HUMAN_DETECTED,TIME:%lu",
                    millis()
                );

                // Broadcast alert to all connected WebSocket clients
                webSocket.broadcastTXT(alertPacket);
            }
        }

        // -----------------------------------------------------------
        // D. Auto-reset alarms after 1.5 seconds
        // -----------------------------------------------------------
        if (alarmActive &&
            (xTaskGetTickCount() - alarmStartTick) >= alarmDuration) {

            digitalWrite(LED_PIN, LOW);
            digitalWrite(BUZZER_PIN, LOW);

            alarmActive = false;
        }
    }
}
