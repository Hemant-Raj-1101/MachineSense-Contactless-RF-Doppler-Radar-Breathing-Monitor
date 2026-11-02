#ifndef COMMS_UI_H
#define COMMS_UI_H

#include <Arduino.h>
#include <WebSocketsServer.h>
#include "globals.h"

// -----------------------------------------------------------------------------
// Wi-Fi configuration
// -----------------------------------------------------------------------------
// Credentials are NOT stored in the repository. Create include/secrets.h
// (copy include/secrets.h.example); it is git-ignored. Alternatively define
// WIFI_SSID / WIFI_PASSWORD via build_flags in platformio.ini.
//
// With an empty SSID the device skips the router and starts its own fallback
// access point ("MachineSense-XXXX") so the dashboard is always reachable.
#if __has_include("secrets.h")
#include "secrets.h"
#endif

#ifndef WIFI_SSID
#define WIFI_SSID     ""
#endif
#ifndef WIFI_PASSWORD
#define WIFI_PASSWORD ""
#endif

// Password of the fallback access point (minimum 8 characters, WPA2).
// Override in secrets.h before deploying anywhere other than a bench.
#ifndef AP_PASSWORD
#define AP_PASSWORD   "machine123"
#endif

// --- Dashboard Telemetry Structure ---
// Sent from the Detection Task to CommsUI on every FFT cycle.
// Includes raw vital data + hybrid detection engine scores.
struct DashboardTelemetry {
    float breathingFreq;       // Breathing peak frequency (Hz)
    float heartbeatFreq;       // Heartbeat peak frequency (Hz)
    float breathingMag;        // Breathing FFT magnitude
    float heartbeatMag;        // Heartbeat FFT magnitude
    int   confidenceLevel;     // Current detection confidence (0 to maxConfidence)
    int   maxConfidence;       // Maximum confidence scale
    bool  alertTriggered;      // True if this cycle caused a confirmed detection
    SystemState state;         // Current system state (CALIBRATING or ACTIVE)
    float scoreA;              // Method A: spectral comparison score (0–1)
    float scoreB;              // Method B: EMA adaptive floor score (0–1)
    float scoreC;              // Method C: differential score (0–1)
    float fusedScore;          // Weighted fusion of A+B+C (0–1)
    bool humanDetected;        // Confirmed detector state (not confidence)
};

// --- FreeRTOS Queue for Dashboard Telemetry ---
// Declared here, defined in main.cpp alongside the other RTOS handles.
extern QueueHandle_t dashboardQueue;

// WebSocket server instance (shared between init and task)
extern WebSocketsServer webSocket;

// Call this from setup() BEFORE creating the CommsUI task.
// It connects to WiFi and starts the WebSocket server on port 81.
void initCommsWiFi();

// FreeRTOS task: handles local alarms (buzzer/LED) and
// non-blocking WebSocket broadcasts + event loop servicing.
void vCommsUITask(void *pvParameters);

#endif // COMMS_UI_H
