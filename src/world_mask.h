// Auto-generated equirectangular land mask (Natural Earth 110m, 0.5deg).
#pragma once
#include <stdint.h>

namespace world_mask {
static constexpr int   W    = 720;
static constexpr int   H    = 360;
static constexpr float STEP = 0.5f;
extern const uint8_t DATA[32400];
// land at (row,col): DATA[idx>>3] >> (7-(idx&7)) & 1, idx=row*W+col
}  // namespace world_mask
