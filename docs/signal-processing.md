# Signal processing and detection

Source of truth: `firmware/src/signal_processing.cpp` and `firmware/src/detection.cpp`.

## 1. Acquisition

| Parameter | Value |
|---|---|
| ADC | ESP32 ADC1, 12-bit (0–4095), GPIO 34 |
| Sample rate | 250 Hz (4 ms period, `vTaskDelayUntil`) |
| Window | 1024 samples = 4.096 s |
| Bin width | 250 / 1024 = 0.2441 Hz |

## 2. FFT pipeline (per window)

1. DC removal (`dcRemoval`)
2. Hamming window
3. 1024-point FFT in single-precision `float` (the ESP32 FPU is single-precision only)
4. Complex → magnitude; only the first 512 bins are meaningful for real input

### Band peaks

| Band | Bins searched | Frequency range | Reported clamp |
|---|---|---|---|
| Breathing | 1–2 (0.244, 0.488 Hz) | 0.2–0.6 Hz | 0.2–0.6 Hz |
| Heartbeat (experimental) | 4–10 (0.977–2.441 Hz) | 1.0–2.5 Hz | 1.0–2.5 Hz |

The strongest bin in the band is refined by parabolic interpolation over its two neighbours, then clamped to the band.
Bin 0 (DC) is never searched. A silent input returns 0 Hz rather than a spurious first-bin value.
Rates shown on the dashboard are `frequency × 60` (breaths or beats per minute).

The first 13 bins (0–12, up to ≈ 2.9 Hz) are passed on to the detector as `magnitudes[]`.

## 3. Detection engine

Detection ignores bin 0 and works on bins 1–12 (`FIRST_BIN = 1`, `FFT_VITAL_BINS = 13`).

### Phase 1 — calibration

The first `CALIB_CYCLES = 3` windows (≈ 12 s) are averaged per bin into the baseline `noise_floor_A` (floored at 1.0).
Methods B and C are seeded from the same baseline. `systemState` is `CALIBRATING` until this completes.

### Phase 2 — three scores

Every bin gets a score via `ratio_to_score(value, min, max)` = 0 below `min`, 1 above `max`, linear in between.
A method's score is the **mean of its non-zero bin scores × a coverage factor**, where coverage = `active_bins / max(usable_bins/12, 1)` clamped to [0, 1] (a deliberately gentle penalty, since only a few bins carry vital-sign energy).

| Method | Per-bin quantity | Scoring range | Reference |
|---|---|---|---|
| **A** spectral | `mag / baseline` | 1.15 → 3.00 | Fixed calibrated baseline |
| **B** adaptive floor | `mag / ema_floor` | 1.15 → 3.00 | EMA, α = 0.01 (~100 windows) |
| **C** differential | `abs(mag − ref) / ref` | 0.10 → 1.00 | EMA, α = 0.08 |

### Fusion and confidence

```
F = 0.40·A + 0.35·B + 0.25·C           (clamped to 0…1)

F ≥ 0.20  → confidence = min(confidence + 1, 5)
F <  0.20 → confidence = max(confidence − 1, 0)
```

- `confidence ≥ 5` and not yet detected → **detection confirmed** (alert on the rising edge only).
- `confidence == 0` while detected → detection released; confidence restarts at **3** (re-acquisition) so a returning target is picked up faster.

### Constants

| Constant | Value |
|---|---|
| `WEIGHT_A / B / C` | 0.40 / 0.35 / 0.25 |
| `FUSION_THRESHOLD` | 0.20 |
| `SIGNAL_RATIO_MIN / MAX` | 1.15 / 3.00 |
| `DIFF_RATIO_MIN / MAX` | 0.10 / 1.00 |
| `EMA_ALPHA` / `DIFF_ALPHA` | 0.01 / 0.08 |
| `CALIB_CYCLES` | 3 |
| `CONFIDENCE_MAX / DETECT / REACQUIRE` | 5 / 5 / 3 |

## 4. Reading the serial log

```
[MachineSense] #118  A=72%  B=59%  C=45%  F=61%  CONF=5/5  BINS=[294,738,679,272,161]
```

`#` window number · A/B/C/F scores in percent · `CONF` confidence · `BINS` magnitudes of bins 1–5.
Set `MACHINESENSE_DEBUG` to `0` in `detection.cpp` to silence this output.

## 5. Differences from the report

The report (Eq. 3.7, Table 3.1) documents an earlier version: `F = 0.60·A + 0.40·C`, threshold 0.60 and a minimum
absolute magnitude of 150. The firmware here adds the adaptive-floor method B and uses the weights/threshold above.
