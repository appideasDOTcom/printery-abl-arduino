# printery-abl-arduino

ESP8266 (NodeMCU, `nodemcuv2` PlatformIO env) firmware that reads an HX711 load-cell
amplifier connected to a strain gauge and drives a digital output pin that acts as a
simulated limit switch. That output wires into a BigTreeTech SKR v2 board running
Marlin as an auto-bed-leveling (ABL) probe input.

Build/upload/monitor via PlatformIO — see the `pio` skill.

## Code style

This is a small, single-purpose sketch. Don't add layers, classes, or files the
current size doesn't need — a couple of well-named functions in `main.cpp` is fine.
The one firm rule: **no magic numbers**. Every pin, threshold, delay, timeout, and
calibration value must be a named `constexpr` (not a bare literal, not `#define`)
with a name that includes units where relevant, e.g. `PROBE_SETTLE_MS`,
`TRIGGER_THRESHOLD_RAW`, not `100` or `50000` inline.

## Performance constraints (read before touching `loop()` or probe logic)

The probe is polled from Marlin during ABL moves, so latency and jitter in the
main loop directly become Z-height error. Code in or called from `loop()` must
stay fast and predictable:

- **No blocking delays in the probe path.** Don't call `delay()` between a
  reading and setting the output pin. Use non-blocking polling (`scale.is_ready()`)
  rather than the blocking `scale.read()`/`read_average()` where the timing matters.
- **No `Serial.print` in the hot path.** Serial I/O is slow relative to loop timing;
  gate debug prints behind a compile-time flag or only emit them outside the
  timing-sensitive section.
- **Avoid `String` and heap allocation.** Use fixed-size buffers / primitive types;
  allocation and fragmentation cause unpredictable stalls on the ESP8266.
- **Prefer integer math over float in the hot path.** The ESP8266 has no hardware
  FPU, so floating-point ops are software-emulated and comparatively slow. Keep
  the raw HX711 reading and threshold comparison in integer domain; only convert
  to float for calibration/display code that isn't timing-critical.
- **Minimize work between "reading crossed threshold" and the `digitalWrite` that
  reflects it.** That gap is the probe's effective response latency.
- **Don't add WiFi/network code** to this sketch unless explicitly asked — the
  WiFi stack steals cycles and adds jitter to loop timing, which is the opposite
  of what a probe needs. If WiFi is ever added, keep it out of the probe's timing
  path or disable it (`WiFi.mode(WIFI_OFF)`) during probing.

When in doubt, ask "does this add latency or variance to the loop that runs
while the probe is active?" — if yes, it needs a good reason to be there.
