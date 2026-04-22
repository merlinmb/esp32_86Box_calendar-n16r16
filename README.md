# nextUp — ESP32 Calendar Display

A wall-mounted calendar display for the ESP32-S3 (N16R16 variant) with a 480×480 round RGB panel. Fetches events from a JSON calendar server and shows a rolling 7-day agenda with a large clock overlay.

## Hardware

| Component | Detail |
|-----------|--------|
| MCU | ESP32-S3 DevKitC-1 N16R16 (16 MB flash, 8 MB PSRAM) |
| Display | 480×480 round RGB panel, ST7701 driver |
| Touch | TAMC GT911 capacitive controller (I2C) |
| Interface | SPI (panel init) + parallel RGB (pixel data) |

### Pin assignments

**Display (RGB parallel)**

| Signal | GPIO |
|--------|------|
| DE | 18 |
| VSYNC | 17 |
| HSYNC | 16 |
| PCLK | 21 |
| R0–R4 | 11, 12, 13, 14, 0 |
| G0–G5 | 8, 20, 3, 46, 9, 10 |
| B0–B4 | 4, 5, 6, 7, 15 |
| Backlight | 38 |

**Display (SPI init)**

| Signal | GPIO |
|--------|------|
| CS | 39 |
| SCK | 48 |
| MOSI | 47 |

**Touch (I2C)**

| Signal | GPIO |
|--------|------|
| SDA | 19 |
| SCL | 45 |

## Software

Built with [PlatformIO](https://platformio.org/) targeting the Arduino framework.

### Dependencies

| Library | Purpose |
|---------|---------|
| [Arduino_GFX](https://github.com/moononournation/Arduino_GFX) | ST7701 RGB panel driver |
| [LVGL v8.3.11](https://github.com/lvgl/lvgl) | UI framework |
| [TAMC GT911](https://github.com/TAMCTec/gt911-arduino) | Touch controller |
| [ArduinoJson 7](https://arduinojson.org/) | Calendar JSON parsing |
| [NTPClient](https://github.com/arduino-libraries/NTPClient) | Time sync |
| [Time](https://github.com/PaulStoffregen/Time) | `hour()` / `minute()` helpers |
| [Timezone](https://github.com/JChristensen/Timezone) | DST-aware local time |
| [PubSubClient](https://github.com/knolleary/pubsubclient) | MQTT brightness control |

## Building

1. Install [PlatformIO](https://platformio.org/install).
2. Clone this repository.
3. Copy `include/secrets.h.example` to `include/secrets.h` and fill in your local defaults if you want compile-time values. The local file is gitignored.
4. Open the project folder in VS Code with the PlatformIO extension, or build from the CLI:

```bash
pio run
pio run --target upload
```

The upload port defaults to `COM19` — change `upload_port` in `platformio.ini` to match your system.

## First-run setup

On first boot (or when no WiFi credentials are stored) the device enters **AP setup mode**:

1. Connect to the WiFi network **CalendarSetup** from any phone or laptop.
2. A captive portal opens automatically — if it doesn't, navigate to `http://192.168.4.1`.
3. Fill in the form and tap **Save & Restart**.

Once credentials are saved they persist in NVS across reboots. The setup page remains available at the device's IP address while connected to the network.

### Configuration fields

| Field | Description |
|-------|-------------|
| WiFi SSID / Password | Network credentials |
| Calendar Server URL | Full URL to the JSON calendar endpoint, including `?timeframe=7d` |
| Read Token | Bearer token for the calendar API (optional) |
| Timezone | One of the supported zones — see `src/timezone_db.cpp` |
| Refresh Interval | How often to poll the calendar, in seconds (60–3600, default 300) |
| MQTT Host / Port | Broker address for brightness control messages |
| MQTT Brightness Topic | Topic that carries a `0–100` percentage payload |

## Calendar API

The device expects a JSON response of the form:

```json
{
  "events": [
    {
      "title": "Team standup",
      "start": "2025-04-22T09:00:00Z",
      "end":   "2025-04-22T09:30:00Z",
      "isAllDay": false,
      "location": "Meeting room 2",
      "calendar": "Work",
      "source": "google"
    }
  ]
}
```

`source` should be `"google"` or `"microsoft"` — this controls the colour of the vertical bar shown next to each event (blue for Google, teal for Microsoft).

Events are filtered to a 7-day window from the start of the current local day. All-day events are shown as chips above the timed events for that day.

## Display layout

```
┌─────────────────────────────────────────┐
│ nextUp                         offline  │  ← header (48 px)
├─────────────────────────────────────────┤
│                              HH:MM      │  ← large clock overlay (top-right)
│ TODAY          Tue 22 Apr               │
│ ████████████████████████████████████   │  ← all-day chips
│ 09:00 │ Team standup                   │
│       │ Work                           │
│ 10:30 │ 1:1 with Alice                 │
│                                        │
│ TOMORROW       Wed 23 Apr              │
│ …                                      │
└─────────────────────────────────────────┘
```

- Tapping an event row expands it to show the time range, location, and calendar name, then auto-collapses after 15 seconds.
- The view auto-scrolls back to the next upcoming event after 45 seconds of inactivity.
- The clock updates every second via `display_update_clock()`.

## MQTT brightness control

Subscribe the device to an MQTT topic. Publish a plain-text integer `0`–`100` (percentage). The device maps it to PWM duty on the backlight pin and updates immediately. Reconnection is attempted every 5 seconds if the broker is unavailable.

## Supported timezones

| Zone | DST |
|------|-----|
| Europe/London | GMT / BST |
| Europe/Paris | CET / CEST |
| Europe/Athens | EET / EEST |
| America/New_York | EST / EDT |
| America/Chicago | CST / CDT |
| America/Denver | MST / MDT |
| America/Los_Angeles | PST / PDT |
| UTC | — |

Additional zones can be added in `src/timezone_db.cpp`.
