# Contributing

Thanks for your interest in MachineSense! Issues and pull requests are welcome.

## Getting set up
1. Install [PlatformIO](https://platformio.org/install).
2. `cd firmware && pio run` to build (no hardware or Wi-Fi credentials needed).
3. Dashboard: open `dashboard/index.html`; `node --check dashboard/app.js` for a syntax check.

## Guidelines
- **Never commit credentials.** Wi-Fi settings live in the git-ignored `firmware/include/secrets.h`.
- Keep the FreeRTOS contract: producers use zero-timeout queue sends; don't block the 250 Hz acquisition task.
- Use `float`, not `double`, in the DSP path (the ESP32 FPU is single-precision).
- Tuning constants belong at the top of `detection.cpp`; if you change them, update `docs/signal-processing.md` and the README table.
- If the packet format changes, update `docs/protocol.md` and `dashboard/app.js` together.
- Schematic changes: edit `docs/schematic/generate_schematic.py` and re-run it so the PNG/SVG/PDF stay in sync.
- Describe how you tested the change (bench setup, serial log, screenshot).

## Pull requests
Branch from `main`, keep PRs focused, and fill in the PR template.
