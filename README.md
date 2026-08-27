# Gas Leak and Fire Detection System — Phase 2

**Team:** IOTronics (BITS)

**Members:** Utsav Pati Tripathi (coordinator, backend/DB), Parth Parolekar (hardware/firmware), Neeraj Kumar (dashboard/backend, architecture)

## Overview

An ESP32-based sensor node reads gas/smoke concentration from an MQ-2 sensor, computes a smoke ppm value, and sends it to a central server over HTTP. The server stores readings and pushes live updates to a web dashboard via SSE. Phase 2 adds a local hardware alert (LED + buzzer) that reacts independently of network status.

## Phase 2 Scope (this submission)

- MQ-2 analog reading via ESP32 ADC (oneshot driver, line-fitting calibration)
- Rs/Ro computation and smoke ppm estimate (LPG power-law curve, used as an MQ-2 general-smoke approximation)
- Wi-Fi connection (credentials via Kconfig, not hardcoded)
- Sensor readings POSTed to Laravel backend as JSON over HTTP
- **New:** Local LED + active buzzer alert with hysteresis thresholds, independent of Wi-Fi/HTTP status

**Out of scope for now:** exposing alert state in the HTTP payload, server-configured thresholds, additional gas curves — noted for Phase 3.

## ESP32 Board Specs

- **Chip:** ESP32-WROOM-32, dual-core, 240MHz
- **Flash:** 4MB
- **SRAM:** 520KB
- **GPIO:** 30 pins, supporting ADC, DAC, PWM, I2C, SPI, UART, Touch, JTAG

## Components Used

| Component | Purpose |
|---|---|
| ESP32-WROOM DevKit (4MB flash) | Main microcontroller |
| MQ-2 gas sensor (Flying Fish module) | Smoke/gas sensing (analog output) |
| LED | Visual alert indicator |
| Active buzzer | Audible alert indicator |
| 2× 10kΩ resistor | Voltage divider (5V sensor output → 3.3V-safe ADC input) |
| 1× 330Ω resistor | Current-limiting resistor for LED |

## Circuit Overview

- **MQ-2 AOUT** → voltage divider (10kΩ/10kΩ) → **GPIO34** (ADC1_CH6). The divider halves the MQ-2's 5V-referenced output so the ESP32's 3.3V-max ADC pin isn't damaged. Firmware multiplies the reading back by 2 to reconstruct the true AOUT voltage.
- **MQ-2 VCC** → 5V, **GND** → common ground with ESP32.
- **LED** anode → **GPIO25** through a 330Ω resistor → LED cathode → GND.
- **Active buzzer** positive lead → **GPIO26** → buzzer negative lead → GND.

```
  5V ──── MQ-2 (VCC)
           │
        AOUT pin
           │
           |
          10kΩ             
           │               
           ├──── GPIO34 (ADC1_CH6)
           │
          10kΩ
           │
          GND

  GPIO25 ──── 330Ω ──── LED (anode) ──── LED (cathode) ──── GND

  GPIO26 ──── Buzzer (+) ──── Buzzer (-) ──── GND

  MQ-2 (GND) ──── ESP32 (GND)   [common ground for all components]
```

## Firmware Notes

- **Framework:** ESP-IDF v5.3.5 (not Arduino)
- ADC calibration uses the line-fitting scheme (correct for original ESP32; curve-fitting is for S2/S3/C3)
- RL on the Flying Fish module is undocumented; assumed 5kΩ — cancels out of the Rs/Ro ratio, so this doesn't affect ppm accuracy
- MQ-2 gives a single Rs/Ro ratio, not gas-specific identification — one "smoke ppm" value is used as a general air-quality/alert signal
- LED/buzzer alert uses hysteresis (ON above threshold, OFF at a lower threshold) to avoid flickering near the trigger point
- Wi-Fi SSID/password and server URL are set via `idf.py menuconfig`, not hardcoded

## Backend Notes

- PHP Laravel + MySQL, SSE used for server → dashboard live updates (simpler than WebSockets for one-directional data)
- Laravel dev server must run with `--host=0.0.0.0` to be reachable from the ESP32 over LAN
- MySQL service must be running before starting the server — a stopped MySQL service silently causes HTTP request timeouts on the ESP32 side

## Testing

- Baseline/clean-air behavior verified (Rs/Ro settles near the MQ-2 datasheet clean-air factor of 9.83)
- Alert threshold tested by introducing smoke (e.g. a lit-then-extinguished matchstick or incense held near the sensor) and confirming LED/buzzer trigger and clear correctly with hysteresis
- End-to-end HTTP posting verified against live server (HTTP 201 responses, ~2s interval matching firmware loop delay)