#include "trigger/desk_state_machine.hpp"

// 这是测试任务清单，尚未实现任何 TEST_CASE，也没有已通过的功能测试。
// 实现时引入 <catch2/catch_test_macros.hpp>，由 Catch2WithMain 提供 main。
// 使用固定 TimePoint{} + milliseconds 构造时间，不依赖真实设备或 sleep。
//
// TODO: 连续 4 帧命中无事件，第 5 帧产生 ArrivalConfirmed。
// TODO: 候选期间插入一帧空结果，连续计数重新开始。
// TODO: 低分、非人体、ROI 外的人体均不计入命中，同帧多人只计一帧。
// TODO: 框中心恰好在 ROI 边界、score 恰好达到阈值的行为。
// TODO: 持续在场只产生一次到岗事件；短暂漏检不重新触发。
// TODO: 连续缺席 999 ms 不复位，达到 1000 ms 产生离开事件。
// TODO: 入队成功后启动冷却；离开复位不清除冷却。
// TODO: 冷却中重新到岗不触发问候，冷却到期仍在场不补播。
// TODO: 冷却结束后离开再到岗，能够产生新的有效到岗事件。
// TODO: 重复/无到岗的入队确认、时间倒退、非法配置被明确拒绝。
//
// ArrivalConfirmed 表达一次到岗事实，greeting_allowed 表达问候资格。
// 冷却期间仍报告新到岗，但 greeting_allowed 必须为 false。
// 测试分别断言这两者，避免把“识别到岗”与“允许问候”混为一谈。
