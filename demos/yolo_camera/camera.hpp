#pragma once
#include <opencv2/core.hpp>
#include <csignal>
#include <cstddef>
#include <string>
#include <vector>

namespace demo {
struct Frame {
    cv::Mat bgr;
    double capture_ms = 0, color_ms = 0, age_ms = -1;
    unsigned dropped = 0;
};
class Camera {
public:
    Camera(const std::string& device, int width, int height);
    ~Camera();
    Camera(const Camera&) = delete;
    Camera& operator=(const Camera&) = delete;
    bool read(Frame& frame, const volatile std::sig_atomic_t& stop);
private:
    struct Mapping { void* ptr; size_t size; };
    void close() noexcept;
    void queue(unsigned index);
    int fd_ = -1;
    bool streaming_ = false;
    unsigned width_ = 0, height_ = 0, stride_ = 0;
    size_t checked_bgr_step_ = 0;
    std::vector<Mapping> maps_;
};
}
