#include "detector.hpp"
#include "core.hpp"
#include <opencv2/imgproc.hpp>
#include <chrono>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <vector>

namespace demo {
namespace {
using Clock = std::chrono::steady_clock;
double ms(Clock::time_point t) { return std::chrono::duration<double, std::milli>(Clock::now() - t).count(); }
void check(int code, const char* operation) {
    if (code < 0) throw std::runtime_error(std::string(operation) + " failed: " + std::to_string(code));
}
struct OutputLease {
    rknn_context ctx;
    std::array<rknn_output, 3> buffers{};
    bool acquired = false;
    ~OutputLease() { if (acquired) rknn_outputs_release(ctx, buffers.size(), buffers.data()); }
};
}
Detector::Detector(const std::string& model) {
    try {
        std::ifstream file(model, std::ios::binary | std::ios::ate);
        if (!file) throw std::runtime_error("Cannot open model: " + model);
        const auto length = file.tellg();
        if (length <= 0 || static_cast<uint64_t>(length) > std::numeric_limits<uint32_t>::max())
            throw std::runtime_error("Invalid model file size");
        std::vector<char> data(static_cast<size_t>(length));
        file.seekg(0);
        if (!file.read(data.data(), data.size())) throw std::runtime_error("Cannot read complete model");
        check(rknn_init(&context_.rknn_ctx, data.data(), data.size(), 0, nullptr), "rknn_init (check Runtime version)");
        rknn_sdk_version version{};
        check(rknn_query(context_.rknn_ctx, RKNN_QUERY_SDK_VERSION, &version, sizeof(version)), "Query SDK version");
        std::cout << "Runtime=" << version.api_version << " driver=" << version.drv_version << '\n';
        check(rknn_query(context_.rknn_ctx, RKNN_QUERY_IN_OUT_NUM, &context_.io_num, sizeof(context_.io_num)), "Query I/O count");
        if (context_.io_num.n_input != 1 || context_.io_num.n_output != 3)
            throw std::runtime_error("Expected vendor YOLO26 detection model with 1 input and 3 outputs");
        check(rknn_query(context_.rknn_ctx, RKNN_QUERY_INPUT_ATTR, &input_, sizeof(input_)), "Query input");
        if (input_.n_dims != 4 || input_.dims[0] != 1 ||
            (input_.fmt != RKNN_TENSOR_NHWC && input_.fmt != RKNN_TENSOR_NCHW))
            throw std::runtime_error("Expected batch=1 RGB input");
        const bool nhwc = input_.fmt == RKNN_TENSOR_NHWC;
        context_.model_height = input_.dims[nhwc ? 1 : 2];
        context_.model_width = input_.dims[nhwc ? 2 : 3];
        context_.model_channel = input_.dims[nhwc ? 3 : 1];
        if (context_.model_channel != 3 || context_.model_width != 640 || context_.model_height != 640)
            throw std::runtime_error("This demo expects the tested 640x640 RGB model");
        for (unsigned i = 0; i < outputs_.size(); ++i) {
            auto& a = outputs_[i]; a.index = i;
            check(rknn_query(context_.rknn_ctx, RKNN_QUERY_OUTPUT_ATTR, &a, sizeof(a)), "Query output");
            if (a.fmt != RKNN_TENSOR_NCHW || a.n_dims != 4 || a.dims[0] != 1 || a.dims[1] != 84 ||
                a.dims[2] != (80u >> i) || a.dims[3] != (80u >> i))
                throw std::runtime_error("Expected outputs [1,84,80,80], [1,84,40,40], [1,84,20,20]");
            const bool quant = a.type == RKNN_TENSOR_INT8 && a.qnt_type == RKNN_TENSOR_QNT_AFFINE_ASYMMETRIC;
            if (i == 0) context_.is_quant = quant;
            if (quant != context_.is_quant || (quant && (!std::isfinite(a.scale) || a.scale <= 0)))
                throw std::runtime_error("Mixed/invalid output quantization is unsupported");
            std::cout << "Output " << i << ": [1,84," << a.dims[2] << ',' << a.dims[3]
                      << "] type=" << int(a.type) << " scale=" << a.scale << " zp=" << a.zp << '\n';
        }
        context_.input_attrs = &input_;
        context_.output_attrs = outputs_.data();
    } catch (...) {
        if (context_.rknn_ctx) rknn_destroy(context_.rknn_ctx);
        context_.rknn_ctx = 0;
        throw;
    }
}
Detector::~Detector() { if (context_.rknn_ctx) rknn_destroy(context_.rknn_ctx); }
object_detect_result_list Detector::run(const cv::Mat& bgr, float threshold, Timing& timing) {
    const auto pre = Clock::now();
    const auto box = letterbox(bgr.cols, bgr.rows, context_.model_width, context_.model_height);
    cv::cvtColor(bgr, rgb_, cv::COLOR_BGR2RGB);
    cv::resize(rgb_, resized_, cv::Size(box.width, box.height), 0, 0, cv::INTER_LINEAR);
    padded_.create(context_.model_height, context_.model_width, CV_8UC3);
    padded_.setTo(cv::Scalar(114, 114, 114));
    resized_.copyTo(padded_(cv::Rect(box.left, box.top, box.width, box.height)));
    rknn_input input{};
    input.index = 0; input.type = RKNN_TENSOR_UINT8; input.fmt = RKNN_TENSOR_NHWC;
    // pass_through=0: Runtime handles the input conversion and embedded /255 normalization.
    input.pass_through = 0; input.size = padded_.total() * padded_.elemSize(); input.buf = padded_.data;
    check(rknn_inputs_set(context_.rknn_ctx, 1, &input), "rknn_inputs_set");
    timing.preprocess_ms = ms(pre);
    const auto infer = Clock::now();
    check(rknn_run(context_.rknn_ctx, nullptr), "rknn_run");
    timing.inference_ms = ms(infer);
    const auto post = Clock::now();
    OutputLease output{context_.rknn_ctx};
    for (unsigned i = 0; i < output.buffers.size(); ++i) {
        output.buffers[i].index = i;
        output.buffers[i].want_float = !context_.is_quant;
    }
    check(rknn_outputs_get(context_.rknn_ctx, output.buffers.size(), output.buffers.data(), nullptr), "rknn_outputs_get");
    output.acquired = true;
    letterbox_t mapping{box.left, box.top, box.scale};
    object_detect_result_list results{};
    check(post_process(&context_, output.buffers.data(), &mapping, threshold, NMS_THRESH, &results), "post_process");
    // Vendor decoder clamps in model space; clip once more to the original image.
    for (int i = 0; i < results.count; ++i) {
        auto& b = results.results[i].box;
        b.left = std::clamp(b.left, 0, bgr.cols - 1); b.right = std::clamp(b.right, 0, bgr.cols - 1);
        b.top = std::clamp(b.top, 0, bgr.rows - 1); b.bottom = std::clamp(b.bottom, 0, bgr.rows - 1);
    }
    timing.postprocess_ms = ms(post);
    return results;
}
}
