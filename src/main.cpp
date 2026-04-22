/*******************************************************************************
   LVGL Widgets
   This is a widgets demo for LVGL - Light and Versatile Graphics Library
   import from: https://github.com/lvgl/lv_demos.git

   Dependent libraries:
   LVGL: https://github.com/lvgl/lvgl.git

   Touch libraries:
   FT6X36: https://github.com/strange-v/FT6X36.git
   GT911: https://github.com/TAMCTec/gt911-arduino.git
   XPT2046: https://github.com/PaulStoffregen/XPT2046_Touchscreen.git

   LVGL Configuration file:
   Copy your_arduino_path/libraries/lvgl/lv_conf_template.h
   to your_arduino_path/libraries/lv_conf.h
   Then find and set:
   #define LV_COLOR_DEPTH     16
   #define LV_TICK_CUSTOM     1

   For SPI display set color swap can be faster, parallel screen don't set!
   #define LV_COLOR_16_SWAP   1

   Optional: Show CPU usage and FPS count
   #define LV_USE_PERF_MONITOR 1
 ******************************************************************************/


/*******************************************************************************
   Start of Arduino_GFX setting

   Arduino_GFX try to find the settings depends on selected board in Arduino IDE
   Or you can define the display dev kit not in the board list
   Defalult pin list for non display dev kit:
   Arduino Nano, Micro and more: CS:  9, DC:  8, RST:  7, BL:  6, SCK: 13, MOSI: 11, MISO: 12
   ESP32 various dev board     : CS:  5, DC: 27, RST: 33, BL: 22, SCK: 18, MOSI: 23, MISO: nil
   ESP32-C3 various dev board  : CS:  7, DC:  2, RST:  1, BL:  3, SCK:  4, MOSI:  6, MISO: nil
   ESP32-S2 various dev board  : CS: 34, DC: 35, RST: 33, BL: 21, SCK: 36, MOSI: 35, MISO: nil
   ESP32-S3 various dev board  : CS: 40, DC: 41, RST: 42, BL: 48, SCK: 36, MOSI: 35, MISO: nil
   ESP8266 various dev board   : CS: 15, DC:  4, RST:  2, BL:  5, SCK: 14, MOSI: 13, MISO: 12
   Raspberry Pi Pico dev board : CS: 17, DC: 27, RST: 26, BL: 28, SCK: 18, MOSI: 19, MISO: 16
   RTL8720 BW16 old patch core : CS: 18, DC: 17, RST:  2, BL: 23, SCK: 19, MOSI: 21, MISO: 20
   RTL8720_BW16 Official core  : CS:  9, DC:  8, RST:  6, BL:  3, SCK: 10, MOSI: 12, MISO: 11
   RTL8722 dev board           : CS: 18, DC: 17, RST: 22, BL: 23, SCK: 13, MOSI: 11, MISO: 12
   RTL8722_mini dev board      : CS: 12, DC: 14, RST: 15, BL: 13, SCK: 11, MOSI:  9, MISO: 10
   Seeeduino XIAO dev board    : CS:  3, DC:  2, RST:  1, BL:  0, SCK:  8, MOSI: 10, MISO:  9
   Teensy 4.1 dev board        : CS: 39, DC: 41, RST: 40, BL: 22, SCK: 13, MOSI: 11, MISO: 12
 ******************************************************************************/



#include "config.h"
#include "web_server.h"
#include "display.h"
#include "calendar_api.h"
#include "timezone_db.h"
#include "mqtt_client.h"

#include <WiFi.h>
#include <WiFiUdp.h>
#include <lvgl.h>
#include <Arduino_GFX_Library.h>
// #include "lv_demo_widgets.h"
//#include "demos/lv_demos.h"
#include <NTPClient.h>
#include <TimeLib.h>


#define GFX_BL 38


static AppConfig     g_cfg;
static CalEvent      g_event;
static bool          g_offline      = false;
static WiFiUDP       g_udp;
static NTPClient     g_ntp(g_udp, "pool.ntp.org", 0, 60000);
static Timezone     *g_tz   = nullptr;


// Display objects below belong to setup_display_inline() (not active).
// display_init() in display.cpp owns the live display objects.
// Arduino_DataBus *spibus = new Arduino_SWSPI(
//     GFX_NOT_DEFINED /* DC */, 39 /* CS */, 48 /* SCK */, 47 /* MOSI */);
// Arduino_ESP32RGBPanel *bus = new Arduino_ESP32RGBPanel(
//     18 /* DE */, 17 /* VSYNC */, 16 /* HSYNC */, 21 /* PCLK */,
//     11 /* R0 */, 12 /* R1 */, 13 /* R2 */, 14 /* R3 */, 0 /* R4 */,
//     8 /* G0 */, 20 /* G1 */, 3 /* G2 */, 46 /* G3 */, 9 /* G4 */, 10 /* G5 */,
//     4 /* B0 */, 5 /* B1 */, 6 /* B2 */, 7 /* B3 */, 15 /* B4 */,
//     1 /* hsync_polarity */, 10 /* hsync_front_porch */, 8 /* hsync_pulse_width */, 50 /* hsync_back_porch */,
//     1 /* vsync_polarity */, 10 /* vsync_front_porch */, 8 /* vsync_pulse_width */, 20 /* vsync_back_porch */,
//     0 /* pclk_active_neg */, 12000000 /* prefer_speed */
// );
// Arduino_RGB_Display *gfx = new Arduino_RGB_Display(
//     480 /* width */, 480 /* height */, bus, 0 /* rotation */, true /* auto_flush */,
//     spibus, GFX_NOT_DEFINED /* RST */,
//     st7701_type1_init_operations, sizeof(st7701_type1_init_operations));

/*******************************************************************************
   Please config the touch panel in touch.h
 ******************************************************************************/
#include "touch.h"

#include <esp_heap_caps.h>

static unsigned long g_last_clock   = 0;
static unsigned long g_last_breathe = 0;
static unsigned long g_last_fetch   = 0;
static bool          g_first_fetch  = true;
static bool          g_wifi_connected = false;

static const unsigned long WIFI_RECONNECT_INTERVAL_MS = 10000UL;
static unsigned long g_last_wifi_retry = 0;

static void begin_wifi_station() {
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.persistent(false);
  WiFi.begin(g_cfg.wifi_ssid, g_cfg.wifi_password);
}

static void sync_time() {
    g_tz = tz_lookup(g_cfg.timezone);

    bool ok = false;
    for (int i = 0; i < 10 && !ok; i++) {
        ok = g_ntp.forceUpdate();
        if (!ok) delay(500);
    }

    time_t utc = (time_t)g_ntp.getEpochTime();
    time_t local = g_tz->toLocal(utc);
    setTime(local);
    Serial.printf("[ntp] UTC=%ld  local=%ld  tz=%s\n",
                  (long)utc, (long)local, g_cfg.timezone);
}

  static void on_wifi_connected(const char *reason) {
    g_wifi_connected = true;
    Serial.printf("[wifi] %s. IP: %s\n", reason, WiFi.localIP().toString().c_str());
    g_ntp.begin();
    sync_time();
    display_set_connection_status(true, mqtt_client_connected());
  }

  static void maintain_wifi() {
    if (g_ap_mode || g_cfg.wifi_ssid[0] == '\0') return;

    if (WiFi.status() == WL_CONNECTED) {
      if (!g_wifi_connected) {
        on_wifi_connected("Reconnected");
      }
      return;
    }

    if (g_wifi_connected) {
      g_wifi_connected = false;
      Serial.println("[wifi] Connection lost");
      mqtt_client_on_wifi_disconnected();
      display_set_connection_status(false, false);
    }

    unsigned long now = millis();
    if (now - g_last_wifi_retry < WIFI_RECONNECT_INTERVAL_MS) return;

    g_last_wifi_retry = now;
    Serial.printf("[wifi] Reconnecting to '%s'\n", g_cfg.wifi_ssid);
    WiFi.disconnect(true, false);
    begin_wifi_station();
  }

void loadConfig(){
    config_load(g_cfg);
  Serial.printf("[boot] Config loaded. WiFi configured: %s\n",
          g_cfg.wifi_ssid[0] != '\0' ? "yes" : "no");

    if (g_cfg.wifi_ssid[0] == '\0') {
        Serial.println("[boot] No WiFi credentials — entering AP setup mode");
        ws_start_ap(g_cfg);
        //display_show_setup();
        return;
    }
}

void setupWifi()
{
    display_show_connecting(g_cfg.wifi_ssid);

  begin_wifi_station();
    Serial.print("[boot] Connecting to configured WiFi");

    unsigned long start = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - start < 20000) {
        delay(250);
        lv_timer_handler();
        Serial.print(".");
    }
    Serial.println();

    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("[boot] WiFi failed — falling back to AP setup mode");
      g_wifi_connected = false;
        ws_start_ap(g_cfg);
        //display_show_setup();
        return;
    }

    on_wifi_connected("Connected");
    ws_start_sta(g_cfg);
    mqtt_client_init(g_cfg);
}

// Rollback: swap display_init() in setup() for setup_display_inline() and restore the
// commented globals/objects above if needed.
#if 0
static uint32_t screenWidth;
static uint32_t screenHeight;
static lv_disp_draw_buf_t draw_buf;
static lv_color_t *disp_draw_buf;
static lv_disp_drv_t disp_drv;

static void my_disp_flush(lv_disp_drv_t *disp, const lv_area_t *area, lv_color_t *color_p)
{
  uint32_t w = (area->x2 - area->x1 + 1);
  uint32_t h = (area->y2 - area->y1 + 1);
#if (LV_COLOR_16_SWAP != 0)
  gfx->draw16bitBeRGBBitmap(area->x1, area->y1, (uint16_t *)&color_p->full, w, h);
#else
  gfx->draw16bitRGBBitmap(area->x1, area->y1, (uint16_t *)&color_p->full, w, h);
#endif
  lv_disp_flush_ready(disp);
}

static void my_touchpad_read(lv_indev_drv_t *indev_driver, lv_indev_data_t *data)
{
  if (touch_has_signal()) {
    if (touch_touched()) {
      data->state   = LV_INDEV_STATE_PR;
      data->point.x = touch_last_x;
      data->point.y = touch_last_y;
    } else if (touch_released()) {
      data->state = LV_INDEV_STATE_REL;
    }
  } else {
    data->state = LV_INDEV_STATE_REL;
  }
}

static void setup_display_inline()
{
  touch_init();
  gfx->begin(11000000);
  gfx->fillScreen(RGB565_BLACK);
#ifdef GFX_BL
  ledcAttach(GFX_BL, 600, 8);
  ledcWrite(GFX_BL, 150);
#endif
  lv_init();
  screenWidth = gfx->width();
  screenHeight = gfx->height();
  lv_color_t *buf_3_1 = (lv_color_t *)heap_caps_malloc(sizeof(lv_color_t) * screenWidth * 480, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!buf_3_1) { Serial.println("LVGL disp_draw_buf allocate failed!"); return; }
  lv_disp_draw_buf_init(&draw_buf, buf_3_1, NULL, screenWidth * 480);
  lv_disp_drv_init(&disp_drv);
  disp_drv.hor_res = screenWidth;
  disp_drv.ver_res = screenHeight;
  disp_drv.flush_cb = my_disp_flush;
  disp_drv.draw_buf = &draw_buf;
  lv_disp_drv_register(&disp_drv);
  static lv_indev_drv_t indev_drv;
  lv_indev_drv_init(&indev_drv);
  indev_drv.type = LV_INDEV_TYPE_POINTER;
  indev_drv.read_cb = my_touchpad_read;
  lv_indev_drv_register(&indev_drv);
}
#endif  // end rollback block

void setup()
{
  Serial.begin(115200);

  Serial.println("LVGL Widgets Demo");

  display_init();

  loadConfig();
  setupWifi();

  // lv_demo_widgets();
  // lv_demo_benchmark();

  Serial.println("Setup done");
  lv_timer_handler(); /* let the GUI do its work */
}

void loop()
{
  lv_timer_handler();
  lv_timer_handler();
  ws_handle();
  delay(2);

    unsigned long now = millis();

    maintain_wifi();
    mqtt_client_tick();
    display_set_connection_status(g_wifi_connected, mqtt_client_connected());

    static unsigned long s_last_ntp = 0;
    if (g_wifi_connected && now - s_last_ntp >= 60000UL) {
        s_last_ntp = now;
        if (g_ntp.update()) {
            time_t utc   = (time_t)g_ntp.getEpochTime();
            time_t local = g_tz->toLocal(utc);
            setTime(local);
        }
    }

    if (g_first_fetch || (now - g_last_fetch >= (unsigned long)g_cfg.refresh_secs * 1000UL)) {
        g_last_fetch  = now;
        g_first_fetch = false;

        Serial.println("[loop] Fetching calendar...");
        static CalEvent tmp;
        bool ok = calendar_fetch(g_cfg, tmp, g_tz);

        if (ok) {
            g_event   = tmp;
            g_offline = false;
            const char *first_title = (g_event.has_event && g_event.count > 0) ? g_event.items[0].title : "";
            Serial.printf("[loop] Fetch OK. has_event=%d title='%s'\n",
                          g_event.has_event, first_title);
        } else {
            g_offline = true;
            Serial.println("[loop] Fetch failed — showing offline indicator");
        }

        display_render(g_event, g_offline);
        g_last_clock   = now;
        g_last_breathe = now;
    }

    if (now - g_last_clock >= 1000UL) {
        g_last_clock = now;
        display_update_clock(g_offline);
    }

    if (now - g_last_breathe >= 30000UL) {
        g_last_breathe = now;
        display_breathe(g_event, g_offline);
    }

}
