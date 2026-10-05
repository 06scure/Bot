// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "yolo26_det.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <type_traits>

namespace {
// 保持配套实现先 clamp 后截断的坐标与 INT8 阈值语义。
int clampCoordinate(float value, int maximum) {
    return value > 0 ? (value < maximum ? static_cast<int>(value) : maximum) : 0;
}
int8_t quantizeThreshold(float value, int32_t zp, float scale) {
    const float q = value / scale + zp;
    return static_cast<int8_t>(static_cast<int32_t>(std::clamp(q, -128.0f, 127.0f)));
}
template <typename T>
float decodeValue(T value, const rknn_tensor_attr& attr) {
    if constexpr (std::is_same_v<T, int8_t>)
        return (float(value) - float(attr.zp)) * attr.scale;
    else
        return value;
}
template <typename T>
void decodeBranch(const T* data, const rknn_tensor_attr& attr,
                  const rknn_app_context_t& context, const letterbox_t& mapping,
                  float threshold, PostprocessScratch& scratch,
                  object_detect_result_list& results) {
    const int height = attr.dims[2], width = attr.dims[3];
    const int cells = height * width, stride = context.model_height / height;
    std::fill_n(scratch.classes.begin(), cells, std::array<uint64_t, 2>{});
    T cutoff;
    if constexpr (std::is_same_v<T, int8_t>) cutoff = quantizeThreshold(threshold, attr.zp, attr.scale);
    else cutoff = threshold;
    // NCHW 中同类别的所有网格连续，避免每次访问跨越整个平面。
    for (int cls = 0; cls < OBJ_CLASS_NUM; ++cls) {
        const T* scores = data + (4 + cls) * cells;
        const uint64_t bit = uint64_t{1} << (cls % 64);
        for (int cell = 0; cell < cells; ++cell)
            if (scores[cell] >= cutoff) scratch.classes[cell][cls / 64] |= bit;
    }
    // 输出仍按网格、类别顺序，保持原实现截取前 128 个结果的行为。
    for (int cell = 0; cell < cells && results.count < OBJ_NUMB_MAX_SIZE; ++cell) {
        const auto& masks = scratch.classes[cell];
        if (!(masks[0] | masks[1])) continue;
        float loc[4];
        for (int i = 0; i < 4; ++i) loc[i] = decodeValue(data[i * cells + cell], attr);
        const float x = (cell % width + 0.5 - loc[0]) * stride;
        const float y = (cell / width + 0.5 - loc[1]) * stride;
        const float x1 = x - mapping.x_pad, y1 = y - mapping.y_pad;
        // 不重排浮点表达式，防止整数框的边界舍入变化。
        const float x2 = x1 + (loc[0] + loc[2]) * stride;
        const float y2 = y1 + (loc[1] + loc[3]) * stride;
        for (int cls = 0; cls < OBJ_CLASS_NUM && results.count < OBJ_NUMB_MAX_SIZE; ++cls) {
            if (!(masks[cls / 64] & (uint64_t{1} << (cls % 64)))) continue;
            auto& result = results.results[results.count++];
            result.box.left = static_cast<int>(clampCoordinate(x1, context.model_width) / mapping.scale);
            result.box.top = static_cast<int>(clampCoordinate(y1, context.model_height) / mapping.scale);
            result.box.right = static_cast<int>(clampCoordinate(x2, context.model_width) / mapping.scale);
            result.box.bottom = static_cast<int>(clampCoordinate(y2, context.model_height) / mapping.scale);
            result.prop = decodeValue(data[(4 + cls) * cells + cell], attr);
            result.cls_id = cls;
        }
    }
}
}

int post_process(const rknn_app_context_t* context, const rknn_output* outputs,
                 const letterbox_t* mapping, float threshold,
                 PostprocessScratch* scratch, object_detect_result_list* results) {
    if (!context || !outputs || !mapping || !scratch || !results || !context->output_attrs ||
        context->io_num.n_output != 3 || !std::isfinite(threshold) || threshold <= 0 || threshold >= 1 ||
        !std::isfinite(mapping->scale) || mapping->scale <= 0) return -1;
    std::memset(results, 0, sizeof(*results));
    for (unsigned i = 0; i < 3; ++i) {
        const auto& attr = context->output_attrs[i];
        if (!outputs[i].buf || attr.n_dims != 4 || attr.dims[0] != 1 || attr.dims[1] != 84 ||
            attr.dims[2] != (80u >> i) || attr.dims[3] != (80u >> i) ||
            (context->is_quant && (!std::isfinite(attr.scale) || attr.scale <= 0))) return -1;
    }
    for (unsigned i = 0; i < 3 && results->count < OBJ_NUMB_MAX_SIZE; ++i) {
        const auto& attr = context->output_attrs[i];
        if (context->is_quant)
            decodeBranch(static_cast<const int8_t*>(outputs[i].buf), attr, *context, *mapping, threshold, *scratch, *results);
        else
            decodeBranch(static_cast<const float*>(outputs[i].buf), attr, *context, *mapping, threshold, *scratch, *results);
    }
    return 0;
}
