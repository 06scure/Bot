#include "camera.hpp"
#include "time.hpp"
#include <linux/videodev2.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <poll.h>
#include <fcntl.h>
#include <unistd.h>
#include <cerrno>
#include <array>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <im2d.hpp>

namespace demo {
namespace {
int call(int fd, unsigned long op, void* arg) {
    int rc;
    do { rc = ioctl(fd, op, arg); } while (rc < 0 && errno == EINTR);
    return rc;
}
void require(int rc, const char* what) {
    if (rc < 0) throw std::runtime_error(std::string(what) + ": " + std::strerror(errno));
}
using time::Clock;
using time::elapsedMs;
}
Camera::Camera(const std::string& device, int width, int height) {
    try {
        fd_ = ::open(device.c_str(), O_RDWR | O_NONBLOCK | O_CLOEXEC);
        require(fd_, "Open camera (close Cheese first)");
        v4l2_capability cap{};
        require(call(fd_, VIDIOC_QUERYCAP, &cap), "VIDIOC_QUERYCAP");
        const auto caps = cap.capabilities & V4L2_CAP_DEVICE_CAPS ? cap.device_caps : cap.capabilities;
        if (!(caps & V4L2_CAP_VIDEO_CAPTURE_MPLANE) || !(caps & V4L2_CAP_STREAMING))
            throw std::runtime_error("Camera must support CAPTURE_MPLANE and STREAMING");
        v4l2_format fmt{};
        fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
        fmt.fmt.pix_mp.width = width;
        fmt.fmt.pix_mp.height = height;
        fmt.fmt.pix_mp.pixelformat = V4L2_PIX_FMT_NV12;
        fmt.fmt.pix_mp.field = V4L2_FIELD_NONE;
        require(call(fd_, VIDIOC_S_FMT, &fmt), "VIDIOC_S_FMT NV12");
        const auto& p = fmt.fmt.pix_mp;
        if (p.pixelformat != V4L2_PIX_FMT_NV12 || p.num_planes != 1)
            throw std::runtime_error("Driver did not accept contiguous, one-plane NV12 (NM12 is not supported)");
        width_ = p.width; height_ = p.height; stride_ = p.plane_fmt[0].bytesperline;
        if (!width_ || !height_ || width_ % 2 || height_ % 2 || stride_ < width_ || width_ > 8192 || height_ > 8192)
            throw std::runtime_error("Unsupported NV12 dimensions/stride");
        auto enc = p.ycbcr_enc;
        if (enc == V4L2_YCBCR_ENC_DEFAULT) enc = V4L2_MAP_YCBCR_ENC_DEFAULT(p.colorspace);
        auto quant = p.quantization;
        if (quant == V4L2_QUANTIZATION_DEFAULT)
            quant = V4L2_MAP_QUANTIZATION_DEFAULT(false, p.colorspace, enc);
        if (enc != V4L2_YCBCR_ENC_601 || quant != V4L2_QUANTIZATION_FULL_RANGE)
            throw std::runtime_error("Expected BT.601 full-range NV12");
        std::cout << "Camera: " << width_ << 'x' << height_ << " NV12 stride=" << stride_
                  << " range=full matrix=601 (sensor frame rate)\n";
        v4l2_requestbuffers req{};
        req.count = 4; req.type = fmt.type; req.memory = V4L2_MEMORY_MMAP;
        require(call(fd_, VIDIOC_REQBUFS, &req), "VIDIOC_REQBUFS");
        if (req.count < 2 || req.count > 32) throw std::runtime_error("Unexpected V4L2 buffer count");
        maps_.reserve(req.count);
        for (unsigned i = 0; i < req.count; ++i) {
            v4l2_plane plane{};
            v4l2_buffer b{};
            b.type = fmt.type; b.memory = V4L2_MEMORY_MMAP; b.index = i; b.length = 1; b.m.planes = &plane;
            require(call(fd_, VIDIOC_QUERYBUF, &b), "VIDIOC_QUERYBUF");
            void* ptr = mmap(nullptr, plane.length, PROT_READ | PROT_WRITE, MAP_SHARED, fd_, plane.m.mem_offset);
            if (ptr == MAP_FAILED) require(-1, "mmap camera");
            maps_.push_back({ptr, plane.length});
            queue(i);
        }
        auto type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
        require(call(fd_, VIDIOC_STREAMON, &type), "VIDIOC_STREAMON");
        streaming_ = true;
    } catch (...) { close(); throw; }
}
Camera::~Camera() { close(); }
void Camera::close() noexcept {
    if (streaming_) { auto type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE; call(fd_, VIDIOC_STREAMOFF, &type); }
    for (auto& m : maps_) munmap(m.ptr, m.size);
    maps_.clear();
    if (fd_ >= 0) ::close(fd_);
    fd_ = -1; streaming_ = false;
}
void Camera::queue(unsigned index) {
    v4l2_plane p{};
    p.length = maps_.at(index).size;
    v4l2_buffer b{};
    b.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE; b.memory = V4L2_MEMORY_MMAP;
    b.index = index; b.length = 1; b.m.planes = &p;
    require(call(fd_, VIDIOC_QBUF, &b), "VIDIOC_QBUF");
}
bool Camera::read(Frame& frame, const volatile std::sig_atomic_t& stop) {
    const auto start = Clock::now();
    frame.capture_ms = 0; frame.color_ms = 0;
    frame.dropped = 0; frame.age_ms = -1;
    // Hold dequeued buffers until draining finishes: the queue cannot refill forever.
    std::array<unsigned, 32> held{};  // 构造时已检查驱动最多返回 32 个缓冲。
    size_t held_count = 0;
    v4l2_buffer latest{};
    v4l2_plane latest_plane{};

    while (!held_count && !stop) {
        pollfd pfd{fd_, POLLIN, 0};
        const int ready = poll(&pfd, 1, 200);
        if (ready < 0) { if (errno == EINTR) continue; require(ready, "poll camera"); }
        if (pfd.revents & (POLLERR | POLLHUP | POLLNVAL)) throw std::runtime_error("Camera disconnected or poll error");
        if (!ready) {
            if (elapsedMs(start) > 3000) throw std::runtime_error("No camera frame for 3 seconds; check Cheese/ISP/device");
            continue;
        }
        for (size_t n = 0; n < maps_.size(); ++n) {
            v4l2_plane p{};
            v4l2_buffer b{};
            b.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE; b.memory = V4L2_MEMORY_MMAP;
            b.length = 1; b.m.planes = &p;
            const int rc = call(fd_, VIDIOC_DQBUF, &b);
            if (rc < 0 && errno == EAGAIN) break;
            require(rc, "VIDIOC_DQBUF");
            if (b.index >= maps_.size()) throw std::runtime_error("Invalid camera buffer index");
            held[held_count++] = b.index; latest = b; latest_plane = p;
        }
    }
    if (!held_count) return false;
    if (latest.flags & V4L2_BUF_FLAG_ERROR) throw std::runtime_error("Camera flagged corrupt frame");
    const auto& mem = maps_.at(latest.index);
    const size_t need = size_t(stride_) * height_ * 3 / 2;
    if (latest_plane.data_offset > mem.size || need > mem.size - latest_plane.data_offset ||
        latest_plane.bytesused > mem.size || latest_plane.bytesused < latest_plane.data_offset + need)
        throw std::runtime_error("Short NV12 frame: bytesused/offset/stride disagree");
    frame.capture_ms = elapsedMs(start);
    const auto color_start = Clock::now();
    frame.bgr.create(height_, width_, CV_8UC3);

    auto* nv12 = static_cast<uint8_t*>(mem.ptr)
        + latest_plane.data_offset;
    const auto src = wrapbuffer_virtualaddr_t(
        nv12,
        width_,
        height_,
        stride_,height_,
        RK_FORMAT_YCbCr_420_SP
    );
    const auto dst = wrapbuffer_virtualaddr_t(
        frame.bgr.data,
        frame.bgr.cols,
        frame.bgr.rows,
        static_cast<int>(frame.bgr.step / frame.bgr.elemSize()),
        frame.bgr.rows,
        RK_FORMAT_BGR_888
    );
    if (checked_bgr_step_ != frame.bgr.step) {
        const IM_STATUS validation = imcheck(src, dst, {}, {});
        if (validation != IM_STATUS_NOERROR)
            throw std::runtime_error(std::string("RGA parameter check failed: ") + imStrError(validation));
        checked_bgr_step_ = frame.bgr.step;
    }

    const IM_STATUS status = imcvtcolor(
        src,
        dst,
        RK_FORMAT_YCbCr_420_SP,
        RK_FORMAT_BGR_888,
        IM_YUV_TO_RGB_BT601_FULL
    );

    if (status != IM_STATUS_SUCCESS) {
        throw std::runtime_error(
            std::string("RGA color conversion failed: ")
            + imStrError(status)
        );
    }
    frame.color_ms = elapsedMs(color_start);
    if ((latest.flags & V4L2_BUF_FLAG_TIMESTAMP_MASK) == V4L2_BUF_FLAG_TIMESTAMP_MONOTONIC) {
        frame.age_ms = time::frameAgeMs(latest.timestamp.tv_sec, latest.timestamp.tv_usec);
    }
    frame.dropped = held_count - 1;
    for (size_t i = 0; i < held_count; ++i) queue(held[i]);
    return true;
}
}
