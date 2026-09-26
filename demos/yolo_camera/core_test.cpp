#include "core.hpp"
#include <iostream>

int main() {
    int failures = 0;
    auto check = [&](bool ok, const char* what) { if (!ok) { std::cerr << what << '\n'; ++failures; } };
    auto black = demo::yuvToBgr(0, 128, 128, true, false);
    auto white = demo::yuvToBgr(255, 128, 128, true, false);
    check(black.r == 0 && black.g == 0 && black.b == 0, "full-range black");
    check(white.r == 255 && white.g == 255 && white.b == 255, "full-range white");
    black = demo::yuvToBgr(16, 128, 128, false, false);
    white = demo::yuvToBgr(235, 128, 128, false, true);
    check(black.r == 0 && white.b == 255, "limited-range endpoints");
    auto red = demo::yuvToBgr(76, 85, 255, true, false);
    check(red.r >= 250 && red.b <= 2 && red.g <= 2, "NV12 U/V and BGR order");
    auto a = demo::letterbox(640, 480, 640, 640);
    check(a.width == 640 && a.height == 480 && a.left == 0 && a.top == 80 && a.scale == 1, "landscape padding");
    auto b = demo::letterbox(1632, 1224, 640, 640);
    check(b.width == 640 && b.height == 480 && b.top == 80, "sensor aspect ratio");
    auto c = demo::letterbox(480, 640, 640, 640);
    check(c.left == 80 && c.top == 0, "portrait padding");
    bool rejected = false;
    try { (void)demo::letterbox(0, 480, 640, 640); } catch (...) { rejected = true; }
    check(rejected, "reject invalid dimensions");
    if (!failures) std::cout << "Color range and letterbox checks passed\n";
    return failures ? 1 : 0;
}
