#pragma once
#include <lvgl.h>
#include <vector>

// Encapsulates one gauge's LVGL widgets/indicators
struct GaugeUI {
    lv_obj_t* meter = nullptr;
    lv_meter_scale_t* scale = nullptr;
    lv_meter_indicator_t* needle = nullptr;
    lv_obj_t* label_temp = nullptr;
    lv_obj_t* label_pressure = nullptr;
    lv_obj_t* label_age = nullptr;
    lv_obj_t* label_period = nullptr;
};

GaugeUI create_pressure_gauge(lv_obj_t* parent, int size_px);

extern const lv_font_t* choose_font_by_height(int target_px);

extern std::vector<GaugeUI> gauges;

void init_gauge_timer();

extern lv_obj_t *led_status;
extern lv_obj_t *status_label;