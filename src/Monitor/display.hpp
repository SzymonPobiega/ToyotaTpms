// display.hpp
#pragma once
#include <lvgl.h>

bool init_display();           // does lv_init, buffer, driver, rotation, etc.
lv_disp_t* get_display();      // access default display
