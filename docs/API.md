# AutoExhaust HTTP API

Controls an exhaust fan and LED strip on a Bambu Lab X1C printer enclosure
(ESP32-C3). The device switches loads automatically from the printer's MQTT
state; this API reads its status and changes its settings.

- **Base URL**: `http://autoexhaust.local` or `http://192.168.50.161` (LAN only).
  `/` serves the web dashboard, which uses this same API.
- **Format**: JSON request and response bodies, `Content-Type: application/json`
- **Auth**: if the firmware is built with `SECRET_API_TOKEN`, every `POST`
  needs `Authorization: Bearer <token>`. `GET` never needs a token.
- **Errors**: `{"ok": false, "error": "<message>"}` with HTTP 400 / 401 / 404 / 503
- Percent values are integers 0–100.

## System states

| State | Meaning | Fan | Light |
|---|---|---|---|
| `BOOT` | Waiting for the first printer report | off | off |
| `IDLE` | Printer not printing | off | off |
| `RUNNING` | Printing (`PREPARE`, `RUNNING`, `PAUSE`) | on | on |
| `COOLDOWN` | After a print: fan 5 min, light 1 min | on | 1 min |
| `MANUAL` | Manual vent; auto-off after 60 min or when a started print ends | on | on |
| `OFFLINE` | Printer link lost; keeps a running print's loads for up to 30 min | held | held |
"On" uses the stored settings (`fan_pct`, `light_pct`). Transition rules:
`docs/AutoExhaust_Flow.md`.

**FAULT** (undervoltage, chip overheat, crash loop) is not a state you can
see here: the device turns loads off, saves a record and goes into deep sleep
with the RGB LED held in the fault color. **The API stops responding** until
someone power-cycles it. After that, `system.last_fault` explains what
happened. A device that stops answering may be in FAULT; it cannot be
recovered remotely.

## GET /api/status

Full device state.

```json
{
  "system":   {"firmware": "4f8ac8f-10281504", "firmware_state": "valid", "state": "RUNNING", "remaining_s": 0,
               "last_fault": null, "last_crash": null, "uptime_s": 3600,
               "reset_reason": "POWERON", "chip_temp_c": 45.2, "free_heap": 150000},
  "output":   {"fan_pct": 100, "light_pct": 70, "supply_v": 11.82},
  "settings": {"fan_pct": 100, "light_pct": 70, "fan_min_pct": 30, "light_max_pct": 70},
  "printer":  {"online": true, "state": "RUNNING", "printing": true,
               "nozzle_c": 220.0, "bed_c": 100.0, "chamber_light": true},
  "network":  {"rssi_dbm": -70, "mqtt": true, "ip": "192.168.50.161"}
}
```

- `remaining_s`: time left in `COOLDOWN` (fan), `MANUAL`, or the `OFFLINE` hold; 0 otherwise.
- `output` is what the pins drive right now; `settings` is what is used when loads are on.
- `firmware_state`: `pending_verify` for ~60 s after an OTA update, then `valid`.
- `last_fault` / `last_crash`: records from earlier boots, kept until
  acknowledged (`null` if none). Report them to the user when present.

```json
"last_fault": {"reason": "Chip overheat", "value": 82.4, "time": 1793170803,
               "uptime_s": 5321, "firmware": "f65bc1d-10281557", "logs": "<last 20 log lines>"},
"last_crash": {"reset_reason": "PANIC", "time": 1793170803, "firmware": "f65bc1d-10281557",
               "task": "loopTask", "pc": "0x40386382", "ra": "0x40386346", "mcause": 2}
```

`reason` is one of `Chip overheat` (°C), `Undervoltage` (V), `Crash loop`
(count). `time` is Unix time, 0 if the clock was not synced yet. A single
crash reboots automatically and only leaves `last_crash`.

## GET /api/logs

Last 50 log lines, oldest first.

```json
{"logs": ["[14:02:11] State: IDLE -> RUNNING", "[14:02:11] Chamber light -> ON (sent)"]}
```

## POST /api/settings

Sets fan and/or light level used whenever loads are on. Takes effect
immediately and persists across reboots.

```json
{"fan_pct": 80, "light_pct": 60}
```

Values are clamped: fan to `fan_min_pct`–100 (a 2-pin fan stalls below
~30%), light to 0–`light_max_pct` (power supply budget, 70%). The response
shows the values actually stored, so check it:

```json
{"ok": true, "settings": {"fan_pct": 80, "light_pct": 60, "fan_min_pct": 30, "light_max_pct": 70}}
```

## POST /api/manual

Starts or stops the manual vent.

```json
{"on": true}
```

Ignored (with a log line) when turning on while `RUNNING`.
Applied on the next loop; confirm with `/api/status` → `system.state`.

## POST /api/chamber_light

Sends an on/off command to the printer's own chamber light.

```json
{"on": false}
```

`503` if the printer MQTT link is down. The printer confirms by reporting
`printer.chamber_light`; check it after a few seconds. The device also does
this automatically: on when a print starts, off 1 min after it ends.

## POST /api/reboot

Reboots the device 1 s after responding. Body may be empty. It comes back in
~10 s (WiFi) and ~15 s (printer link).

## POST /api/fault/ack

Clears `last_fault` and `last_crash` after the user has seen them. Body may be empty.

## POST /api/test

**Verification only — do not call unless the user explicitly asks for this test.**

```json
{"action": "crash"}
```

| action | Effect |
|---|---|
| `crash` | `abort()` → reboots in ~10 s, leaves `last_crash` |
| `hang` | Infinite loop → loop watchdog reboots after 60 s, leaves `last_crash` |
| `fault_overheat`, `fault_undervoltage`, `fault_crash_loop` | Real FAULT: deep sleep, **needs a power cycle** |

Three crashes within 10 min of uptime cause a real crash-loop FAULT; reboot
with `/api/reboot` between crash tests to reset the counter.

## Examples

```bash
curl http://autoexhaust.local/api/status
curl http://autoexhaust.local/api/logs
curl -X POST http://autoexhaust.local/api/settings -H 'Content-Type: application/json' -d '{"light_pct": 60}'
curl -X POST http://autoexhaust.local/api/manual   -H 'Content-Type: application/json' -d '{"on": true}'
curl -X POST http://autoexhaust.local/api/reboot
```

## Not available via API

- Recovering from `FAULT` (needs a power cycle at the device).
- Exceeding the light/fan limits.
- Firmware update (use OTA from the development PC).
