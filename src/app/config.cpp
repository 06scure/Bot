#include "app/config.hpp"

#include <stdexcept>

namespace bot {

Config load_config(const std::string& /*path*/) {
    // TODO 1: 读取 UTF-8 JSON，错误信息带文件路径与字段名。
    // TODO 2: 按 config/bot.json 的平铺字段映射到 vision / desk。
    // TODO 3: 检查字段类型及数值范围，再转换到 size_t/chrono，避免负数溢出。
    // TODO 4: ROI 必须有限、位于 [0,1] 且 left < right、top < bottom；
    //         min_score 位于 [0,1]，连续帧 >= 1，absence_reset_ms > 0，cooldown_ms >= 0。
    // TODO 5: input_size > 0；板端初始化时核对实际模型尺寸和精度。
    // TODO 6: 拒绝未知字段，避免配置拼写错误悄悄失效。
    // TODO 7: 明确相对模型路径以进程工作目录为基准；部署时设置 WorkingDirectory。
    // TODO(后续): 加入相机、TTS、音频、HTTP 配置；热重载先校验再整体替换，
    //            明确可重载字段白名单，不在线重建模型/设备上下文。
    throw std::logic_error("JSON config loading is not implemented; use built-in defaults for the scaffold");
}

}  // namespace bot
