#pragma once

#include "trigger/desk_state_machine.hpp"

#include <string>

namespace bot {

struct VisionConfig {
    std::string model{"models/yolov8n_person.rknn"};
    int input_size{416};
    std::string precision{"int8"};
};

struct Config {
    VisionConfig vision;
    DeskStateMachineConfig desk;
};

// TODO: vendor nlohmann/json 后实现读取、类型校验和取值校验。
// 文件缺失、格式错误、非法参数均应报错，不能静默回退默认值。
// 默认配置和 config/bot.json 保持一致。
Config load_config(const std::string& path);

}  // namespace bot
