# WebSocket protocol

The ESP32 runs a WebSocket server on **port 81** (`ws://<host>:81`). Messages are plain-text, comma-separated and start with `$MachineSense`. The dashboard matches the prefix case-insensitively. No client-to-device commands are implemented yet.

## `STATUS` — sent once to a newly connected client

```
$MachineSense,STATUS,CONNECTED
```

## `DATA` — broadcast once per FFT window (≈ every 4.1 s)

```
$MachineSense,DATA,<bFreq>,<hFreq>,<bMag>,<hMag>,<conf>,<confMax>,<ms>,<state>,<A>,<B>,<C>,<F>,<detected>
```

| # | Field | Format | Meaning |
|--:|---|---|---|
| 2 | `bFreq` | `%.3f` Hz | Breathing peak frequency (0.2–0.6) |
| 3 | `hFreq` | `%.3f` Hz | Heartbeat peak frequency (1.0–2.5, experimental) |
| 4 | `bMag` | `%.1f` | Breathing peak FFT magnitude |
| 5 | `hMag` | `%.1f` | Heartbeat peak FFT magnitude |
| 6 | `conf` | int | Detection confidence 0–5 |
| 7 | `confMax` | int | Confidence scale maximum (5) |
| 8 | `ms` | unsigned long | Device uptime in ms |
| 9 | `state` | `CALIBRATING` \| `ACTIVE` | System state |
| 10–12 | `A`,`B`,`C` | `%.3f` | Method scores, 0–1 |
| 13 | `F` | `%.3f` | Fused score, 0–1 |
| 14 | `detected` | `0` \| `1` | Confirmed detector state |

(Indices are zero-based positions after splitting on commas, matching `dashboard/app.js`.)

Illustrative example:

```
$MachineSense,DATA,0.407,1.542,812.3,455.1,5,5,123456,ACTIVE,0.512,0.301,0.422,0.404,1
```

Older firmware that sends fewer than 15 fields is still handled: the dashboard falls back to `conf >= 5` for the detected state.
While `state` is `CALIBRATING` the dashboard shows the calibration overlay and ignores the values.

## `ALERT` — broadcast on the rising edge of a detection

```
$MachineSense,ALERT,HUMAN_DETECTED,TIME:<ms>
```

The dashboard shows a red alert for 5 s, increments the alert counter, plays a tone and logs the device timestamp.

## Limits

- Max `DATA` packet size is 255 characters (`char dataPacket[256]`).
- Transport is unencrypted and unauthenticated; do not expose port 81 beyond a trusted network.
