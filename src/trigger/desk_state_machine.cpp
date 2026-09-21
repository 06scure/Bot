#include "trigger/desk_state_machine.hpp"

#include <stdexcept>
#include <utility>

namespace bot {

DeskStateMachine::DeskStateMachine(DeskStateMachineConfig config)
    : config_(std::move(config)) {
    // TODO: 校验 ROI、分数、连续帧数和时间参数；非法值抛 invalid_argument。
    // 在模块边界校验，确保未来测试或其他调用方绕过 JSON 也不会传入坏配置。
}

DeskState DeskStateMachine::state() const noexcept {
    return state_;
}

DeskUpdate DeskStateMachine::update(
    const std::vector<Detection>& /*detections*/, TimePoint /*now*/) {
    // TODO 1: 找出置信度达标且框中心在 ROI 内的人体；同帧多人只记一次命中。
    // TODO 2: 累计连续命中帧；确认到岗前遇到不命中帧则清零。
    // TODO 3: 达到阈值产生一次 ArrivalConfirmed，同次在场不得反复产生事件。
    // TODO 4: 已确认在场后，连续缺席满 absence_reset 才产生 DepartureConfirmed。
    // TODO 5: 离开可复位在场状态，但不得清除尚未结束的冷却期限。
    // TODO 6: 到岗事件仅在非冷却时设置 greeting_allowed；冷却期间照常报告
    //         到岗事实但不问候，冷却结束且人仍在场时也不主动补播。
    // TODO 7: 拒绝倒退时间；明确 ROI 边界包含、时间阈值 >= 等边界规则。
    // 硬件长时间中断后的重新确认策略需在 App 接入时明确，不能静默算作离开。
    throw std::logic_error("DeskStateMachine::update is not implemented");
}

void DeskStateMachine::mark_greeting_queued(TimePoint /*now*/) {
    // TODO: 仅接受本次有效到岗的入队确认，记录冷却起点；拒绝重复确认。
    // 队列满/技能未执行时不调用本方法，失败由 App 记录，本次到岗不自动重试。
    // 手动 HTTP 问候的限流与自动到岗冷却是否共享，需在接入 HTTP 前明确。
    throw std::logic_error("DeskStateMachine::mark_greeting_queued is not implemented");
}

const char* to_string(DeskState state) noexcept {
    switch (state) {
        case DeskState::Absent: return "ABSENT";
        case DeskState::PersonCandidate: return "PERSON_CANDIDATE";
        case DeskState::Present: return "PRESENT";
        case DeskState::Greeted: return "GREETED";
        case DeskState::Cooldown: return "COOLDOWN";
    }
    return "UNKNOWN";
}

}  // namespace bot
