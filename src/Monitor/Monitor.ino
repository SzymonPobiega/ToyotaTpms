/**
 * 4x Car-style pressure gauges (0–? range) in a 2×2 grid.
 * First gauge is driven by values from TPMS-Gateway.
 */

#include <Arduino.h>
#include <vector>
#include <esp_display_panel.hpp>
#include <lvgl.h>
#include <WiFi.h>
#include <esp_mac.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include "gauge_ui.hpp"
#include "tpms_data.hpp"
//#include "display.hpp"

using namespace esp_panel::board;
using namespace esp_panel::drivers;

#if LV_COLOR_DEPTH != 16
#error "Set LV_COLOR_DEPTH to 16 (RGB565) in lv_conf.h"
#endif

Board *board = nullptr;
LCD   *lcd   = nullptr;

// ---- LVGL glue ----
static lv_disp_draw_buf_t draw_buf;
static lv_color_t *buf1 = nullptr;
static lv_color_t *buf2 = nullptr;
static int lcd_w = 0, lcd_h = 0;
static uint32_t last_update = millis();
static uint32_t start_time = millis();

static void my_disp_flush(lv_disp_drv_t *disp, const lv_area_t *area, lv_color_t *color_p)
{
    int32_t x = area->x1;
    int32_t y = area->y1;
    int32_t w = area->x2 - area->x1 + 1;
    int32_t h = area->y2 - area->y1 + 1;
    if (w <= 0 || h <= 0) { lv_disp_flush_ready(disp); return; }

    lcd->drawBitmap(x, y, w, h, reinterpret_cast<const uint8_t*>(color_p));
    lv_disp_flush_ready(disp);
}

static void tick_label_cb(lv_event_t * e)
{
    lv_obj_draw_part_dsc_t * dsc = lv_event_get_draw_part_dsc(e);
    if(dsc == NULL) return;

    /* Only care about meter ticks */
    if(dsc->type != LV_METER_DRAW_PART_TICK) return;
    if(dsc->part != LV_PART_TICKS) return;

    /* Minor ticks have no label_dsc/text */
    if(dsc->label_dsc == NULL || dsc->text == NULL) return;

    int32_t raw = dsc->value;
    int32_t mapped = raw / 100000;

    dsc->value = mapped;

    lv_snprintf(dsc->text, sizeof(dsc->text), "%d", dsc->value);
}

void setup()
{
    Serial0.begin(115200);
    Serial0.println("Initializing board with default config");
    board = new Board();
    assert(board->begin());

    lcd = board->getLCD();
    if (!lcd) {
        Serial0.println("LCD is not available");
        while (true) { delay(1000); }
    }

    if (auto bl = board->getBacklight()) { 
        bl->on();
        bl->setBrightness(50);
    }

    lcd_w = lcd->getFrameWidth();
    lcd_h = lcd->getFrameHeight();
    Serial0.printf("LCD: %dx%d\n", lcd_w, lcd_h);

    // ---- LVGL init ----
    lv_init();

    // Two line-buffers (40 lines each)
    const int lines = 40;
    size_t buf_pixels = lcd_w * lines;
    buf1 = (lv_color_t *)heap_caps_malloc(buf_pixels * sizeof(lv_color_t), MALLOC_CAP_8BIT);
    buf2 = (lv_color_t *)heap_caps_malloc(buf_pixels * sizeof(lv_color_t), MALLOC_CAP_8BIT);
    assert(buf1 && buf2);

    lv_disp_draw_buf_init(&draw_buf, buf1, buf2, buf_pixels);

    static lv_disp_drv_t disp_drv;
    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res  = lcd_w;
    disp_drv.ver_res  = lcd_h;
    disp_drv.flush_cb = my_disp_flush;
    disp_drv.draw_buf = &draw_buf;

    disp_drv.sw_rotate = 1;
    disp_drv.rotated   = LV_DISP_ROT_90;

    lv_disp_drv_register(&disp_drv);

    // Get the (now valid) default display & its active screen
    lv_disp_t *disp = lv_disp_get_default();
    lv_obj_t  *scr  = lv_disp_get_scr_act(disp);

    int disp_w = lv_disp_get_hor_res(disp);
    int disp_h = lv_disp_get_ver_res(disp);

    // Screen background
    lv_obj_set_style_bg_color(scr, lv_color_make(155, 155, 155), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

    // ---- 2×2 grid container (4 gauges) ----
    lv_obj_t* cont = lv_obj_create(lv_scr_act());
    lv_obj_set_size(cont, disp_w, disp_h);
    lv_obj_set_style_bg_opa(cont, LV_OPA_TRANSP, 0); // transparent container

    static lv_coord_t col_dsc[] = {
        LV_GRID_FR(1), LV_GRID_FR(1),
        LV_GRID_TEMPLATE_LAST
    };
    static lv_coord_t row_dsc[] = {
        LV_GRID_FR(4),   // Row 0: top gauges
        LV_GRID_FR(4),   // Row 1: bottom gauges
        LV_GRID_FR(2),   // Row 2: controls
        LV_GRID_TEMPLATE_LAST
    };
    lv_obj_set_grid_dsc_array(cont, col_dsc, row_dsc);

    int cell_w = disp_w / 2;
    int cell_h = disp_h / 3;
    int s = (cell_w < cell_h ? cell_w : cell_h) * 9 / 10;

    // Create 4 gauges and place them in the grid
    gauges.clear();
    gauges.reserve(4);
    for (int r = 0; r < 2; ++r) {
        for (int c = 0; c < 2; ++c) {

            // One container per cell
            lv_obj_t *cell = lv_obj_create(cont);
            lv_obj_set_style_bg_opa(cell, LV_OPA_TRANSP, 0);  // transparent
            lv_obj_remove_style_all(cell);
            lv_obj_set_size(cell, LV_SIZE_CONTENT, LV_SIZE_CONTENT);

            lv_obj_set_grid_cell(
                cell,
                LV_GRID_ALIGN_CENTER, c, 1,
                LV_GRID_ALIGN_CENTER, r, 1);

            GaugeUI g = create_pressure_gauge(cell, s);
            
            gauges.push_back(g);
        }
    }

    lv_obj_t *cell_led = lv_obj_create(cont);
    lv_obj_remove_style_all(cell_led);
    lv_obj_set_style_bg_opa(cell_led, LV_OPA_TRANSP, 0);
    lv_obj_set_size(cell_led, LV_SIZE_CONTENT, LV_SIZE_CONTENT);

    lv_obj_set_grid_cell(
        cell_led,
        LV_GRID_ALIGN_CENTER, 0, 1,   // column 0, span 1 column
        LV_GRID_ALIGN_CENTER, 2, 1);  // row 2, span 1 row

    // LED widget (or just a colored circle)
    led_status = lv_led_create(cell_led);
    lv_obj_set_size(led_status, 30, 30);
    lv_obj_center(led_status);

    lv_led_set_color(led_status, lv_palette_main(LV_PALETTE_YELLOW));

    lv_obj_t *cell_lbl = lv_obj_create(cont);
    lv_obj_remove_style_all(cell_lbl);
    lv_obj_set_style_bg_opa(cell_lbl, LV_OPA_TRANSP, 0);
    lv_obj_set_size(cell_lbl, LV_SIZE_CONTENT, LV_SIZE_CONTENT);

    lv_obj_set_grid_cell(
        cell_lbl,
        LV_GRID_ALIGN_CENTER, 1, 1,   // column 1
        LV_GRID_ALIGN_CENTER, 2, 1);  // row 2

    status_label = lv_label_create(cell_lbl);
    const lv_font_t* status_font = choose_font_by_height(s / 4);
    lv_obj_set_style_text_font(status_label, status_font, 0);
    lv_label_set_text(status_label, "Status: ---");
    lv_obj_center(status_label);

    init_gauge_timer();

    using namespace tpms;

    if (!initEspNow()) {
        lv_led_set_color(led_status, lv_palette_main(LV_PALETTE_RED));
    } else {
        lv_led_set_color(led_status, lv_palette_main(LV_PALETTE_YELLOW));
    }
}

void loop()
{
    static uint32_t last = millis();
    uint32_t now = millis();
    lv_tick_inc(now - last);
    last = now;

    lv_timer_handler();

    delay(1000);
}
