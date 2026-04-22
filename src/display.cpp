#include "display.h"

#include <Arduino.h>
#include <Arduino_GFX_Library.h>
#include <Wire.h>
#include <TAMC_GT911.h>
#include <WiFi.h>
#include <lvgl.h>
#include <TimeLib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <esp_heap_caps.h>

#include "ui_fonts.h"

// Corrected ST7701 init: 0x20 = INVOFF (not 0x21 IPS inversion), 0x50 = RGB565 (not 0x60 RGB666)
static const uint8_t st7701_init_corrected[] = {
    BEGIN_WRITE,
    WRITE_COMMAND_8, 0xFF,
    WRITE_BYTES, 5, 0x77, 0x01, 0x00, 0x00, 0x10,

    WRITE_C8_D16, 0xC0, 0x3B, 0x00,
    WRITE_C8_D16, 0xC1, 0x0D, 0x02,
    WRITE_C8_D16, 0xC2, 0x31, 0x05,
    WRITE_C8_D8, 0xCD, 0x08,

    WRITE_COMMAND_8, 0xB0, // Positive Voltage Gamma Control
    WRITE_BYTES, 16,
    0x00, 0x11, 0x18, 0x0E,
    0x11, 0x06, 0x07, 0x08,
    0x07, 0x22, 0x04, 0x12,
    0x0F, 0xAA, 0x31, 0x18,

    WRITE_COMMAND_8, 0xB1, // Negative Voltage Gamma Control
    WRITE_BYTES, 16,
    0x00, 0x11, 0x19, 0x0E,
    0x12, 0x07, 0x08, 0x08,
    0x08, 0x22, 0x04, 0x11,
    0x11, 0xA9, 0x32, 0x18,

    WRITE_COMMAND_8, 0xFF,
    WRITE_BYTES, 5, 0x77, 0x01, 0x00, 0x00, 0x11,

    WRITE_C8_D8, 0xB0, 0x60,
    WRITE_C8_D8, 0xB1, 0x32,
    WRITE_C8_D8, 0xB2, 0x07,
    WRITE_C8_D8, 0xB3, 0x80,
    WRITE_C8_D8, 0xB5, 0x49,
    WRITE_C8_D8, 0xB7, 0x85,
    WRITE_C8_D8, 0xB8, 0x21,
    WRITE_C8_D8, 0xC1, 0x78,
    WRITE_C8_D8, 0xC2, 0x78,

    WRITE_COMMAND_8, 0xE0,
    WRITE_BYTES, 3, 0x00, 0x1B, 0x02,

    WRITE_COMMAND_8, 0xE1,
    WRITE_BYTES, 11,
    0x08, 0xA0, 0x00, 0x00,
    0x07, 0xA0, 0x00, 0x00,
    0x00, 0x44, 0x44,

    WRITE_COMMAND_8, 0xE2,
    WRITE_BYTES, 12,
    0x11, 0x11, 0x44, 0x44,
    0xED, 0xA0, 0x00, 0x00,
    0xEC, 0xA0, 0x00, 0x00,

    WRITE_COMMAND_8, 0xE3,
    WRITE_BYTES, 4, 0x00, 0x00, 0x11, 0x11,

    WRITE_C8_D16, 0xE4, 0x44, 0x44,

    WRITE_COMMAND_8, 0xE5,
    WRITE_BYTES, 16,
    0x0A, 0xE9, 0xD8, 0xA0,
    0x0C, 0xEB, 0xD8, 0xA0,
    0x0E, 0xED, 0xD8, 0xA0,
    0x10, 0xEF, 0xD8, 0xA0,

    WRITE_COMMAND_8, 0xE6,
    WRITE_BYTES, 4, 0x00, 0x00, 0x11, 0x11,

    WRITE_C8_D16, 0xE7, 0x44, 0x44,

    WRITE_COMMAND_8, 0xE8,
    WRITE_BYTES, 16,
    0x09, 0xE8, 0xD8, 0xA0,
    0x0B, 0xEA, 0xD8, 0xA0,
    0x0D, 0xEC, 0xD8, 0xA0,
    0x0F, 0xEE, 0xD8, 0xA0,

    WRITE_COMMAND_8, 0xEB,
    WRITE_BYTES, 7,
    0x02, 0x00, 0xE4, 0xE4,
    0x88, 0x00, 0x40,

    WRITE_C8_D16, 0xEC, 0x3C, 0x00,

    WRITE_COMMAND_8, 0xED,
    WRITE_BYTES, 16,
    0xAB, 0x89, 0x76, 0x54,
    0x02, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0x20,
    0x45, 0x67, 0x98, 0xBA,

    WRITE_COMMAND_8, 0xFF,
    WRITE_BYTES, 5, 0x77, 0x01, 0x00, 0x00, 0x13,

    WRITE_C8_D8, 0xE5, 0xE4,

    WRITE_COMMAND_8, 0xFF,
    WRITE_BYTES, 5, 0x77, 0x01, 0x00, 0x00, 0x00,

    WRITE_COMMAND_8, 0x20,   // INVOFF — was 0x21 (IPS inversion), causing all colours to be inverted
    WRITE_C8_D8, 0x3A, 0x50, // RGB565 — was 0x60 (RGB666), mismatched with 16-bit framebuffer

    WRITE_COMMAND_8, 0x11, // Sleep Out
    END_WRITE,

    DELAY, 120,

    BEGIN_WRITE,
    WRITE_COMMAND_8, 0x29, // Display On
    END_WRITE,
};

// ── Pin definitions ────────────────────────────────────────────────────────

#define GFX_BL 38

// Touch (GT911)
#define TOUCH_SCL 45
#define TOUCH_SDA 19
#define TOUCH_INT -1
#define TOUCH_RST -1
#define TOUCH_MAP_X1 480
#define TOUCH_MAP_X2 0
#define TOUCH_MAP_Y1 480
#define TOUCH_MAP_Y2 0

// ── Arduino_GFX display ────────────────────────────────────────────────────

static Arduino_DataBus *s_spibus = new Arduino_SWSPI(
    GFX_NOT_DEFINED /* DC */, 39 /* CS */, 48 /* SCK */, 47 /* MOSI */);

static Arduino_ESP32RGBPanel *s_rgb_bus = new Arduino_ESP32RGBPanel(
    18 /* DE */, 17 /* VSYNC */, 16 /* HSYNC */, 21 /* PCLK */,
    11 /* R0 */, 12 /* R1 */, 13 /* R2 */, 14 /* R3 */, 0  /* R4 */,
    8  /* G0 */, 20 /* G1 */, 3  /* G2 */, 46 /* G3 */, 9  /* G4 */, 10 /* G5 */,
    4  /* B0 */, 5  /* B1 */, 6  /* B2 */, 7  /* B3 */, 15 /* B4 */,
    1  /* hsync_polarity */, 10 /* hsync_front_porch */, 8 /* hsync_pulse_width */, 50 /* hsync_back_porch */,
    1  /* vsync_polarity */, 10 /* vsync_front_porch */, 8 /* vsync_pulse_width */, 20 /* vsync_back_porch */,
    0  /* pclk_active_neg */, 12000000 /* prefer_speed */);

static Arduino_RGB_Display *s_gfx = new Arduino_RGB_Display(
    480 /* width */, 480 /* height */, s_rgb_bus, 0 /* rotation */, true /* auto_flush */,
    s_spibus, GFX_NOT_DEFINED /* RST */,
    st7701_init_corrected, sizeof(st7701_init_corrected));

// ── Touch ──────────────────────────────────────────────────────────────────

static TAMC_GT911 s_ts(TOUCH_SDA, TOUCH_SCL, TOUCH_INT, TOUCH_RST,
                       max(TOUCH_MAP_X1, TOUCH_MAP_X2),
                       max(TOUCH_MAP_Y1, TOUCH_MAP_Y2));

static int s_touch_x = 0, s_touch_y = 0;

// ── LVGL draw buffer / driver ──────────────────────────────────────────────

static lv_disp_draw_buf_t s_draw_buf;
static lv_disp_drv_t      s_disp_drv;
static bool               s_display_ready = false;

// ── LVGL callbacks ────────────────────────────────────────────────────────

static void lvgl_flush_cb(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *color_p) {
    uint32_t w = area->x2 - area->x1 + 1;
    uint32_t h = area->y2 - area->y1 + 1;

    uint16_t *fb = s_gfx->getFramebuffer();
    uint16_t *src = (uint16_t *)&color_p->full;

    if (area->x1 == 0 && w == 480) {
        memcpy(fb + area->y1 * 480, src, w * h * 2);
        Cache_WriteBack_Addr((uint32_t)(fb + area->y1 * 480), w * h * 2);
    } else {
        for (uint32_t row = 0; row < h; row++) {
            memcpy(fb + (area->y1 + row) * 480 + area->x1, src + row * w, w * 2);
        }
        Cache_WriteBack_Addr((uint32_t)(fb + area->y1 * 480 + area->x1), (h - 1) * 480 * 2 + w * 2);
    }

    lv_disp_flush_ready(drv);
}

static void lvgl_touch_cb(lv_indev_drv_t *drv, lv_indev_data_t *data) {
    static bool          s_was_touched  = false;
    static unsigned long s_touch_down_ms = 0;
    static constexpr unsigned long DEBOUNCE_MS = 50;

    s_ts.read();
    if (s_ts.isTouched) {
        unsigned long now_ms = millis();
        if (!s_was_touched) {
            s_touch_down_ms = now_ms;
            s_was_touched   = true;
        }
        s_touch_x = map(s_ts.points[0].x, TOUCH_MAP_X1, TOUCH_MAP_X2, 0, 479);
        s_touch_y = map(s_ts.points[0].y, TOUCH_MAP_Y1, TOUCH_MAP_Y2, 0, 479);
        // Only report as pressed after debounce period
        if (now_ms - s_touch_down_ms >= DEBOUNCE_MS) {
            data->state   = LV_INDEV_STATE_PR;
            data->point.x = s_touch_x;
            data->point.y = s_touch_y;
        } else {
            data->state   = LV_INDEV_STATE_REL;
            data->point.x = s_touch_x;
            data->point.y = s_touch_y;
        }
    } else if (s_touch_x || s_touch_y) {
        data->state   = LV_INDEV_STATE_REL;
        data->point.x = s_touch_x;
        data->point.y = s_touch_y;
        s_was_touched = false;
        s_touch_x = 0;
        s_touch_y = 0;
    } else {
        data->state   = LV_INDEV_STATE_REL;
        s_was_touched = false;
    }
}

// ── UI state ──────────────────────────────────────────────────────────────

namespace {

constexpr int SCREEN_W  = 480;
constexpr int SCREEN_H  = 480;
constexpr int CARD_INSET = 0;
constexpr int CARD_RADIUS = 0;
constexpr int HEADER_H  = 48;
constexpr int BODY_PAD  = 16;

const lv_color_t COLOR_BG          = lv_color_hex(0x000000); //lv_color_hex(0x0b0c11);
const lv_color_t COLOR_SURFACE     = lv_color_hex(0x111318);
const lv_color_t COLOR_SURFACE_ALT = lv_color_hex(0x171a21);
const lv_color_t COLOR_TEXT_1      = lv_color_hex(0xe8eaf2);
const lv_color_t COLOR_TEXT_2      = lv_color_hex(0x8b90aa);
const lv_color_t COLOR_TEXT_3      = lv_color_hex(0x50546a);
const lv_color_t COLOR_ACCENT      = lv_color_hex(0x5b8ef0);
const lv_color_t COLOR_AZURE       = lv_color_hex(0x00a6ff);
const lv_color_t COLOR_ALERT       = lv_color_hex(0xe26d5a);
const lv_color_t COLOR_CYAN        = lv_color_hex(0x00e5ff);
const lv_color_t COLOR_CLOCK       = lv_color_hex(0xffffff);
constexpr lv_opa_t BACKGROUND_CLOCK_OPA = LV_OPA_COVER;

// hsl(220, 75%, 62%) -> #5b8ef0  hsl(185, 60%, 48%) -> #31a8bb
lv_color_t source_color(const char *source) {
    if (source && strcmp(source, "microsoft") == 0) return lv_color_hex(0x31a8bb);
    return lv_color_hex(0x5b8ef0); // google or unknown -> blue
}

enum class ScreenMode { Setup, Connecting, Agenda };

lv_obj_t *s_card             = nullptr;
lv_obj_t *s_title_label      = nullptr;
lv_obj_t *s_offline_label    = nullptr;
lv_obj_t *s_wifi_status_label = nullptr;
lv_obj_t *s_mqtt_status_label = nullptr;
lv_obj_t *s_background_clock = nullptr;
ScreenMode s_mode             = ScreenMode::Setup;

bool s_wifi_connected = false;
bool s_mqtt_connected = false;
bool s_title_expanded = false;

lv_obj_t *s_body             = nullptr;
lv_obj_t *s_sticky_day_label = nullptr;

static constexpr uint8_t CAL_MAX_DAYS = 8;
struct DayAnchor { lv_coord_t top_y; char label[32]; };
DayAnchor s_day_anchors[CAL_MAX_DAYS];
uint8_t   s_day_anchor_count = 0;

lv_obj_t  *s_expanded_row  = nullptr;
lv_timer_t *s_expand_timer = nullptr;
lv_timer_t *s_return_timer = nullptr;
lv_timer_t *s_title_timer  = nullptr;

lv_obj_t *s_event_row_objs[CAL_MAX_EVENTS];
uint8_t   s_event_row_count = 0;

CalEvent s_cached_ev;

// ── helpers ────────────────────────────────────────────────────────────────

int64_t days_from_civil(int year, unsigned month, unsigned day) {
    year -= month <= 2;
    const int era = (year >= 0 ? year : year - 399) / 400;
    const unsigned yoe = static_cast<unsigned>(year - era * 400);
    const unsigned doy = (153 * (month + (month > 2 ? -3 : 9)) + 2) / 5 + day - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097LL + static_cast<int64_t>(doe) - 719468LL;
}

time_t tm_to_epoch_utc(const struct tm &t) {
    const int64_t days = days_from_civil(t.tm_year + 1900,
                                         static_cast<unsigned>(t.tm_mon + 1),
                                         static_cast<unsigned>(t.tm_mday));
    return static_cast<time_t>((((days * 24) + t.tm_hour) * 60 + t.tm_min) * 60 + t.tm_sec);
}

void cleanup_timers() {
    if (s_expand_timer) { lv_timer_del(s_expand_timer); s_expand_timer = nullptr; }
    if (s_return_timer) { lv_timer_del(s_return_timer); s_return_timer = nullptr; }
    if (s_title_timer) { lv_timer_del(s_title_timer); s_title_timer = nullptr; }
    s_expanded_row     = nullptr;
    s_day_anchor_count = 0;
    s_event_row_count  = 0;
    s_body             = nullptr;
    s_sticky_day_label = nullptr;
    s_title_expanded   = false;
}

void format_hhmm(char *out, size_t out_size) {
    snprintf(out, out_size, "%02d:%02d", hour(), minute());
}

void format_day_caption(time_t local_epoch, char *out, size_t out_size) {
    static const char *DAYS[]   = {"Sun","Mon","Tue","Wed","Thu","Fri","Sat"};
    static const char *MONTHS[] = {"Jan","Feb","Mar","Apr","May","Jun",
                                    "Jul","Aug","Sep","Oct","Nov","Dec"};
    struct tm ti;
    gmtime_r(&local_epoch, &ti);
    snprintf(out, out_size, "%s %d %s", DAYS[ti.tm_wday], ti.tm_mday, MONTHS[ti.tm_mon]);
}

time_t local_midnight(time_t local_epoch) {
    struct tm ti;
    gmtime_r(&local_epoch, &ti);
    ti.tm_hour = 0; ti.tm_min = 0; ti.tm_sec = 0;
    return tm_to_epoch_utc(ti);
}

bool event_overlaps_day(const AgendaEntry &entry, time_t day_start, time_t day_end) {
    const time_t end = entry.ts_end_local > entry.ts_start_local
        ? entry.ts_end_local
        : entry.ts_start_local + (entry.is_all_day ? 86400 : 60);
    return end > day_start && entry.ts_start_local < day_end;
}

lv_obj_t *create_label(lv_obj_t *parent, const lv_font_t *font,
                        lv_color_t color, lv_opa_t opa, const char *text) {
    lv_obj_t *lbl = lv_label_create(parent);
    lv_obj_set_style_text_font(lbl, font, 0);
    lv_obj_set_style_text_color(lbl, color, 0);
    lv_obj_set_style_text_opa(lbl, opa, 0);
    lv_label_set_text(lbl, text ? text : "");
    return lbl;
}


void set_text(lv_obj_t *lbl, const char *text) {
    if (lbl) lv_label_set_text(lbl, text ? text : "");
}

void collapse_title();

void update_title_label() {
    if (!s_title_label) return;

    if (!s_title_expanded || WiFi.status() != WL_CONNECTED) {
        lv_label_set_text(s_title_label, "nextUp");
        return;
    }

    char title_text[48];
    snprintf(title_text, sizeof(title_text), "nextUp %s", WiFi.localIP().toString().c_str());
    lv_label_set_text(s_title_label, title_text);
}

void collapse_title() {
    s_title_expanded = false;
    if (s_title_timer) {
        lv_timer_del(s_title_timer);
        s_title_timer = nullptr;
    }
    update_title_label();
}

static void title_timer_cb(lv_timer_t *) {
    collapse_title();
}

static void title_click_cb(lv_event_t *) {
    if (s_title_expanded) {
        collapse_title();
        return;
    }

    if (WiFi.status() != WL_CONNECTED) {
        collapse_title();
        return;
    }

    s_title_expanded = true;
    update_title_label();
    if (s_title_timer) {
        lv_timer_reset(s_title_timer);
    } else {
        s_title_timer = lv_timer_create(title_timer_cb, 15000, nullptr);
        lv_timer_set_repeat_count(s_title_timer, 1);
    }
}

// ── UI build ───────────────────────────────────────────────────────────────

void create_card_shell(bool offline) {
    lv_obj_t *screen = lv_scr_act();
    lv_obj_clean(screen);
    lv_obj_set_style_bg_color(screen, COLOR_BG, 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);

    // Full-bleed: s_card is just the screen itself (no border/radius)
    s_card = lv_obj_create(screen);
    lv_obj_set_size(s_card, SCREEN_W, SCREEN_H);
    lv_obj_align(s_card, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_radius(s_card, 0, 0);
    lv_obj_set_style_bg_color(s_card, COLOR_BG, 0);
    lv_obj_set_style_bg_opa(s_card, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_card, 0, 0);
    lv_obj_set_style_shadow_width(s_card, 0, 0);
    lv_obj_set_style_pad_all(s_card, 0, 0);
    lv_obj_clear_flag(s_card, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *header = lv_obj_create(s_card);
    lv_obj_set_size(header, lv_pct(100), HEADER_H);
    lv_obj_align(header, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_radius(header, 0, 0);
    lv_obj_set_style_bg_opa(header, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(header, 0, 0);
    lv_obj_set_style_pad_left(header, BODY_PAD, 0);
    lv_obj_set_style_pad_right(header, BODY_PAD, 0);
    lv_obj_set_style_pad_top(header, 10, 0);
    lv_obj_set_style_pad_bottom(header, 10, 0);
    lv_obj_set_layout(header, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(header, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(header, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    s_title_label = create_label(header, &font_inter_semibold_14, COLOR_AZURE, LV_OPA_COVER, "nextUp");
    lv_obj_set_style_text_letter_space(s_title_label, 1, 0);
    lv_label_set_long_mode(s_title_label, LV_LABEL_LONG_DOT);
    lv_obj_set_width(s_title_label, 220);
    lv_obj_add_flag(s_title_label, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_title_label, title_click_cb, LV_EVENT_CLICKED, nullptr);

    lv_obj_t *right = lv_obj_create(header);
    lv_obj_remove_style_all(right);
    lv_obj_clear_flag(right, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(right, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_pad_all(right, 0, 0);
    lv_obj_set_style_pad_column(right, 12, 0);
    lv_obj_set_layout(right, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(right, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(right, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    s_wifi_status_label = create_label(right, &font_jetbrainsmono_medium_12, COLOR_TEXT_2, LV_OPA_COVER, "");
    s_mqtt_status_label = create_label(right, &font_jetbrainsmono_medium_12, COLOR_TEXT_2, LV_OPA_COVER, "");
    s_offline_label = create_label(right, &font_jetbrainsmono_medium_12, COLOR_ALERT, LV_OPA_COVER, offline ? "offline" : "");
    if (!offline) lv_obj_add_flag(s_offline_label, LV_OBJ_FLAG_HIDDEN);

}

void add_empty_state(lv_obj_t *parent, const char *title, const char *subtitle) {
    (void)subtitle;
    lv_obj_t *lbl = create_label(parent, &font_inter_regular_14, COLOR_TEXT_3, LV_OPA_COVER, title);
    lv_obj_set_style_pad_top(lbl, 2, 0);
    lv_obj_set_style_pad_bottom(lbl, 6, 0);
}

void add_all_day_row(lv_obj_t *parent, const AgendaEntry &entry) {
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_layout(row, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_bg_color(row, source_color(entry.source), 0);
    lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_radius(row, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_pad_left(row, 12, 0);
    lv_obj_set_style_pad_right(row, 12, 0);
    lv_obj_set_style_pad_top(row, 4, 0);
    lv_obj_set_style_pad_bottom(row, 4, 0);

    lv_obj_t *lbl = create_label(row, &font_inter_semibold_14, lv_color_hex(0x000000), LV_OPA_COVER, entry.title);
    lv_label_set_long_mode(lbl, LV_LABEL_LONG_DOT);
    lv_obj_set_width(lbl, 270);
}

void collapse_expanded_row() {
    if (!s_expanded_row) return;
    if (lv_obj_get_child_cnt(s_expanded_row) >= 2) {
        lv_obj_t *detail = lv_obj_get_child(s_expanded_row, 1);
        lv_obj_add_flag(detail, LV_OBJ_FLAG_HIDDEN);
        lv_obj_invalidate(s_expanded_row);
    }
    s_expanded_row = nullptr;
    if (s_expand_timer) { lv_timer_del(s_expand_timer); s_expand_timer = nullptr; }
}

static void expand_timer_cb(lv_timer_t *) {
    collapse_expanded_row();
    s_expand_timer = nullptr;
}

static void row_click_cb(lv_event_t *e) {
    lv_obj_t *row = (lv_obj_t *)lv_event_get_user_data(e);
    if (s_return_timer) lv_timer_reset(s_return_timer);

    if (s_expanded_row == row) { collapse_expanded_row(); return; }
    collapse_expanded_row();

    // row is the column wrapper: child 0 = line, child 1 = detail
    uint32_t n = lv_obj_get_child_cnt(row);
    if (n < 2) return;
    lv_obj_t *detail = lv_obj_get_child(row, 1);
    lv_obj_clear_flag(detail, LV_OBJ_FLAG_HIDDEN);
    lv_obj_invalidate(row);
    s_expanded_row = row;
    s_expand_timer = lv_timer_create(expand_timer_cb, 15000, nullptr);
    lv_timer_set_repeat_count(s_expand_timer, 1);
}

lv_obj_t *add_timed_event_row(lv_obj_t *parent, const AgendaEntry &entry) {
    // Outer wrapper: column so the tap-expanded detail sits below the main row
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_set_size(row, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_pad_all(row, 0, 0);
    lv_obj_set_style_pad_row(row, 4, 0);
    lv_obj_set_layout(row, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_COLUMN);

    // Inner line: time | bar | content
    lv_obj_t *line = lv_obj_create(row);
    lv_obj_set_size(line, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_clear_flag(line, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_opa(line, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(line, 0, 0);
    lv_obj_set_style_pad_all(line, 0, 0);
    lv_obj_set_style_pad_column(line, 10, 0);
    lv_obj_set_layout(line, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(line, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(line, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

    lv_obj_t *time_lbl = create_label(line, &font_inter_semibold_14, COLOR_TEXT_1, LV_OPA_COVER, entry.time_start);
    lv_obj_set_width(time_lbl, 52);

    lv_obj_t *bar = lv_obj_create(line);
    lv_obj_remove_style_all(bar);
    lv_obj_set_size(bar, 3, 32);
    lv_obj_clear_flag(bar, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_radius(bar, 2, 0);
    lv_obj_set_style_bg_color(bar, source_color(entry.source), 0);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);

    lv_obj_t *content = lv_obj_create(line);
    lv_obj_set_flex_grow(content, 1);
    lv_obj_set_height(content, LV_SIZE_CONTENT);
    lv_obj_clear_flag(content, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_opa(content, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(content, 0, 0);
    lv_obj_set_style_pad_all(content, 0, 0);
    lv_obj_set_style_pad_row(content, 2, 0);
    lv_obj_set_layout(content, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(content, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(content, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

    lv_obj_t *title_lbl = create_label(content, &font_inter_semibold_14, COLOR_TEXT_1, LV_OPA_COVER, entry.title);
    lv_label_set_long_mode(title_lbl, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(title_lbl, lv_pct(100));

    if (entry.calendar_name[0]) {
        lv_obj_t *cal_lbl = create_label(content, &font_inter_regular_12, COLOR_TEXT_3, LV_OPA_COVER, entry.calendar_name);
        lv_label_set_long_mode(cal_lbl, LV_LABEL_LONG_DOT);
        lv_obj_set_width(cal_lbl, lv_pct(100));
    }

    // Hidden detail shown on tap — sits below the line in the column wrapper
    lv_obj_t *detail = lv_obj_create(row);
    lv_obj_set_size(detail, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_clear_flag(detail, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_opa(detail, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(detail, 0, 0);
    lv_obj_set_style_pad_all(detail, 0, 0);
    lv_obj_set_style_pad_left(detail, 58, 0); // align with content (48 time + 10 gap)
    lv_obj_set_style_pad_top(detail, 2, 0);
    lv_obj_set_style_pad_row(detail, 2, 0);
    lv_obj_set_layout(detail, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(detail, LV_FLEX_FLOW_COLUMN);
    lv_obj_add_flag(detail, LV_OBJ_FLAG_HIDDEN);

    if (entry.time_start[0] && entry.time_end[0]) {
        char range[16];
        snprintf(range, sizeof(range), "%s \xe2\x80\x93 %s", entry.time_start, entry.time_end);
        lv_obj_t *tl = create_label(detail, &font_inter_semibold_14, COLOR_CYAN, LV_OPA_COVER, range);
        lv_label_set_long_mode(tl, LV_LABEL_LONG_DOT);
        lv_obj_set_width(tl, lv_pct(100));
    }
    if (entry.location[0]) {
        lv_obj_t *ll = create_label(detail, &font_inter_semibold_14, COLOR_CYAN, LV_OPA_COVER, entry.location);
        lv_label_set_long_mode(ll, LV_LABEL_LONG_DOT);
        lv_obj_set_width(ll, lv_pct(100));
    }
    if (entry.calendar_name[0]) {
        lv_obj_t *cl = create_label(detail, &font_inter_semibold_14, COLOR_CYAN, LV_OPA_COVER, entry.calendar_name);
        lv_label_set_long_mode(cl, LV_LABEL_LONG_DOT);
        lv_obj_set_width(cl, lv_pct(100));
    }

    lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(row, row_click_cb, LV_EVENT_CLICKED, row);
    return row;
}

lv_obj_t *add_day_section(lv_obj_t *parent, const char *heading,
                           time_t day_start, const CalEvent &agenda) {
    lv_obj_t *section = lv_obj_create(parent);
    lv_obj_set_size(section, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_clear_flag(section, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(section, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(section, 0, 0);
    lv_obj_set_style_pad_all(section, 0, 0);
    lv_obj_set_style_pad_row(section, 10, 0);
    lv_obj_set_layout(section, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(section, LV_FLEX_FLOW_COLUMN);

    lv_obj_t *hdr = lv_obj_create(section);
    lv_obj_set_size(hdr, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_clear_flag(hdr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(hdr, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(hdr, 0, 0);
    lv_obj_set_style_pad_all(hdr, 0, 0);
    lv_obj_set_layout(hdr, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(hdr, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(hdr, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(hdr, 10, 0);

    // "TODAY" / "TOMORROW" etc. in accent color
    create_label(hdr, &font_inter_semibold_14, COLOR_ACCENT, LV_OPA_COVER, heading);
    // "Tue 21 Apr" date in primary text color
    char date_cap[24];
    format_day_caption(day_start, date_cap, sizeof(date_cap));
    create_label(hdr, &font_inter_regular_14, COLOR_TEXT_1, LV_OPA_COVER, date_cap);

    const time_t day_end = day_start + 86400;
    bool has_content = false;

    lv_obj_t *chip_row = lv_obj_create(section);
    lv_obj_remove_style_all(chip_row);
    lv_obj_set_size(chip_row, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_clear_flag(chip_row, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_pad_all(chip_row, 0, 0);
    lv_obj_set_style_pad_column(chip_row, 8, 0);
    lv_obj_set_style_pad_row(chip_row, 6, 0);
    lv_obj_set_layout(chip_row, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(chip_row, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(chip_row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

    for (uint8_t i = 0; i < agenda.count; ++i) {
        const AgendaEntry &e = agenda.items[i];
        if (e.is_all_day && event_overlaps_day(e, day_start, day_end)) {
            add_all_day_row(chip_row, e);
            has_content = true;
        }
    }
    if (!lv_obj_get_child_cnt(chip_row)) lv_obj_add_flag(chip_row, LV_OBJ_FLAG_HIDDEN);

    for (uint8_t i = 0; i < agenda.count; ++i) {
        const AgendaEntry &e = agenda.items[i];
        if (!e.is_all_day && event_overlaps_day(e, day_start, day_end)) {
            lv_obj_t *row = add_timed_event_row(section, e);
            if (s_event_row_count < CAL_MAX_EVENTS) s_event_row_objs[s_event_row_count++] = row;
            has_content = true;
        }
    }
    if (!has_content) add_empty_state(section, "No events", "Nothing scheduled for this day");
    return section;
}

void update_sticky_day_label() {
    if (!s_sticky_day_label || !s_body || s_day_anchor_count == 0) return;
    const lv_coord_t scroll_y = lv_obj_get_scroll_y(s_body);
    const char *cur = s_day_anchors[0].label;
    for (uint8_t i = 0; i < s_day_anchor_count; ++i) {
        if (s_day_anchors[i].top_y <= scroll_y) cur = s_day_anchors[i].label;
    }
    lv_label_set_text(s_sticky_day_label, cur);
}

static void body_scroll_begin_cb(lv_event_t *) {
    if (s_return_timer) lv_timer_reset(s_return_timer);
}

lv_coord_t compute_return_target_y() {
    if (!s_body) return 0;
    const int32_t now_ts = (int32_t)::now();
    for (uint8_t i = 0; i < s_event_row_count; ++i) {
        const AgendaEntry &entry = s_cached_ev.items[i];
        if (entry.ts_end_local > now_ts && s_event_row_objs[i]) {
            lv_obj_t *parent = lv_obj_get_parent(s_event_row_objs[i]);
            return lv_obj_get_y(s_event_row_objs[i]) + (parent ? lv_obj_get_y(parent) : 0);
        }
    }
    if (s_day_anchor_count > 1) return s_day_anchors[1].top_y;
    return 0;
}

static void return_timer_cb(lv_timer_t *) {
    collapse_expanded_row();
    lv_coord_t target = compute_return_target_y();
    lv_obj_scroll_to_y(s_body, target, LV_ANIM_ON);
    update_sticky_day_label();
}

void update_live_labels(bool offline) {
    if (WiFi.status() != WL_CONNECTED && s_title_expanded) {
        collapse_title();
    }
    update_title_label();
    if (s_background_clock) {
        if (s_wifi_connected) {
            char tt[6];
            format_hhmm(tt, sizeof(tt));
            set_text(s_background_clock, tt);
            lv_obj_clear_flag(s_background_clock, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(s_background_clock, LV_OBJ_FLAG_HIDDEN);
        }
    }
    if (s_wifi_status_label) {
        lv_label_set_text(s_wifi_status_label, "wifi down");
        lv_obj_set_style_text_color(s_wifi_status_label, COLOR_ALERT, 0);
        if (s_wifi_connected) {
            lv_obj_add_flag(s_wifi_status_label, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_clear_flag(s_wifi_status_label, LV_OBJ_FLAG_HIDDEN);
        }
    }
    if (s_mqtt_status_label) {
        lv_label_set_text(s_mqtt_status_label, "mqtt down");
        lv_obj_set_style_text_color(s_mqtt_status_label, COLOR_ALERT, 0);
        if (s_mqtt_connected) {
            lv_obj_add_flag(s_mqtt_status_label, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_clear_flag(s_mqtt_status_label, LV_OBJ_FLAG_HIDDEN);
        }
    }
    if (s_offline_label) {
        if (offline) {
            lv_obj_clear_flag(s_offline_label, LV_OBJ_FLAG_HIDDEN);
            lv_label_set_text(s_offline_label, "offline");
        } else {
            lv_obj_add_flag(s_offline_label, LV_OBJ_FLAG_HIDDEN);
        }
    }
    update_sticky_day_label();
}

void build_setup_or_connecting(const char *title, const char *line1, const char *line2) {
    create_card_shell(false);

    lv_obj_t *content = lv_obj_create(s_card);
    lv_obj_set_size(content, lv_pct(100), SCREEN_H - CARD_INSET * 2 - HEADER_H - 1);
    lv_obj_align(content, LV_ALIGN_TOP_MID, 0, HEADER_H + 1);
    lv_obj_set_style_bg_opa(content, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(content, 0, 0);
    lv_obj_set_style_pad_left(content, 28, 0);
    lv_obj_set_style_pad_right(content, 28, 0);
    lv_obj_set_style_pad_top(content, 36, 0);
    lv_obj_set_style_pad_bottom(content, 28, 0);
    lv_obj_set_layout(content, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(content, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(content, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(content, 12, 0);

    lv_obj_t *headline = create_label(content, &font_inter_display_bold_108, COLOR_TEXT_1, static_cast<lv_opa_t>(16), "86");
    lv_obj_set_style_text_align(headline, LV_TEXT_ALIGN_CENTER, 0);

    lv_obj_t *card = lv_obj_create(content);
    lv_obj_set_width(card, lv_pct(100));
    lv_obj_set_style_bg_color(card, COLOR_SURFACE_ALT, 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(card, 0, 0);
    lv_obj_set_style_radius(card, 22, 0);
    lv_obj_set_style_pad_all(card, 24, 0);
    lv_obj_set_style_pad_row(card, 10, 0);
    lv_obj_set_layout(card, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);

    if (title && title[0]) {
        create_label(card, &font_inter_semibold_14, COLOR_ACCENT, LV_OPA_COVER, title);
    }
    create_label(card, &font_inter_regular_14,  COLOR_TEXT_1, LV_OPA_COVER, line1);
    create_label(card, &font_inter_regular_14, COLOR_TEXT_1, LV_OPA_COVER, line2);

    update_live_labels(false);
}

}  // namespace

static void flush_lvgl() {
    for (int i = 0; i < 10; i++) {
        lv_timer_handler();
        delay(5);
    }
}

// ── Public API ─────────────────────────────────────────────────────────────

bool display_ready() {
    return s_display_ready;
}

void display_set_connection_status(bool wifi_connected, bool mqtt_connected) {
    s_wifi_connected = wifi_connected;
    s_mqtt_connected = mqtt_connected;
    update_live_labels(false);
}

void display_init() {
    Serial.println("display_init: start");
    Serial.printf("SRAM free: %d  PSRAM free: %d\n",
                  heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                  heap_caps_get_free_size(MALLOC_CAP_SPIRAM));

    // 1. Touch first (matches working reference)
    Wire.begin(TOUCH_SDA, TOUCH_SCL);
    s_ts.begin();
    s_ts.setRotation(ROTATION_NORMAL);

    // 2. Display panel
    s_gfx->begin(11000000);
    s_gfx->fillScreen(RGB565_BLACK);

    // 3. LVGL init (backlight stays OFF until after first flush below)
    lv_init();

    uint32_t screenWidth  = s_gfx->width();
    uint32_t screenHeight = s_gfx->height();

    lv_color_t *buf = (lv_color_t *)heap_caps_malloc(
        sizeof(lv_color_t) * screenWidth * screenHeight,
        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);

    if (!buf) {
        Serial.println("display_init: LVGL draw buffer alloc failed!");
        return;
    }

    lv_disp_draw_buf_init(&s_draw_buf, buf, nullptr, screenWidth * screenHeight);

    lv_disp_drv_init(&s_disp_drv);
    s_disp_drv.hor_res  = screenWidth;
    s_disp_drv.ver_res  = screenHeight;
    s_disp_drv.flush_cb = lvgl_flush_cb;
    s_disp_drv.draw_buf = &s_draw_buf;
    lv_disp_drv_register(&s_disp_drv);

    lv_obj_set_style_bg_color(lv_scr_act(), lv_color_hex(0x000000), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(lv_scr_act(), LV_OPA_COVER, LV_PART_MAIN);
    lv_timer_handler();

    // Backlight ON at full brightness
    ledcAttach(GFX_BL, 5000, 8);
    ledcWrite(GFX_BL, 255);

    static lv_indev_drv_t indev_drv;
    lv_indev_drv_init(&indev_drv);
    indev_drv.type         = LV_INDEV_TYPE_POINTER;
    indev_drv.read_cb      = lvgl_touch_cb;
    indev_drv.scroll_throw = 20;  // higher = more friction, snappier on slow MCU
    indev_drv.scroll_limit = 20;  // require 20px move before scroll starts (reduces accidental scroll on tap)
    lv_indev_drv_register(&indev_drv);

    s_display_ready = true;
    Serial.println("display_init: done");
    Serial.printf("SRAM free: %d  PSRAM free: %d\n",
                  heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                  heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
}

void display_show_setup() {
    s_mode = ScreenMode::Setup;
    build_setup_or_connecting("Setup Mode", "Join WiFi AP: CalendarSetup", "Open http://192.168.4.1");
    flush_lvgl();
}

void display_show_connecting(const char *ssid) {
    s_mode = ScreenMode::Connecting;
    char line1[80];
    if (ssid && ssid[0])
        snprintf(line1, sizeof(line1), "Connecting to %s", ssid);
    else
        snprintf(line1, sizeof(line1), "Connecting to WiFi");
    build_setup_or_connecting(nullptr, line1, "Calendar data will appear after first refresh");
    flush_lvgl();
}

void display_render(const CalEvent &ev, bool offline) {
    cleanup_timers();
    s_cached_ev = ev;
    s_mode = ScreenMode::Agenda;
    create_card_shell(offline);

    s_body = lv_obj_create(s_card);
    lv_obj_set_size(s_body, lv_pct(100), SCREEN_H - CARD_INSET * 2 - HEADER_H - 1);
    lv_obj_align(s_body, LV_ALIGN_TOP_MID, 0, HEADER_H + 1);
    lv_obj_set_style_bg_opa(s_body, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_body, 0, 0);
    lv_obj_set_style_pad_left(s_body, BODY_PAD, 0);
    lv_obj_set_style_pad_right(s_body, BODY_PAD, 0);
    lv_obj_set_style_pad_top(s_body, 20, 0);
    lv_obj_set_style_pad_bottom(s_body, BODY_PAD, 0);
    lv_obj_set_style_pad_row(s_body, 6, 0);
    lv_obj_set_layout(s_body, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(s_body, LV_FLEX_FLOW_COLUMN);
    lv_obj_add_flag(s_body, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(s_body, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(s_body, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_set_style_width(s_body, 4, LV_PART_SCROLLBAR);
    lv_obj_set_style_bg_color(s_body, lv_color_hex(0x2a3040), LV_PART_SCROLLBAR);
    lv_obj_set_style_bg_opa(s_body, LV_OPA_COVER, LV_PART_SCROLLBAR);
    lv_obj_add_event_cb(s_body, body_scroll_begin_cb, LV_EVENT_SCROLL_BEGIN, nullptr);

    s_sticky_day_label = lv_label_create(s_card);
    lv_obj_add_flag(s_sticky_day_label, LV_OBJ_FLAG_HIDDEN);

    const time_t today = local_midnight(::now());

    struct SectionRef { lv_obj_t *obj; const char *label; };
    SectionRef section_refs[CAL_MAX_DAYS];
    uint8_t section_ref_count = 0;

    auto record_section = [&](lv_obj_t *sec, const char *lbl) {
        if (section_ref_count < CAL_MAX_DAYS) section_refs[section_ref_count++] = {sec, lbl};
    };

    static const char *DAY_LABELS[] = {
        "TODAY", "TOMORROW", "IN 2 DAYS", "IN 3 DAYS",
        "IN 4 DAYS", "IN 5 DAYS", "IN 6 DAYS"
    };
    for (uint8_t d = 0; d < 7; ++d)
        record_section(add_day_section(s_body, DAY_LABELS[d], today + d * 86400, ev), DAY_LABELS[d]);

    if (!ev.has_event)
        add_empty_state(s_body, "Nothing queued", "The calendar feed returned no events for the next 7 days");

    lv_obj_update_layout(s_body);

    s_day_anchor_count = 0;
    for (uint8_t i = 0; i < section_ref_count; ++i) {
        if (s_day_anchor_count >= CAL_MAX_DAYS) break;
        DayAnchor &a = s_day_anchors[s_day_anchor_count++];
        a.top_y = lv_obj_get_y(section_refs[i].obj);
        snprintf(a.label, sizeof(a.label), "%s", section_refs[i].label);
    }

    update_live_labels(offline);

    s_return_timer = lv_timer_create(return_timer_cb, 45000, nullptr);
    lv_timer_set_repeat_count(s_return_timer, -1);

    // Large clock overlay — bottom-right corner, above all content.
    s_background_clock = create_label(s_card, &font_inter_display_bold_108, COLOR_CLOCK, BACKGROUND_CLOCK_OPA, "--:--");
    lv_obj_add_flag(s_background_clock, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_clear_flag(s_background_clock, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_text_align(s_background_clock, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_width(s_background_clock, SCREEN_W - (BODY_PAD * 2));
    lv_obj_align(s_background_clock, LV_ALIGN_BOTTOM_RIGHT, -BODY_PAD, -BODY_PAD);
    lv_obj_move_foreground(s_background_clock);
    update_live_labels(offline);

    flush_lvgl();
}

void display_update_clock(bool offline) {
    update_live_labels(offline);
}

void display_breathe(const CalEvent &ev, bool offline) {
    (void)ev;
    display_update_clock(offline);
}

void display_set_brightness(uint8_t level) {
    ledcWrite(GFX_BL, level);
}
