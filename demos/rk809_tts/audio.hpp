#pragma once
#include <string>
#include <vector>

namespace speech {
constexpr int sample_rate = 44100;
// 使用 ALSA 的 aplay，避免为独立 demo 引入额外开发包。
// posix_spawn 直接传 argv，不经过 shell；失败必须向调用者报告。
void run(const std::vector<std::string>& args);
void configure_speaker();
void save_wav(const std::string& path, const std::vector<float>& samples);
void play(const std::string& path);
std::vector<float> test_tone();
}
