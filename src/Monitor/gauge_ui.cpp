#include "gauge_ui.hpp"
#include <Arduino.h>   // for millis()
#include "tpms_data.hpp"  // we'll create this next, for latestPressure/Temps

// Global instances defined here
std::vector<GaugeUI> gauges;
lv_obj_t *led_status   = nullptr;
lv_obj_t *status_label = nullptr;

// Private helpers
const lv_font_t* choose_font_by_height(int target_px) {
    const lv_font_t *best = LV_FONT_DEFAULT;
    int best_px = 0;

    #if LV_FONT_MONTSERRAT_36
      if (36 <= target_px && 36 > best_px) { best = &lv_font_montserrat_36; best_px = 36; }
    #endif
    #if LV_FONT_MONTSERRAT_32
      if (32 <= target_px && 32 > best_px) { best = &lv_font_montserrat_32; best_px = 32; }
    #endif
    #if LV_FONT_MONTSERRAT_28
      if (28 <= target_px && 28 > best_px) { best = &lv_font_montserrat_28; best_px = 28; }
    #endif
    #if LV_FONT_MONTSERRAT_24
      if (24 <= target_px && 24 > best_px) { best = &lv_font_montserrat_24; best_px = 24; }
    #endif
    #if LV_FONT_MONTSERRAT_22
      if (22 <= target_px && 22 > best_px) { best = &lv_font_montserrat_22; best_px = 22; }
    #endif
    #if LV_FONT_MONTSERRAT_20
      if (20 <= target_px && 20 > best_px) { best = &lv_font_montserrat_20; best_px = 20; }
    #endif
    #if LV_FONT_MONTSERRAT_18
      if (18 <= target_px && 18 > best_px) { best = &lv_font_montserrat_18; best_px = 18; }
    #endif
    #if LV_FONT_MONTSERRAT_16
      if (16 <= target_px && 16 > best_px) { best = &lv_font_montserrat_16; best_px = 16; }
    #endif
    #if LV_FONT_MONTSERRAT_14
      if (14 <= target_px && 14 > best_px) { best = &lv_font_montserrat_14; best_px = 14; }
    #endif
    #if LV_FONT_MONTSERRAT_12
      if (12 <= target_px && 12 > best_px) { best = &lv_font_montserrat_12; best_px = 12; }
    #endif

    return best;
}

static void tick_label_cb(lv_event_t * e) {
    lv_obj_draw_part_dsc_t * dsc = lv_event_get_draw_part_dsc(e);
    if (!dsc) return;

    if(dsc->type != LV_METER_DRAW_PART_TICK) return;
    if(dsc->part != LV_PART_TICKS) return;
    if(!dsc->label_dsc || !dsc->text) return;

    int32_t raw = dsc->value;
    int32_t mapped = raw / 100000;
    dsc->value = mapped;
    lv_snprintf(dsc->text, sizeof(dsc->text), "%d", dsc->value);
}

GaugeUI create_pressure_gauge(lv_obj_t* parent, int size_px)
{
    GaugeUI g{};

    // --- Meter ---
    g.meter = lv_meter_create(parent);
    lv_obj_set_size(g.meter, size_px, size_px);
    lv_obj_align(g.meter, LV_ALIGN_TOP_MID, 0, 0);   // <--- move meter to top

    // (rest of your meter setup unchanged)
    lv_obj_set_style_bg_color(g.meter, lv_color_make(0x20,0x20,0x20), 0);
    lv_obj_set_style_bg_opa(g.meter, LV_OPA_COVER, 0);
    g.scale = lv_meter_add_scale(g.meter);
    lv_meter_set_scale_range(g.meter, g.scale, 100000, 300000, 240, 150);
    lv_obj_set_style_text_color(g.meter, lv_color_white(), LV_PART_TICKS);
    lv_obj_set_style_text_opa(g.meter, LV_OPA_COVER, LV_PART_TICKS);
    lv_meter_set_scale_ticks(g.meter, g.scale, 21, 2, 10,
        lv_palette_darken(LV_PALETTE_GREY, 2));
    lv_meter_set_scale_major_ticks(g.meter, g.scale, 10, 4, 20,
        lv_color_white(), 14);
    lv_obj_add_event_cb(g.meter, tick_label_cb, LV_EVENT_DRAW_PART_BEGIN, NULL);
    g.needle = lv_meter_add_needle_line(g.meter, g.scale, 10,
        lv_palette_main(LV_PALETTE_RED), -14);

    g.label_temp = lv_label_create(parent);
    lv_obj_set_style_text_color(g.label_temp, lv_color_black(), 0);
    const lv_font_t* temp_font = choose_font_by_height(size_px / 4);
    lv_obj_set_style_text_font(g.label_temp, temp_font, 0);
    lv_label_set_text(g.label_temp, "-- °C");
    lv_obj_align_to(g.label_temp, g.meter, LV_ALIGN_OUT_BOTTOM_MID, 0, 10);

    g.label_age = lv_label_create(parent);
    lv_obj_set_style_text_color(g.label_age, lv_color_black(), 0);
    const lv_font_t* age_font = choose_font_by_height(size_px / 8);
    lv_obj_set_style_text_font(g.label_age, age_font, 0);
    lv_label_set_text(g.label_age, "");
    lv_obj_align_to(g.label_age, g.meter, LV_ALIGN_OUT_BOTTOM_MID, -30, 50);

    g.label_period = lv_label_create(parent);
    lv_obj_set_style_text_color(g.label_period, lv_color_black(), 0);
    lv_obj_set_style_text_font(g.label_period, age_font, 0);
    lv_label_set_text(g.label_period, "");
    lv_obj_align_to(g.label_period, g.meter, LV_ALIGN_OUT_BOTTOM_MID, 30, 50);

    g.label_pressure = lv_label_create(g.meter);
    lv_obj_set_style_text_color(g.label_pressure, lv_color_white(), 0);
    const lv_font_t* pressure_font = choose_font_by_height(size_px / 4);
    lv_obj_set_style_text_font(g.label_pressure, pressure_font, 0);
    lv_label_set_text(g.label_pressure, "-- KPa");
    //lv_obj_align(g.label_pressure, LV_ALIGN_BOTTOM_MID, 0, -10);
    lv_obj_align(g.label_pressure, LV_ALIGN_BOTTOM_MID, 0, -8);

    return g;
}

// This is your anim_all_cb, just moved here and renamed private
static void anim_all_cb(lv_timer_t *t)
{
    if (gauges.empty()) return;

    using namespace tpms;

    if (hasData) {
        for (int gi = 0; gi < 4; gi++) {
            GaugeUI &g0 = gauges[gi];

            lv_meter_set_indicator_value(g0.meter, g0.needle, latestPressure[gi]);
            float pressure_kpa = latestPressure[gi] / 100000.0f;

            char buf_pressure[16];
            snprintf(buf_pressure, sizeof(buf_pressure), "%.2f", pressure_kpa);
            lv_label_set_text(g0.label_pressure, buf_pressure);

            float temp_c = latestTemp[gi] / 100.0f;
            char buf_temp[16];
            snprintf(buf_temp, sizeof(buf_temp), "%.1f °C", temp_c);
            lv_label_set_text(g0.label_temp, buf_temp);
        }
    }

    uint32_t now = millis();
    uint32_t elapsed_sec = (now - last_update) / 1000;

    char statusBuf[32];
    snprintf(statusBuf, sizeof(statusBuf), "%u", elapsed_sec);
    lv_label_set_text(status_label, statusBuf);

    if (elapsed_sec < 10) {
        lv_led_set_color(led_status, lv_palette_main(LV_PALETTE_GREEN));
    } else if (elapsed_sec < 30) {
        lv_led_set_color(led_status, lv_palette_main(LV_PALETTE_YELLOW));
    } else {
        lv_led_set_color(led_status, lv_palette_main(LV_PALETTE_RED));
    }

    for (int gi = 0; gi < 4; gi++) {
        GaugeUI &g0 = gauges[gi];

        uint32_t age_sec = (now - lastUpdated[gi]) / 1000;
        char ageBuf[32];
        snprintf(ageBuf, sizeof(ageBuf), "%u", age_sec);
        lv_label_set_text(g0.label_age, ageBuf);

        uint32_t period_sec = (now - start_time) / ((totalPeriods[gi] + 1) * 1000);
        char periodBuf[32];
        snprintf(periodBuf, sizeof(periodBuf), "%u", period_sec);
        lv_label_set_text(g0.label_period, periodBuf);
    }
}

void init_gauge_timer()
{
    // one place to create the timer
    lv_timer_create(anim_all_cb, 100, nullptr);
}
