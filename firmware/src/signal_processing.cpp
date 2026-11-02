#include "signal_processing.h"
#include "globals.h"
#include "arduinoFFT.h"
#include <string.h>

// =============================================================================
// MachineSense FFT Configuration for 1024-Point Transform
// =============================================================================
// 1024 samples at 250 Hz = 4.096 seconds per FFT window
// Frequency resolution: 250 / 1024 = 0.2441 Hz per bin
// Nyquist limit: 125 Hz (well above our 3 Hz target)
//
// IMPORTANT: Using float (not double) because the ESP32's FPU is
// single-precision only. Double-precision is software-emulated and ~10x slower.
// For a 1024-point FFT the speedup is critical (~30ms vs ~300ms compute time).
// =============================================================================
#define SAMPLES 1024
#define SAMPLING_FREQUENCY 250
#define FREQ_RESOLUTION ((float)SAMPLING_FREQUENCY / SAMPLES)  // 0.2441 Hz

// --- MachineSense Sub-Band Bin Ranges ---

// Breathing band: 0.2 - 0.6 Hz
// Bin 1 = 0.244 Hz, Bin 2 = 0.488 Hz
// Covers the core breathing-frequency range.
#define BREATHING_BIN_START 1
#define BREATHING_BIN_END   2

// Heartbeat band: 1.0 - 2.5 Hz
// Bin 4 = 0.977 Hz, Bin 10 = 2.441 Hz
#define HEARTBEAT_BIN_START 4
#define HEARTBEAT_BIN_END   10

// =============================================================================
// findSubBandPeak - Locate the dominant frequency within a specific bin range
// =============================================================================
// Uses parabolic interpolation on the 3 bins surrounding the peak for
// sub-bin frequency accuracy.
//
// Parameters:
//   magnitudes   - FFT magnitude array (post complexToMagnitude)
//   startBin     - First bin index to search (inclusive)
//   endBin       - Last bin index to search (inclusive)
//   totalBins    - Total number of bins (SAMPLES/2) for boundary checking
//   outMagnitude - Output: magnitude of the detected peak
//   freqRes      - Frequency resolution (Hz per bin)
//
// Returns:
//   Interpolated frequency in Hz of the strongest peak in the range.
// =============================================================================
float findSubBandPeak(float* magnitudes, int startBin, int endBin,
                      int totalBins, float* outMagnitude, float freqRes,
                      float minFrequency, float maxFrequency) {

    // 1. Find the bin with maximum magnitude in the target range.
    //    CRITICAL: Always start from at least bin 1 to skip the DC component.
    //    DC leakage from the ADC offset can dominate bin 0 and corrupt
    //    parabolic interpolation, producing incorrect frequencies.
    int safeStart = (startBin < 1) ? 1 : startBin;

    float maxMag = 0.0f;
    int peakBin = safeStart;

    for (int i = safeStart; i <= endBin; i++) {
        if (magnitudes[i] > maxMag) {
            maxMag = magnitudes[i];
            peakBin = i;
        }
    }

    *outMagnitude = maxMag;

    // A silent input has no meaningful peak. Do not report the first bin as
    // a valid breathing or heartbeat rate when the sensor has no signal.
    if (maxMag <= 0.0f) {
        return 0.0f;
    }

    // 2. Parabolic interpolation for sub-bin frequency accuracy.
    //    Uses bins on either side of the detected peak.
    //
    //    Guard: peakBin must be > 0 AND < totalBins-1 so that
    //    both neighboring bins are valid.
    float interpolatedBin = (float)peakBin;

    if (peakBin > 0 && peakBin < totalBins - 1) {

        float alpha = magnitudes[peakBin - 1];
        float beta  = magnitudes[peakBin];
        float gamma = magnitudes[peakBin + 1];

        float denom = alpha - 2.0f * beta + gamma;

        if (denom != 0.0f) {
            interpolatedBin =
                peakBin + 0.5f * (alpha - gamma) / denom;
        }
    }

    // Clamp to the physical frequency band, not the integer bin centers.
    // This allows interpolation to report values between adjacent bins and
    // across the lower/upper edge bins of the requested band.
    const float interpolatedFrequency = interpolatedBin * freqRes;
    if (interpolatedFrequency < minFrequency) {
        return minFrequency;
    }
    if (interpolatedFrequency > maxFrequency) {
        return maxFrequency;
    }
    return interpolatedFrequency;
}

// =============================================================================
// MachineSense Signal Processing Task
// Priority: 3
// =============================================================================
// Accumulates 1024 raw ADC samples (~4.1 seconds), then performs:
//
//   ADC Samples
//        ↓
//   DC Removal
//        ↓
//   Hamming Window
//        ↓
//   FFT
//        ↓
//   Magnitude Spectrum
//        ↓
//   Breathing / Heartbeat Sub-Band Detection
//        ↓
//   VitalSignData
//
// The processed result is sent to the MachineSense Detection Task.
// =============================================================================
void vSignalProcessingTask(void *pvParameters) {

    // --- All FFT state is local to this task ---
    //
    // 'static' places these arrays in BSS instead of the task stack,
    // helping prevent FreeRTOS stack overflow.
    //
    // Memory:
    // 2 × 1024 × 4 bytes = 8,192 bytes
    static float vReal[SAMPLES];
    static float vImag[SAMPLES];

    int sampleCounter = 0;
    uint16_t rawSample;

    // Initialize the MachineSense FFT processor.
    ArduinoFFT<float> FFT = ArduinoFFT<float>();

    // Serial output removed.
    // MachineSense telemetry is transmitted through WebSocket.

    for (;;) {

        // 1. Collect raw radar samples from the MachineSense
        //    Radar Acquisition Task through the queue.
        if (xQueueReceive(rawDataQueue, &rawSample, portMAX_DELAY) == pdPASS) {

            vReal[sampleCounter] = (float)rawSample;
            vImag[sampleCounter] = 0.0f;

            sampleCounter++;

            // 2. Once 1024 samples are collected,
            //    execute the complete FFT processing pipeline.
            if (sampleCounter == SAMPLES) {

                // =========================================================
                // MachineSense FFT Processing Pipeline
                // =========================================================

                // Remove the DC component from the radar signal.
                FFT.dcRemoval(vReal, SAMPLES);

                // Apply Hamming window to reduce spectral leakage.
                FFT.windowing(
                    vReal,
                    SAMPLES,
                    FFT_WIN_TYP_HAMMING,
                    FFT_FORWARD
                );

                // Perform the forward FFT.
                FFT.compute(
                    vReal,
                    vImag,
                    SAMPLES,
                    FFT_FORWARD
                );

                // Convert complex FFT output into magnitude spectrum.
                FFT.complexToMagnitude(
                    vReal,
                    vImag,
                    SAMPLES
                );

                // Keep bin 0 intact: the breathing peak at bin 1 uses it as
                // the left neighbor for parabolic frequency interpolation.

                // =========================================================
                // MachineSense Vital Sign Extraction
                // =========================================================

                VitalSignData vitals;

                // Clear all fields, including magnitudes[].
                memset(&vitals, 0, sizeof(vitals));

                // Only the first half of the FFT spectrum is meaningful
                // for a real-valued input signal.
                int halfBins = SAMPLES / 2;

                // ---------------------------------------------------------
                // Breathing Frequency Detection
                // ---------------------------------------------------------
                vitals.breathingFreq = findSubBandPeak(
                    vReal,
                    BREATHING_BIN_START,
                    BREATHING_BIN_END,
                    halfBins,
                    &vitals.breathingMag,
                    FREQ_RESOLUTION,
                    0.2f,
                    0.6f
                );

                // ---------------------------------------------------------
                // Heartbeat Frequency Detection
                // ---------------------------------------------------------
                vitals.heartbeatFreq = findSubBandPeak(
                    vReal,
                    HEARTBEAT_BIN_START,
                    HEARTBEAT_BIN_END,
                    halfBins,
                    &vitals.heartbeatMag,
                    FREQ_RESOLUTION,
                    1.0f,
                    2.5f
                );

                // =========================================================
                // Copy Vital-Sign FFT Bins
                // =========================================================
                //
                // The MachineSense Detection Engine uses these individual
                // FFT bins for additional spectral analysis.

                for (int b = 0;
                     b < FFT_VITAL_BINS && b < halfBins;
                     b++) {

                    vitals.magnitudes[b] = vReal[b];
                }

                // =========================================================
                // Send Processed Data to Detection Task
                // =========================================================
                //
                // Non-blocking queue operation prevents the FFT task
                // from being delayed if the Detection Task is busy.

                xQueueSend(
                    processedDataQueue,
                    &vitals,
                    0
                );

                // Reset the sample counter for the next FFT window.
                sampleCounter = 0;
            }
        }
    }
}
