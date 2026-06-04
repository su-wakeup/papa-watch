#include "fs_image.h"
#include <LittleFS.h>
#include <esp_heap_caps.h>
#include <string.h>

namespace fs_image {

lv_image_dsc_t* load_rgb565(const char* path, int w, int h) {
    File f = LittleFS.open(path, "r");
    if (!f) {                              // LittleFS may not be mounted yet
        LittleFS.begin(false);
        f = LittleFS.open(path, "r");
        if (!f) return nullptr;
    }
    const size_t sz = (size_t)w * h * 2;
    if ((size_t)f.size() < sz) { f.close(); return nullptr; }

    uint8_t* buf = (uint8_t*)heap_caps_malloc(sz, MALLOC_CAP_SPIRAM);
    if (!buf) { f.close(); return nullptr; }
    size_t rd = f.read(buf, sz);
    f.close();
    if (rd != sz) { heap_caps_free(buf); return nullptr; }

    lv_image_dsc_t* d = (lv_image_dsc_t*)heap_caps_malloc(sizeof(lv_image_dsc_t), MALLOC_CAP_SPIRAM);
    if (!d) { heap_caps_free(buf); return nullptr; }
    memset(d, 0, sizeof(*d));
    d->header.magic  = LV_IMAGE_HEADER_MAGIC;
    d->header.cf     = LV_COLOR_FORMAT_RGB565;
    d->header.w      = w;
    d->header.h      = h;
    d->header.stride = w * 2;
    d->data          = buf;
    d->data_size     = sz;
    return d;
}

void free(lv_image_dsc_t* img) {
    if (!img) return;
    if (img->data) heap_caps_free((void*)img->data);
    heap_caps_free(img);
}

}  // namespace fs_image
