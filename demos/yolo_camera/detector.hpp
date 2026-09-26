#pragma once
#include <opencv2/core.hpp>
#include "yolo26_det.h"
#include <array>
#include <string>

namespace demo {
struct Timing { double preprocess_ms = 0, inference_ms = 0, postprocess_ms = 0; };
class Detector {
public:
    explicit Detector(const std::string& model);
    ~Detector();
    Detector(const Detector&) = delete;
    Detector& operator=(const Detector&) = delete;
    object_detect_result_list run(const cv::Mat& bgr, float threshold, Timing& timing);
private:
    rknn_app_context_t context_{};
    rknn_tensor_attr input_{};
    std::array<rknn_tensor_attr, 3> outputs_{};
    cv::Mat rgb_, resized_, padded_;
};
}
