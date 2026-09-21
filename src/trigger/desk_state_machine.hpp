#pragma once

#include "vision/detection.hpp"

#include <chrono>
#include <cstddef>
#include <optional>
#include <vector>

namespace bot {

using MonotonicClock = std::chrono::steady_clock;
using TimePoint = MonotonicClock::time_point;

struct DeskStateMachineConfig {
    NormalizedRect roi{0.15F, 0.25F, 0.85F, 0.95F};
    float min_score{0.60F};
    std::size_t min_consecutive_frames{5};
    std::chrono::milliseconds absence_reset{1000};
    std::chrono::milliseconds cooldown{600000};
};

enum class DeskState {
    Absent,     //无人
    PersonCandidate,    //检测到有人，但不满足条件
    Present,    //确认有人
    Greeted,    //打招呼
    Cooldown,   //冷却时间
};

enum class DeskEvent {
    ArrivalConfirmed,
    DepartureConfirmed,
};

struct DeskUpdate {
    DeskState state{DeskState::Absent};
    std::optional<DeskEvent> event;
    // 到岗事实与问候资格分开：冷却期间仍可报告到岗，但不能问候。
    // 仅本次新到岗、未处于冷却时为 true；后续在场帧保持 false。
    bool greeting_allowed{false};
};

const char* to_string(DeskState state) noexcept;

// 纯逻辑模块：不访问设备、不睡眠、不创建线程，不读取系统墙上时间。
// 当前仅实现初始化与状态查询，其余操作会明确抛出未实现错误。
// 未来由 vision 线程独占；HTTP 通过 App 的同步快照读状态。
class DeskStateMachine {
public:
    explicit DeskStateMachine(DeskStateMachineConfig config);

    DeskState state() const noexcept;

    // 一次调用代表一帧“成功完成检测”的结果；空列表表示这一帧无人。
    // 采集/推理失败不能伪装成空列表，应另行报告健康状态。
    // now 由调用方注入，单测无需 sleep；约定时间不得倒退。
    DeskUpdate update(const std::vector<Detection>& detections, TimePoint now);

    // App 在技能请求成功入队后调用，作为冷却起点。
    // 此处表示“请求已接受”，不是“播放已成功”；播放结果单独记录。
    void mark_greeting_queued(TimePoint now);

private:
    DeskStateMachineConfig config_;
    DeskState state_{DeskState::Absent};

    // TODO: 实现规则后补充最小必要状态：连续命中帧数、缺席开始时间、
    // 冷却截止时间、本次在场是否已产生到岗事件，以及上次更新时间。
    // “是否在场”和“是否处于冷却”需分别保存，避免冷却到期导致在场重播。
};

}  // namespace bot
