---
name: pio
description: Build, upload, and monitor this PlatformIO/ESP8266 firmware project. Use when asked to build, flash, upload, run, or open the serial monitor for this project.
---

# PlatformIO build / upload / monitor

This is a PlatformIO project — env `nodemcuv2` (ESP8266 NodeMCU board, Arduino
framework, see `platformio.ini`). The `pio` CLI is available on this machine.

## Commands (run from repo root)

- Build only: `pio run`
- Upload to the board: `pio run -t upload`
- Serial monitor: `pio device monitor -b 115200` (matches `monitor_speed`)
- Build, upload, then monitor: `pio run -t upload && pio device monitor -b 115200`
- Clean build artifacts: `pio run -t clean`

## Notes

- Upload and monitor talk to real hardware over USB. Confirm the board is
  connected and safe to reflash before running an upload — don't upload if a
  print or probing sequence might be in progress.
- The serial monitor is a long-running foreground command (exit with Ctrl-C);
  don't run it in the background where its output won't be seen.
- If multiple serial devices are attached, PlatformIO may need `--upload-port`/
  `--monitor-port` — ask the user which port if autodetection is ambiguous.
