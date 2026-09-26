#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <stdexcept>

namespace demo {
struct Bgr { uint8_t b, g, r; };
// OpenCV's NV12 conversion assumes limited range; this camera advertises full range.
inline Bgr yuvToBgr(int y, int u, int v, bool full, bool bt709) {
    const float kr = bt709 ? 0.2126f : 0.299f;
    const float kb = bt709 ? 0.0722f : 0.114f;
    const float kg = 1.f - kr - kb;
    const float yy = full ? float(y) : (y - 16.f) * (255.f / 219.f);
    const float uu = (u - 128.f) * (full ? 1.f : 255.f / 224.f);
    const float vv = (v - 128.f) * (full ? 1.f : 255.f / 224.f);
    auto byte = [](float n) { return static_cast<uint8_t>(std::clamp(std::lround(n), 0L, 255L)); };
    return {byte(yy + 2.f * (1.f - kb) * uu),
            byte(yy - 2.f * kb * (1.f - kb) / kg * uu - 2.f * kr * (1.f - kr) / kg * vv),
            byte(yy + 2.f * (1.f - kr) * vv)};
}
struct Letterbox {
    float scale;
    int width, height, left, top;
};
inline Letterbox letterbox(int w, int h, int mw, int mh) {
    if (w <= 0 || h <= 0 || mw <= 0 || mh <= 0) throw std::runtime_error("Invalid image dimensions");
    const float scale = std::min(float(mw) / w, float(mh) / h);
    const int rw = std::clamp(int(std::lround(w * scale)), 1, mw);
    const int rh = std::clamp(int(std::lround(h * scale)), 1, mh);
    return {scale, rw, rh, (mw - rw) / 2, (mh - rh) / 2};
}
}
