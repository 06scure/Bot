#pragma once

// 修改这里的配置后，重新执行 bash build.sh。不再解析命令行参数。
namespace demo::config {
inline constexpr char model_path[] = "model/yolo26n-rk3568-i8.rknn";
inline constexpr char camera_device[] = "/dev/video0";
inline constexpr int capture_width = 640;
inline constexpr int capture_height = 480;
inline constexpr float confidence_threshold = 0.25f;
inline constexpr bool person_only = false;  // 只过滤显示结果，不减少 NPU 计算。

inline constexpr char window_title[] = "LubanCat YOLO26 - q/Esc to quit";
inline constexpr int window_width = 960;
inline constexpr int window_height = 720;
}
