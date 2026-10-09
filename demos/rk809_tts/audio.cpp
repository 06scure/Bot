#include "audio.hpp"
#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <spawn.h>
#include <stdexcept>
#include <sys/wait.h>
#include <signal.h>
#include <chrono>
#include <thread>
extern char** environ;

namespace speech {
void run(const std::vector<std::string>& args) {
    std::vector<char*> argv;
    for (const auto& s : args) argv.push_back(const_cast<char*>(s.c_str()));
    argv.push_back(nullptr);
    pid_t pid{};
    const int result = posix_spawnp(&pid, argv[0], nullptr, nullptr, argv.data(), environ);
    if (result) throw std::runtime_error(args.front() + " spawn error=" + std::to_string(result));
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
    int status{};
    for (;;) {
        const auto waited = waitpid(pid, &status, WNOHANG);
        if (waited == pid) break;
        if (waited < 0 && errno != EINTR) throw std::runtime_error("waitpid failed");
        if (std::chrono::steady_clock::now() >= deadline) {
            kill(pid, SIGKILL);
            while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {}
            throw std::runtime_error(args.front() + " timeout");
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
        throw std::runtime_error(args.front() + " failed, status=" + std::to_string(status));
}
void configure_speaker() {
    run({"amixer", "-c", "rockchiprk809co", "sset", "Playback Path", "SPK"});
    // 起始音量 70%，测试波形另做幅度限制。
    run({"amixer", "-c", "rockchiprk809co", "sset", "Playback", "70%"});
}
void save_wav(const std::string& path, const std::vector<float>& samples) {
    if (samples.empty() || samples.size() > sample_rate * 15)
        throw std::runtime_error("audio length out of range");
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    auto put = [&](std::uint32_t v, int bytes) {
        for (int i=0; i<bytes; ++i) out.put(static_cast<char>((v >> (8*i)) & 255));
    };
    out.write("RIFF",4); put(36+samples.size()*2,4); out.write("WAVEfmt ",8);
    put(16,4); put(1,2); put(1,2); put(sample_rate,4); put(sample_rate*2,4);
    put(2,2); put(16,2); out.write("data",4); put(samples.size()*2,4);
    for (float v : samples) {
        if (!std::isfinite(v)) throw std::runtime_error("nonfinite audio");
        const auto s = static_cast<std::int16_t>(std::clamp(v,-1.f,1.f)*32767);
        put(static_cast<std::uint16_t>(s),2);
    }
    out.close();
    if (!out) throw std::runtime_error("cannot write WAV: " + path);
}
void play(const std::string& path) {
    run({"aplay", "-D", "plughw:CARD=rockchiprk809co,DEV=0", path});
}
std::vector<float> test_tone() {
    std::vector<float> result(sample_rate*2);
    for (std::size_t i=0;i<result.size();++i) {
        const double t = static_cast<double>(i)/sample_rate;
        const double envelope = std::min({1., t/.05, (2.-t)/.05});
        result[i] = .12 * envelope * std::sin(2*3.141592653589793*440*t);
    }
    return result;
}
}
