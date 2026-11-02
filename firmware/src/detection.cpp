// =============================================================================
// MachineSense - Vital Sign Detection Engine
// =============================================================================
//
// Three-method detection fusion:
//
//   Method A — Frequency-domain spectral comparison
//              Weight: 0.40
//
//   Method B — EMA adaptive noise-floor detection
//              Weight: 0.35
//
//   Method C — Differential detection between FFT windows
//              Weight: 0.25
//
// Detection flow:
//
//   Phase 1:
//       Calibration + baseline capture
//
//   Phase 2:
//       A + B + C active
//       -> weighted fusion
//       -> confidence accumulation
//       -> human detection
//
// =============================================================================
//
// MachineSense v2.1 improvements:
//   - HAWK branding completely replaced with MachineSense
//   - Safer FFT-bin bounds handling
//   - Explicit <cstring> include for memset()
//   - Centralized debug logging control
//   - Cleaner helper functions
//   - More descriptive comments
//   - Preserves original detection algorithm and tuning values
//
// =============================================================================

#include "detection.h"
#include "globals.h"
#include "comms_ui.h"

#include <math.h>
#include <cstring>

// =============================================================================
// DEBUG CONFIGURATION
// =============================================================================

// Set to 1 when tuning/debugging the detection algorithm.
// Set to 0 for cleaner Serial output during normal operation.
#define MACHINESENSE_DEBUG 1

// =============================================================================
// DETECTION TUNING CONSTANTS
// =============================================================================

// -----------------------------------------------------------------------------
// Method B EMA smoothing
// -----------------------------------------------------------------------------
//
// 0.01 = approximately 100-cycle adaptation window.
//
// A lower value makes the adaptive floor more stable.
// A higher value makes it respond faster to environmental changes.
//
static constexpr float EMA_ALPHA = 0.01f;


// -----------------------------------------------------------------------------
// Method C differential reference smoothing
// -----------------------------------------------------------------------------
//
// Faster than Method B because Method C is intended to detect recent changes.
//
static constexpr float DIFF_ALPHA = 0.08f;


// =============================================================================
// FUSION WEIGHTS
// =============================================================================
//
// These must add up to 1.0.
//
static constexpr float WEIGHT_A = 0.40f;
static constexpr float WEIGHT_B = 0.35f;
static constexpr float WEIGHT_C = 0.25f;


// =============================================================================
// DETECTION THRESHOLDS
// =============================================================================

// Minimum fused score required to generate a confidence hit.
static constexpr float FUSION_THRESHOLD = 0.20f;


// -----------------------------------------------------------------------------
// Signal ratio thresholds
// -----------------------------------------------------------------------------
//
// A ratio of 1.15 means the signal is approximately 15% above its floor.
//
static constexpr float SIGNAL_RATIO_MIN = 1.15f;
static constexpr float SIGNAL_RATIO_MAX = 3.00f;


// -----------------------------------------------------------------------------
// Differential detection thresholds
// -----------------------------------------------------------------------------
//
// 0.10 means a 10% change from the reference begins scoring.
//
static constexpr float DIFF_RATIO_MIN = 0.10f;
static constexpr float DIFF_RATIO_MAX = 1.00f;


// =============================================================================
// CALIBRATION
// =============================================================================
//
// Original calibration duration:
// 3 FFT cycles.
//
// With approximately 4.1 seconds per FFT cycle this is around 12 seconds.
//
static constexpr int CALIB_CYCLES = 3;


// First FFT bin to process.
//
// Bin 0 is DC and is intentionally ignored.
//
static constexpr int FIRST_BIN = 1;


// =============================================================================
// CONFIDENCE SYSTEM
// =============================================================================

static constexpr int CONFIDENCE_MAX        = 5;
static constexpr int CONFIDENCE_DETECT     = 5;
static constexpr int CONFIDENCE_REACQUIRE  = 3;


// =============================================================================
// MODULE STATE
// =============================================================================

// -----------------------------------------------------------------------------
// Method A
// Phase-1 frequency-domain baseline
// -----------------------------------------------------------------------------

static float noise_floor_A[FFT_VITAL_BINS] = {0.0f};

static float calib_accumulator[FFT_VITAL_BINS] = {0.0f};

static int calib_cycles_done = 0;

static bool calibration_complete = false;


// -----------------------------------------------------------------------------
// Method B
// Adaptive EMA noise floor
// -----------------------------------------------------------------------------

static float ema_floor_B[FFT_VITAL_BINS] = {0.0f};

static bool ema_seeded = false;


// -----------------------------------------------------------------------------
// Method C
// Previous FFT window reference
// -----------------------------------------------------------------------------

static float prev_window_C[FFT_VITAL_BINS] = {0.0f};

static bool diff_initialized = false;


// -----------------------------------------------------------------------------
// Global detection state
// -----------------------------------------------------------------------------

static int confidence = 0;

static bool detected = false;


// -----------------------------------------------------------------------------
// Debug cycle counter
// -----------------------------------------------------------------------------

static uint32_t cycle_count = 0;


// =============================================================================
// HELPER FUNCTIONS
// =============================================================================

// -----------------------------------------------------------------------------
// Clamp a floating-point value between lower and upper bounds.
// -----------------------------------------------------------------------------

static inline float clampf(float value, float low, float high)
{
    if (value < low)
        return low;

    if (value > high)
        return high;

    return value;
}


// -----------------------------------------------------------------------------
// Convert a ratio/change into a normalized 0.0 - 1.0 score.
// -----------------------------------------------------------------------------

static inline float ratio_to_score(
    float ratio,
    float minimum,
    float maximum)
{
    if (ratio <= minimum)
        return 0.0f;

    if (ratio >= maximum)
        return 1.0f;

    return (ratio - minimum) / (maximum - minimum);
}


// -----------------------------------------------------------------------------
// Safely determine the number of FFT bins that can be processed.
//
// Prevents accidental access beyond the fixed-size MachineSense arrays.
// -----------------------------------------------------------------------------

static inline int get_safe_bin_count(int num_bins)
{
    if (num_bins < FIRST_BIN)
        return FIRST_BIN;

    if (num_bins > FFT_VITAL_BINS)
        return FFT_VITAL_BINS;

    return num_bins;
}


// -----------------------------------------------------------------------------
// Calculate coverage factor.
//
// For Doppler vital-sign signals, only a small number of FFT bins may
// contain meaningful energy. Therefore, the coverage penalty is deliberately
// gentle.
//
static inline float calculate_coverage(
    int active_bins,
    int usable_bins)
{
    if (active_bins <= 0 || usable_bins <= 0)
        return 0.0f;

    float minimum_bins =
        fmaxf((float)usable_bins / 12.0f, 1.0f);

    return clampf(
        (float)active_bins / minimum_bins,
        0.0f,
        1.0f
    );
}


// =============================================================================
// PHASE 1
// BASELINE ACCUMULATION
// =============================================================================

static void accumulate_baseline(
    const float* magnitudes,
    int num_bins)
{
    num_bins = get_safe_bin_count(num_bins);

    for (int i = FIRST_BIN; i < num_bins; i++)
    {
        calib_accumulator[i] += magnitudes[i];
    }

    calib_cycles_done++;

    // -------------------------------------------------------------------------
    // Calibration complete
    // -------------------------------------------------------------------------

    if (calib_cycles_done >= CALIB_CYCLES)
    {
        for (int i = FIRST_BIN; i < num_bins; i++)
        {
            // Calculate average baseline.
            noise_floor_A[i] =
                calib_accumulator[i] /
                (float)calib_cycles_done;

            // Prevent division by extremely small values.
            if (noise_floor_A[i] < 1.0f)
                noise_floor_A[i] = 1.0f;


            // -----------------------------------------------------------------
            // Seed Method B
            //
            // This prevents the EMA from starting at the first live sample.
            // -----------------------------------------------------------------

            ema_floor_B[i] = noise_floor_A[i];


            // -----------------------------------------------------------------
            // Seed Method C
            // -----------------------------------------------------------------

            prev_window_C[i] = noise_floor_A[i];
        }

        ema_seeded = true;

        diff_initialized = true;

        calibration_complete = true;


#if MACHINESENSE_DEBUG

        Serial.println();
        Serial.println("==========================================");
        Serial.println("MachineSense Calibration Complete");
        Serial.println("Baseline captured successfully");
        Serial.println("==========================================");

        Serial.println("[MachineSense] Baseline per FFT bin:");

        for (int i = FIRST_BIN; i < num_bins; i++)
        {
            Serial.print("  Bin ");
            Serial.print(i);
            Serial.print(": ");
            Serial.println((int)noise_floor_A[i]);
        }

        Serial.println("==========================================");
        Serial.println();

#endif
    }
}


// =============================================================================
// METHOD A
// FREQUENCY-DOMAIN BASELINE COMPARISON
// =============================================================================

static float score_method_A(
    const float* magnitudes,
    int num_bins)
{
    if (!calibration_complete)
        return 0.0f;

    num_bins = get_safe_bin_count(num_bins);

    float total_score = 0.0f;

    int active_bins = 0;

    int usable_bins = num_bins - FIRST_BIN;


    for (int i = FIRST_BIN; i < num_bins; i++)
    {
        float floor_value =
            noise_floor_A[i] > 1.0f
            ? noise_floor_A[i]
            : 1.0f;


        float ratio =
            magnitudes[i] / floor_value;


        float bin_score =
            ratio_to_score(
                ratio,
                SIGNAL_RATIO_MIN,
                SIGNAL_RATIO_MAX
            );


        if (bin_score > 0.0f)
        {
            total_score += bin_score;

            active_bins++;
        }
    }


    if (active_bins == 0)
        return 0.0f;


    float average_score =
        total_score / (float)active_bins;


    float coverage =
        calculate_coverage(
            active_bins,
            usable_bins
        );


    return average_score * coverage;
}


// =============================================================================
// METHOD B
// EMA ADAPTIVE NOISE FLOOR
// =============================================================================

static float score_method_B(
    const float* magnitudes,
    int num_bins)
{
    num_bins = get_safe_bin_count(num_bins);

    float total_score = 0.0f;

    int active_bins = 0;

    int usable_bins = num_bins - FIRST_BIN;


    for (int i = FIRST_BIN; i < num_bins; i++)
    {
        // ---------------------------------------------------------------------
        // Initialize EMA if necessary.
        // ---------------------------------------------------------------------

        if (!ema_seeded)
        {
            ema_floor_B[i] =
                magnitudes[i] > 1.0f
                ? magnitudes[i]
                : 1.0f;
        }
        else
        {
            // -----------------------------------------------------------------
            // Normal EMA update.
            // -----------------------------------------------------------------

            ema_floor_B[i] =
                EMA_ALPHA * magnitudes[i]
                +
                (1.0f - EMA_ALPHA) * ema_floor_B[i];
        }


        float floor_value =
            ema_floor_B[i] > 1.0f
            ? ema_floor_B[i]
            : 1.0f;


        float ratio =
            magnitudes[i] / floor_value;


        float bin_score =
            ratio_to_score(
                ratio,
                SIGNAL_RATIO_MIN,
                SIGNAL_RATIO_MAX
            );


        if (bin_score > 0.0f)
        {
            total_score += bin_score;

            active_bins++;
        }
    }


    if (!ema_seeded)
        ema_seeded = true;


    if (active_bins == 0)
        return 0.0f;


    float average_score =
        total_score / (float)active_bins;


    float coverage =
        calculate_coverage(
            active_bins,
            usable_bins
        );


    return average_score * coverage;
}


// =============================================================================
// METHOD C
// DIFFERENTIAL FFT DETECTION
// =============================================================================
//
// Detects changes between consecutive FFT windows.
//
// This is useful because a human target can introduce small changes in
// microwave Doppler energy even when the absolute signal level is low.
//
static float score_method_C(
    const float* magnitudes,
    int num_bins)
{
    num_bins = get_safe_bin_count(num_bins);

    float total_score = 0.0f;

    int active_bins = 0;

    int usable_bins = num_bins - FIRST_BIN;


    for (int i = FIRST_BIN; i < num_bins; i++)
    {
        // ---------------------------------------------------------------------
        // Initialize reference if required.
        // ---------------------------------------------------------------------

        if (!diff_initialized)
        {
            prev_window_C[i] =
                magnitudes[i] > 1.0f
                ? magnitudes[i]
                : 1.0f;
        }


        float reference =
            prev_window_C[i] > 1.0f
            ? prev_window_C[i]
            : 1.0f;


        // ---------------------------------------------------------------------
        // Calculate relative change.
        // ---------------------------------------------------------------------

        float delta_ratio =
            fabsf(
                magnitudes[i] - reference
            ) / reference;


        float bin_score =
            ratio_to_score(
                delta_ratio,
                DIFF_RATIO_MIN,
                DIFF_RATIO_MAX
            );


        if (bin_score > 0.0f)
        {
            total_score += bin_score;

            active_bins++;
        }


        // ---------------------------------------------------------------------
        // Update reference using faster EMA.
        // ---------------------------------------------------------------------

        prev_window_C[i] =
            DIFF_ALPHA * magnitudes[i]
            +
            (1.0f - DIFF_ALPHA) * prev_window_C[i];
    }


    diff_initialized = true;


    if (active_bins == 0)
        return 0.0f;


    float average_score =
        total_score / (float)active_bins;


    float coverage =
        calculate_coverage(
            active_bins,
            usable_bins
        );


    return average_score * coverage;
}


// =============================================================================
// DEBUG OUTPUT
// =============================================================================

static void print_detection_debug(
    float score_A,
    float score_B,
    float score_C,
    float fused_score,
    int current_confidence,
    const float* magnitudes,
    int num_bins)
{
#if MACHINESENSE_DEBUG

    Serial.print("[MachineSense] #");
    Serial.print(cycle_count);

    Serial.print("  A=");
    Serial.print((int)(score_A * 100));

    Serial.print("%  B=");
    Serial.print((int)(score_B * 100));

    Serial.print("%  C=");
    Serial.print((int)(score_C * 100));

    Serial.print("%  F=");
    Serial.print((int)(fused_score * 100));

    Serial.print("%  CONF=");
    Serial.print(current_confidence);

    Serial.print("/");
    Serial.print(CONFIDENCE_MAX);


    // -------------------------------------------------------------------------
    // Print first few FFT bins.
    // -------------------------------------------------------------------------

    Serial.print("  BINS=[");

    int last_bin =
        num_bins < 6
        ? num_bins
        : 6;


    for (int i = FIRST_BIN; i < last_bin; i++)
    {
        Serial.print((int)magnitudes[i]);

        if (i < last_bin - 1)
            Serial.print(",");
    }

    Serial.println("]");

#endif
}


// =============================================================================
// PUBLIC API
// =============================================================================
//
// Called once for every processed FFT cycle.
//
DetectionResult run_detection(
    const float* magnitudes,
    int num_bins)
{
    DetectionResult result;


    // -------------------------------------------------------------------------
    // Initialize result structure.
    // -------------------------------------------------------------------------

    result.detected       = false;
    result.confidence     = confidence;
    result.score_A        = 0.0f;
    result.score_B        = 0.0f;
    result.score_C        = 0.0f;
    result.fused_score    = 0.0f;
    result.calibrating    = false;
    result.calib_progress = 0.0f;


    // -------------------------------------------------------------------------
    // Basic input protection.
    // -------------------------------------------------------------------------

    if (magnitudes == nullptr || num_bins <= FIRST_BIN)
    {
        return result;
    }


    num_bins = get_safe_bin_count(num_bins);


    cycle_count++;


    // =========================================================================
    // PHASE 1 — CALIBRATION
    // =========================================================================

    if (!calibration_complete)
    {
        accumulate_baseline(
            magnitudes,
            num_bins
        );


        // Warm up Method B during calibration.
        score_method_B(
            magnitudes,
            num_bins
        );


        // Warm up Method C during calibration.
        score_method_C(
            magnitudes,
            num_bins
        );


        result.calibrating = true;


        result.calib_progress =
            clampf(
                (float)calib_cycles_done /
                (float)CALIB_CYCLES,
                0.0f,
                1.0f
            );


        result.confidence = confidence;


        return result;
    }


    // =========================================================================
    // PHASE 2 — THREE-METHOD DETECTION
    // =========================================================================

    float score_A =
        score_method_A(
            magnitudes,
            num_bins
        );


    float score_B =
        score_method_B(
            magnitudes,
            num_bins
        );


    float score_C =
        score_method_C(
            magnitudes,
            num_bins
        );


    // -------------------------------------------------------------------------
    // Weighted fusion
    // -------------------------------------------------------------------------

    float fused_score =
        (WEIGHT_A * score_A)
        +
        (WEIGHT_B * score_B)
        +
        (WEIGHT_C * score_C);


    fused_score =
        clampf(
            fused_score,
            0.0f,
            1.0f
        );


    // -------------------------------------------------------------------------
    // Debug information
    // -------------------------------------------------------------------------

    print_detection_debug(
        score_A,
        score_B,
        score_C,
        fused_score,
        confidence,
        magnitudes,
        num_bins
    );


    // =========================================================================
    // CONFIDENCE UPDATE
    // =========================================================================

    if (fused_score >= FUSION_THRESHOLD)
    {
        // Strong enough signal -> increase confidence.
        if (confidence < CONFIDENCE_MAX)
        {
            confidence++;
        }
    }
    else
    {
        // Weak signal -> decrease confidence.
        if (confidence > 0)
        {
            confidence--;
        }
    }


    // =========================================================================
    // DETECTION EVENT
    // =========================================================================

    bool newly_detected =
        (confidence >= CONFIDENCE_DETECT)
        &&
        !detected;


    if (newly_detected)
    {
        detected = true;


#if MACHINESENSE_DEBUG
        Serial.println();
        Serial.println("************************************************");
        Serial.println("   MachineSense HUMAN DETECTION CONFIRMED");
        Serial.println("************************************************");
        Serial.println();
#endif
    }


    // =========================================================================
    // SIGNAL LOST / RE-ACQUISITION
    // =========================================================================

    if (confidence == 0 && detected)
    {
        detected = false;

        // Start from reacquisition confidence.
        confidence = CONFIDENCE_REACQUIRE;


#if MACHINESENSE_DEBUG
        Serial.println(
            "[MachineSense] Signal lost - re-acquisition mode."
        );
#endif
    }


    // =========================================================================
    // BUILD RESULT
    // =========================================================================

    result.detected =
        detected;


    result.confidence =
        confidence;


    result.score_A =
        score_A;


    result.score_B =
        score_B;


    result.score_C =
        score_C;


    result.fused_score =
        fused_score;


    result.calibrating =
        false;


    result.calib_progress =
        1.0f;


    return result;
}


// =============================================================================
// HARD RESET
// =============================================================================
//
// Completely resets the MachineSense detection engine.
//
// Useful when:
//   - The environment changes significantly.
//   - Radar position changes.
//   - A new person enters/leaves.
//   - You want to perform a fresh calibration.
//
void reset_detection()
{
    // -------------------------------------------------------------------------
    // Clear all baseline/reference arrays.
    // -------------------------------------------------------------------------

    memset(
        noise_floor_A,
        0,
        sizeof(noise_floor_A)
    );


    memset(
        calib_accumulator,
        0,
        sizeof(calib_accumulator)
    );


    memset(
        ema_floor_B,
        0,
        sizeof(ema_floor_B)
    );


    memset(
        prev_window_C,
        0,
        sizeof(prev_window_C)
    );


    // -------------------------------------------------------------------------
    // Reset state.
    // -------------------------------------------------------------------------

    calib_cycles_done = 0;

    calibration_complete = false;

    ema_seeded = false;

    diff_initialized = false;

    confidence = 0;

    detected = false;

    cycle_count = 0;


#if MACHINESENSE_DEBUG

    Serial.println();
    Serial.println("------------------------------------------");
    Serial.println("MachineSense Detection Engine Reset");
    Serial.println("New calibration will begin.");
    Serial.println("------------------------------------------");
    Serial.println();

#endif
}


// =============================================================================
// FREERTOS DETECTION TASK
// =============================================================================

void vDetectionTask(void *pvParameters)
{
    VitalSignData vitals;

    bool lastAlertState = false;


    // -------------------------------------------------------------------------
    // Main task loop
    // -------------------------------------------------------------------------

    for (;;)
    {
        // Wait until processed FFT/vital-sign data is available.
        if (xQueueReceive(
                processedDataQueue,
                &vitals,
                portMAX_DELAY
            ) == pdPASS)
        {
            // =================================================================
            // RUN DETECTION ENGINE
            // =================================================================

            DetectionResult detection =
                run_detection(
                    vitals.magnitudes,
                    FFT_VITAL_BINS
                );


            // =================================================================
            // UPDATE SYSTEM STATE
            // =================================================================

            systemState =
                detection.calibrating
                ? STATE_CALIBRATING
                : STATE_ACTIVE;


            // =================================================================
            // ALERT RISING EDGE
            // =================================================================
            //
            // The alert is generated only when detection changes:
            //
            //     NOT DETECTED -> DETECTED
            //
            // This prevents continuous alert spam while a person remains
            // detected.
            //
            // =================================================================

            bool alertFired = false;


            if (detection.detected && !lastAlertState)
            {
                alertFired = true;


                // Notify the alert/alarm task.
                xSemaphoreGive(
                    detectionSemaphore
                );


#if MACHINESENSE_DEBUG

                Serial.println(
                    "[MachineSense] Alert event triggered."
                );

#endif
            }


            lastAlertState =
                detection.detected;


            // =================================================================
            // BUILD DASHBOARD TELEMETRY
            // =================================================================

            DashboardTelemetry telemetry;


            telemetry.breathingFreq =
                vitals.breathingFreq;


            telemetry.heartbeatFreq =
                vitals.heartbeatFreq;


            telemetry.breathingMag =
                vitals.breathingMag;


            telemetry.heartbeatMag =
                vitals.heartbeatMag;


            telemetry.confidenceLevel =
                detection.confidence;


            telemetry.maxConfidence =
                CONFIDENCE_MAX;


            telemetry.alertTriggered =
                alertFired;


            telemetry.state =
                detection.calibrating
                ? STATE_CALIBRATING
                : STATE_ACTIVE;


            telemetry.scoreA =
                detection.score_A;


            telemetry.scoreB =
                detection.score_B;


            telemetry.scoreC =
                detection.score_C;


            telemetry.fusedScore =
                detection.fused_score;

            telemetry.humanDetected =
                detection.detected;


            // =================================================================
            // SEND TELEMETRY TO DASHBOARD TASK
            // =================================================================

            xQueueSend(
                dashboardQueue,
                &telemetry,
                0
            );


        }
    }
}


// =============================================================================
// END OF MACHINE SENSE DETECTION ENGINE
// =============================================================================
