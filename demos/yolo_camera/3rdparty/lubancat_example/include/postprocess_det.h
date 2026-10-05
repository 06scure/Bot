#ifndef _RKNN_YOLO26_DEMO_POSTPROCESS_H_
#define _RKNN_YOLO26_DEMO_POSTPROCESS_H_

#include <stdint.h>
#include <array>
#include "rknn_api.h"
#include "image_utils.h"

#define OBJ_NUMB_MAX_SIZE 128
#define OBJ_CLASS_NUM 80

typedef struct {
    image_rect_t box;
    float prop;
    int cls_id;
} object_detect_result;

typedef struct
{
    uint8_t *seg_mask;
} object_segment_result;

typedef struct {
    int id;
    int count;
    object_detect_result results[OBJ_NUMB_MAX_SIZE];
    object_segment_result results_seg[OBJ_NUMB_MAX_SIZE];
} object_detect_result_list;

// 每个网格用 80 位记录超过阈值的类别；最大分支为 80×80。
struct PostprocessScratch {
    std::array<std::array<uint64_t, 2>, 80 * 80> classes{};
};
int post_process(const rknn_app_context_t *app_ctx, const rknn_output *outputs,
                 const letterbox_t *letter_box, float conf_threshold,
                 PostprocessScratch *scratch, object_detect_result_list *od_results);

#endif //_RKNN_YOLO26_DEMO_POSTPROCESS_H_
