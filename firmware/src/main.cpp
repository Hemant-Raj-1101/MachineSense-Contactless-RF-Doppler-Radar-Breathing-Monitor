#include "globals.h"
#include "radar_sensor.h"
#include "signal_processing.h"
#include "detection.h"
#include "comms_ui.h"

// =============================================================================
// MachineSense - Global RTOS Handles
// =============================================================================

// Define the RTOS handles
QueueHandle_t rawDataQueue;
QueueHandle_t processedDataQueue;
QueueHandle_t dashboardQueue;
SemaphoreHandle_t detectionSemaphore;


// =============================================================================
// MachineSense - Global System State
// =============================================================================
//
// Declared as extern in globals.h
//
volatile SystemState systemState = STATE_CALIBRATING;


// =============================================================================
// SETUP
// =============================================================================

void setup()
{
    // -------------------------------------------------------------------------
    // Initialize Serial
    // -------------------------------------------------------------------------

    Serial.begin(115200);

    delay(1000);  // Wait for Serial port to stabilize


    // -------------------------------------------------------------------------
    // MachineSense Boot Information
    // -------------------------------------------------------------------------

    Serial.println();
    Serial.println("==============================================");
    Serial.println("       MachineSense System Boot");
    Serial.println("==============================================");

    Serial.print("VitalSignData size: ");
    Serial.println(sizeof(VitalSignData));

    Serial.print("DashboardTelemetry size: ");
    Serial.println(sizeof(DashboardTelemetry));

    Serial.print("Free heap before queues: ");
    Serial.println(ESP.getFreeHeap());


    // =========================================================================
    // 1. CREATE FREERTOS QUEUES AND SEMAPHORES
    // =========================================================================

    // -------------------------------------------------------------------------
    // Raw radar samples
    //
    // Buffered for 1024-point FFT accumulation.
    // -------------------------------------------------------------------------

    rawDataQueue =
        xQueueCreate(
            2048,
            sizeof(uint16_t)
        );


    // -------------------------------------------------------------------------
    // Processed vital-sign data
    //
    // Holds dual-band vital-sign information.
    // -------------------------------------------------------------------------

    processedDataQueue =
        xQueueCreate(
            4,
            sizeof(VitalSignData)
        );


    // -------------------------------------------------------------------------
    // Dashboard telemetry
    //
    // Detection task -> Communications/UI task
    // -------------------------------------------------------------------------

    dashboardQueue =
        xQueueCreate(
            4,
            sizeof(DashboardTelemetry)
        );


    // -------------------------------------------------------------------------
    // Detection semaphore
    //
    // Triggered when MachineSense confirms a human detection.
    // -------------------------------------------------------------------------

    detectionSemaphore =
        xSemaphoreCreateBinary();


    // -------------------------------------------------------------------------
    // Memory information
    // -------------------------------------------------------------------------

    Serial.print("Free heap after queues: ");
    Serial.println(ESP.getFreeHeap());


    // =========================================================================
    // 2. VERIFY RTOS OBJECT CREATION
    // =========================================================================

    if (
        rawDataQueue == NULL ||
        processedDataQueue == NULL ||
        dashboardQueue == NULL ||
        detectionSemaphore == NULL
    )
    {
        Serial.println();
        Serial.println("==============================================");
        Serial.println("ERROR: MachineSense RTOS object creation failed!");
        Serial.println("System halted.");
        Serial.println("==============================================");


        // Halt system if RTOS objects could not be created.
        while (1)
        {
            delay(1000);
        }
    }


    Serial.println();
    Serial.println("MachineSense RTOS Queues: OK");
    Serial.println("MachineSense Detection Semaphore: OK");


    // =========================================================================
    // 3. INITIALIZE COMMUNICATIONS / WIFI
    // =========================================================================

    Serial.println();
    Serial.println("Initializing MachineSense communications...");


    initCommsWiFi();


    Serial.println("MachineSense communications initialized.");


    // =========================================================================
    // 4. CREATE FREERTOS TASKS
    // =========================================================================
    //
    // FreeRTOS priority:
    //
    //     Higher number = higher priority
    //
    // MachineSense task architecture:
    //
    //     Priority 4 -> Radar Acquisition
    //     Priority 3 -> Signal Processing
    //     Priority 2 -> Detection
    //     Priority 1 -> Communications / UI
    //
    // =========================================================================


    // -------------------------------------------------------------------------
    // Radar acquisition task
    // -------------------------------------------------------------------------

    if (xTaskCreate(
        vRadarAcquisitionTask,
        "RadarTask",
        2048,
        NULL,
        4,
        NULL
    ) != pdPASS) {
        Serial.println("ERROR: Failed to create RadarTask; system halted.");
        while (1) delay(1000);
    }


    // -------------------------------------------------------------------------
    // Signal processing task
    // -------------------------------------------------------------------------

    if (xTaskCreate(
        vSignalProcessingTask,
        "ProcessTask",
        8192,
        NULL,
        3,
        NULL
    ) != pdPASS) {
        Serial.println("ERROR: Failed to create ProcessTask; system halted.");
        while (1) delay(1000);
    }


    // -------------------------------------------------------------------------
    // MachineSense detection task
    // -------------------------------------------------------------------------

    if (xTaskCreate(
        vDetectionTask,
        "DetectTask",
        6144,
        NULL,
        2,
        NULL
    ) != pdPASS) {
        Serial.println("ERROR: Failed to create DetectTask; system halted.");
        while (1) delay(1000);
    }


    // -------------------------------------------------------------------------
    // MachineSense communications and dashboard task
    // -------------------------------------------------------------------------

    if (xTaskCreate(
        vCommsUITask,
        "CommsTask",
        8192,
        NULL,
        1,
        NULL
    ) != pdPASS) {
        Serial.println("ERROR: Failed to create CommsTask; system halted.");
        while (1) delay(1000);
    }


    // =========================================================================
    // SYSTEM READY
    // =========================================================================

    Serial.println();
    Serial.println("==============================================");
    Serial.println("       MachineSense System Ready");
    Serial.println("==============================================");
    Serial.println("Radar Task       : Priority 4");
    Serial.println("Processing Task  : Priority 3");
    Serial.println("Detection Task   : Priority 2");
    Serial.println("Comms/UI Task    : Priority 1");
    Serial.println("==============================================");
    Serial.println();
}


// =============================================================================
// MAIN LOOP
// =============================================================================
//
// The application is completely FreeRTOS-task based.
//
// No processing is required inside Arduino's loopTask, so it deletes itself
// and releases its resources.
//
void loop()
{
    // All MachineSense work is performed by FreeRTOS tasks.
    vTaskDelete(NULL);
}
