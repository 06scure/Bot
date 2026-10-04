#include "camera.hpp"
#include "config.hpp"
#include "detector.hpp"
#include <opencv2/highgui.hpp>
#include <opencv2/imgproc.hpp>
#include <algorithm>
#include <chrono>
#include <csignal>
#include <iomanip>
#include <iostream>
#include <sstream>

namespace {
volatile std::sig_atomic_t stopped = 0;
void signalStop(int) { stopped = 1; }
using Clock = std::chrono::steady_clock;
double elapsedMs(Clock::time_point start) {
    return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
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

int drawDetections(cv::Mat& image, const object_detect_result_list& detections) {
    int count = 0;
    for (int i = 0; i < detections.count; ++i) {
        const auto& detection = detections.results[i];
        if (detection.cls_id < 0 || detection.cls_id >= 80) continue;
        if (demo::config::person_only && detection.cls_id != 0) continue;
        const auto& box = detection.box;
        if (box.right <= box.left || box.bottom <= box.top) continue;

        ++count;
        cv::rectangle(image, {box.left, box.top}, {box.right, box.bottom}, cv::Scalar(0, 255, 0), 2);
        std::ostringstream label;
        label << names[detection.cls_id] << ' ' << std::fixed << std::setprecision(2) << detection.prop;
        cv::putText(image, label.str(), {box.left, std::max(16, box.top - 5)},
                    cv::FONT_HERSHEY_SIMPLEX, 0.5, cv::Scalar(0, 255, 0), 1);
    }
    return count;
}
}

int main() {
    // SSH 中的 Ctrl+C 或 SIGTERM 只设置退出标志，资源由对象析构释放。
    std::signal(SIGINT, signalStop);
    std::signal(SIGTERM, signalStop);
    std::cout.setf(std::ios::unitbuf);
    try {
        namespace config = demo::config;
        cv::setNumThreads(2);
        demo::Detector detector(config::model_path);
        demo::Camera camera(config::camera_device, config::capture_width,
                            config::capture_height);
        cv::namedWindow(config::window_title, cv::WINDOW_NORMAL);
        cv::resizeWindow(config::window_title, config::window_width, config::window_height);

        auto interval_start = Clock::now();
        int total_frames = 0;
        int interval_frames = 0;
        double fps = 0;
        unsigned dropped_total = 0;

        while (!stopped) {
            // 每次循环只有一条路径：取帧 → 推理 → 画框 → 显示。
            demo::Frame frame;
            if (!camera.read(frame, stopped)) break;
            const auto after_capture = Clock::now();
            demo::Timing timing;
            const auto detections = detector.run(frame.bgr, config::confidence_threshold, timing);
            const int count = drawDetections(frame.bgr, detections);
            const double age = frame.age_ms < 0 ? -1 : frame.age_ms + elapsedMs(after_capture);

            std::ostringstream status;
            const int key = cv::waitKey(1) & 0xff;
            if (key == 'q' || key == 27) {
                stopped = 1;
            }
            cv::imshow(config::window_title, frame.bgr);

            ++total_frames;
            ++interval_frames;
            dropped_total += frame.dropped;
            const double interval_ms = elapsedMs(interval_start);
            if (interval_ms >= 1000) {
                fps = interval_frames * 1000.0 / interval_ms;
                std::cout << std::fixed << std::setprecision(1)
                          << "frames=" << total_frames << " fps=" << fps
                          << " capture=" << frame.capture_ms << " color=" << frame.color_ms
                          << " pre=" << timing.preprocess_ms << " npu=" << timing.inference_ms
                          << " post=" << timing.postprocess_ms << " age=" << age
                          << "ms dropped=" << dropped_total << " objects=" << count << '\n';
                interval_frames = 0;
                interval_start = Clock::now();
            }
        }
        cv::destroyAllWindows();
        std::cout << "Stopped cleanly after " << total_frames << " frames\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "ERROR: " << error.what() << '\n';
        return 1;
    }
}
