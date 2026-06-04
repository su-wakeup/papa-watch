// fs_image — load a raw little-endian RGB565 blob (w*h*2 bytes) from LittleFS
// into PSRAM and wrap it in an lv_image_dsc_t. Lets big opaque backgrounds live
// in the LittleFS partition instead of bloating the app flash. The decoded image
// sits in PSRAM only while its screen is open; free() it on leave().
#pragma once
#include <lvgl.h>

namespace fs_image {

lv_image_dsc_t* load_rgb565(const char* path, int w, int h);  // nullptr on failure
void            free(lv_image_dsc_t* img);

}  // namespace fs_image
