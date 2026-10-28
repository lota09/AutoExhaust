# AutoExhaust HTTP API

Controls an exhaust fan and LED strip on a Bambu Lab X1C printer enclosure
(ESP32-C3). The device switches loads automatically from the printer's MQTT
state; this API reads its status and changes its settings.

- **Base URL**: `http://autoexhaust.local` or `http://192.168.50.161` (LAN only)
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
| `FAULT` | Undervoltage or chip overheat; loads off. **Cleared only by the physical button (hold 2 s)** | off | off |

"On" uses the stored settings (`fan_pct`, `light_pct`).

## GET /api/status

Full device state.

```json
{
  "system":   {"state": "RUNNING", "remaining_s": 0, "fault": "", "uptime_s": 3600,
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

Ignored (with a log line) during `FAULT`, or when turning on while `RUNNING`.
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

## Examples

```bash
curl http://autoexhaust.local/api/status
curl http://autoexhaust.local/api/logs
curl -X POST http://autoexhaust.local/api/settings -H 'Content-Type: application/json' -d '{"light_pct": 60}'
curl -X POST http://autoexhaust.local/api/manual   -H 'Content-Type: application/json' -d '{"on": true}'
curl -X POST http://autoexhaust.local/api/reboot
```

## Not available via API

- Clearing `FAULT` (needs a person at the device: hold button 2 s).
- Exceeding the light/fan limits.
- Firmware update (use OTA from the development PC).
