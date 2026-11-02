#include "radar_sensor.h"
#include "globals.h"

// --- MachineSense Radar Sampling Parameters ---
// To satisfy the Nyquist theorem for human vital signals (max 3 Hz),
// a sampling rate of 250 Hz is excellent.
// 1000 ms / 250 Hz = 4 ms period.
#define SAMPLING_PERIOD_MS 4

void initRadar() {
    // Set ESP32 ADC to 12-bit resolution (values from 0 to 4095)
    // This high resolution is critical for detecting faint breathing ripples.
    analogReadResolution(12);

    // RADAR_ADC_PIN is defined in globals.h
    pinMode(RADAR_ADC_PIN, INPUT);

    // Serial output removed — ADC initialization is silent.
}

void vRadarAcquisitionTask(void *pvParameters) {
    // 1. Initialize the MachineSense radar ADC hardware
    initRadar();

    // 2. Setup deterministic FreeRTOS timing
    TickType_t xLastWakeTime;
    const TickType_t xFrequency = pdMS_TO_TICKS(SAMPLING_PERIOD_MS);

    // Get the initial tick count
    xLastWakeTime = xTaskGetTickCount();

    uint16_t adcValue = 0;

    // 3. MachineSense real-time radar acquisition loop
    for (;;) {
        // Read the analog signal coming from the IF amplifier circuit
        adcValue = analogRead(RADAR_ADC_PIN);

        // Safely push the data to the raw data queue
        if (rawDataQueue != NULL) {
            // The '0' timeout is crucial here.
            // If the queue is full because the processing task is lagging,
            // the sample is dropped instead of blocking acquisition.
            // This maintains the strict 250 Hz sampling rate.
            xQueueSend(rawDataQueue, &adcValue, 0);
        }

        // 4. Sleep until the next exact 4 ms sampling point.
        // vTaskDelayUntil() accounts for the time spent executing
        // analogRead() and other operations in the loop.
        vTaskDelayUntil(&xLastWakeTime, xFrequency);
    }
}
