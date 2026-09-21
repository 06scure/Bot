#pragma once

namespace bot {

// 原始摄像头图像坐标系中的归一化矩形，合法范围为 [0, 1]。
// detector 必须先消除缩放/letterbox 的偏移，再输出这些坐标。
// 不在此结构中存放 cv::Mat、RKNN 张量或设备缓冲区。
struct NormalizedRect {
    float left{0.0F};
    float top{0.0F};
    float right{1.0F};
    float bottom{1.0F};
};

struct Detection {
    NormalizedRect box;
    float score{0.0F};
    // 由检测器适配模型标签；状态机不依赖某个模型的类别编号。
    bool is_person{false};
};

// TODO(检测器): 过滤非法坐标/非有限值，定义越界框裁剪策略。
// TODO(预处理): 明确像素格式、缩放比例和填充量，保证 ROI 与框同坐标系。

}  // namespace bot
