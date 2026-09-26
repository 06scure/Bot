#include "camera.hpp"
#include "detector.hpp"
#include <opencv2/highgui.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <csignal>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <memory>
#include <sstream>
#include <stdexcept>

namespace {
volatile std::sig_atomic_t stopped = 0;
void signalStop(int) { stopped = 1; }
using Clock = std::chrono::steady_clock;
double ms(Clock::time_point t) { return std::chrono::duration<double, std::milli>(Clock::now() - t).count(); }
struct Options {
    std::string model, device = "/dev/video0", image, save;
    int width = 640, height = 480, fps = 30, frames = 0;
    float conf = 0.25f;
    bool headless = false, person = false;
};
void help() {
    std::cout << "yolo_camera_demo --model MODEL [--device /dev/video0] [--width 640 --height 480]\n"
              << "  [--fps 30] [--conf 0.25] [--person-only] [--headless] [--frames N]\n"
              << "  [--save last-frame.jpg] [--image bus.jpg]\n"
              << "q/Esc or Ctrl+C exits. --image runs one still-image check. --frames 0 runs continuously.\n";
}
Options parse(int argc, char** argv) {
    Options o;
    for (int i = 1; i < argc; ++i) {
        const std::string key = argv[i];
        auto value = [&]() -> std::string { if (++i >= argc) throw std::runtime_error("Missing value for " + key); return argv[i]; };
        auto integer = [&]() { const auto v = value(); size_t used = 0; int n = std::stoi(v, &used); if (used != v.size()) throw std::runtime_error("Invalid number: " + v); return n; };
        if (key == "--model") o.model = value();
        else if (key == "--device") o.device = value();
        else if (key == "--image") o.image = value();
        else if (key == "--save") o.save = value();
        else if (key == "--width") o.width = integer();
        else if (key == "--height") o.height = integer();
        else if (key == "--fps") o.fps = integer();
        else if (key == "--frames") o.frames = integer();
        else if (key == "--conf") { const auto v = value(); size_t used = 0; o.conf = std::stof(v, &used); if (used != v.size()) throw std::runtime_error("Invalid confidence"); }
        else if (key == "--headless") o.headless = true;
        else if (key == "--person-only") o.person = true;
        else throw std::runtime_error("Unknown option: " + key);
    }
    if (o.model.empty()) throw std::runtime_error("--model is required");
    if (o.width < 32 || o.height < 16 || o.width > 8192 || o.height > 8192 || o.width % 2 || o.height % 2 ||
        o.fps < 1 || o.fps > 120 || o.frames < 0 || !std::isfinite(o.conf) || o.conf <= 0 || o.conf >= 1)
        throw std::runtime_error("Invalid dimensions, FPS, frame limit or confidence");
    return o;
}
const char* names[] = {
    "person","bicycle","car","motorcycle","airplane","bus","train","truck","boat","traffic light",
    "fire hydrant","stop sign","parking meter","bench","bird","cat","dog","horse","sheep","cow",
    "elephant","bear","zebra","giraffe","backpack","umbrella","handbag","tie","suitcase","frisbee",
    "skis","snowboard","sports ball","kite","baseball bat","baseball glove","skateboard","surfboard","tennis racket","bottle",
    "wine glass","cup","fork","knife","spoon","bowl","banana","apple","sandwich","orange",
    "broccoli","carrot","hot dog","pizza","donut","cake","chair","couch","potted plant","bed",
    "dining table","toilet","tv","laptop","mouse","remote","keyboard","cell phone","microwave","oven",
    "toaster","sink","refrigerator","book","clock","vase","scissors","teddy bear","hair drier","toothbrush"
};
int draw(cv::Mat& img, const object_detect_result_list& results, bool person, bool print) {
    int count = 0;
    for (int i = 0; i < results.count; ++i) {
        const auto& d = results.results[i];
        if (d.cls_id < 0 || d.cls_id >= 80 || (person && d.cls_id != 0)) continue;
        if (d.box.right <= d.box.left || d.box.bottom <= d.box.top) continue;
        ++count;
        cv::rectangle(img, {d.box.left, d.box.top}, {d.box.right, d.box.bottom}, cv::Scalar(0,255,0), 2);
        std::ostringstream label; label << names[d.cls_id] << ' ' << std::fixed << std::setprecision(2) << d.prop;
        cv::putText(img, label.str(), {d.box.left, std::max(16, d.box.top - 5)}, cv::FONT_HERSHEY_SIMPLEX, 0.5, cv::Scalar(0,255,0), 1);
        if (print) std::cout << label.str() << " box=" << d.box.left << ',' << d.box.top << ',' << d.box.right << ',' << d.box.bottom << '\n';
    }
    return count;
}
}
int main(int argc, char** argv) {
    std::cout.setf(std::ios::unitbuf);
    if (argc == 2 && std::string(argv[1]) == "--help") { help(); return 0; }
    try {
        const auto o = parse(argc, argv);
        std::signal(SIGINT, signalStop); std::signal(SIGTERM, signalStop);
        if (!o.headless && !std::getenv("DISPLAY")) throw std::runtime_error("DISPLAY is missing. Use run.sh or --headless");
        cv::setNumThreads(2);
        demo::Detector detector(o.model);
        std::unique_ptr<demo::Camera> camera;
        cv::Mat still;
        if (o.image.empty()) camera = std::make_unique<demo::Camera>(o.device, o.width, o.height, o.fps);
        else {
            still = cv::imread(o.image);
            if (still.empty()) throw std::runtime_error("Cannot read image: " + o.image);
        }
        const char* window = "LubanCat YOLO26 - q/Esc to quit";
        if (!o.headless) { cv::namedWindow(window, cv::WINDOW_NORMAL); cv::resizeWindow(window, 960, 720); }
        auto log_start = Clock::now();
        int total = 0, interval_frames = 0;
        unsigned dropped_total = 0;
        double previous_fps = 0;
        cv::Mat last;
        while (!stopped && (!o.frames || total < o.frames)) {
            const auto loop_start = Clock::now();
            demo::Frame frame;
            if (camera) { if (!camera->read(frame, stopped)) break; }
            else frame.bgr = still.clone();
            const auto after_capture = Clock::now();
            demo::Timing t;
            const auto detections = detector.run(frame.bgr, o.conf, t);
            const auto display_start = Clock::now();
            const int count = draw(frame.bgr, detections, o.person, !camera);
            const double age = frame.age_ms < 0 ? -1 : frame.age_ms + ms(after_capture);
            std::ostringstream hud;
            hud << std::fixed << std::setprecision(1) << "FPS " << previous_fps << "  NPU " << t.inference_ms
                << "ms  age " << age << "ms  objects " << count;
            cv::putText(frame.bgr, hud.str(), {8,24}, cv::FONT_HERSHEY_SIMPLEX, 0.5, cv::Scalar(0,0,0), 3);
            cv::putText(frame.bgr, hud.str(), {8,24}, cv::FONT_HERSHEY_SIMPLEX, 0.5, cv::Scalar(255,255,255), 1);
            if (!o.headless) {
                cv::imshow(window, frame.bgr);
                const int key = cv::waitKey(1) & 0xff;
                // GTK in OpenCV 4.2 may return -1 for unsupported visibility queries.
                // Do not treat that as a closed window; also allow initial window mapping.
                if (key == 'q' || key == 27 || (total > 2 && cv::getWindowProperty(window, cv::WND_PROP_VISIBLE) == 0)) stopped = 1;
            }
            const double display_ms = ms(display_start);
            last = frame.bgr;
            ++total; ++interval_frames; dropped_total += frame.dropped;
            const double interval = ms(log_start);
            if (interval >= 1000 || !camera || (o.frames && total == o.frames)) {
                previous_fps = interval_frames * 1000.0 / interval;
                std::cout << std::fixed << std::setprecision(1) << "frames=" << total << " fps=" << previous_fps
                    << " capture=" << frame.capture_ms << " color=" << frame.color_ms
                    << " pre=" << t.preprocess_ms << " npu=" << t.inference_ms << " post=" << t.postprocess_ms
                    << " draw_display=" << display_ms << " loop=" << ms(loop_start)
                    << " age_before_display=" << age << "ms dropped=" << dropped_total << " objects=" << count << '\n';
                interval_frames = 0; log_start = Clock::now();
            }
            if (!camera) break;
        }
        if (!o.save.empty() && !last.empty() && !cv::imwrite(o.save, last)) throw std::runtime_error("Cannot save frame: " + o.save);
        if (!o.headless) cv::destroyAllWindows();
        std::cout << "Stopped cleanly after " << total << " frames\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "ERROR: " << e.what() << '\n';
        return 1;
    }
}
