#include "trigger/desk_state_machine.hpp"

#include <stdexcept>
#include <utility>
#include <cmath>
#include <algorithm>

namespace bot {

DeskStateMachine::DeskStateMachine(DeskStateMachineConfig config)
    : config_(std::move(config)) {
    const auto& r=config_.roi;
    if (!std::isfinite(r.left)||!std::isfinite(r.top)||!std::isfinite(r.right)||!std::isfinite(r.bottom)||
        r.left<0||r.top<0||r.right>1||r.bottom>1||r.left>=r.right||r.top>=r.bottom||
        !std::isfinite(config_.min_score)||config_.min_score<0||config_.min_score>1||
        !config_.min_consecutive_frames||config_.absence_reset.count()<0||config_.cooldown.count()<0)
        throw std::invalid_argument("invalid desk state machine configuration");
}

DeskState DeskStateMachine::state() const noexcept {
    return state_;
}

void DeskStateMachine::check_time(TimePoint now) {
    if(last_update_ && now<*last_update_) throw std::invalid_argument("time moved backwards");
    last_update_=now;
}

DeskUpdate DeskStateMachine::update(const std::vector<Detection>& detections, TimePoint now) {
    check_time(now);
    queue_allowed_=false;
    const bool cooling=cooldown_until_ && now<*cooldown_until_;
    const bool hit=std::any_of(detections.begin(),detections.end(),[&](const Detection& d) {
        const auto& b=d.box; const auto& r=config_.roi;
        if(!d.is_person||!std::isfinite(d.score)||d.score<config_.min_score||d.score>1||
           !std::isfinite(b.left)||!std::isfinite(b.top)||!std::isfinite(b.right)||!std::isfinite(b.bottom)||
           b.left<0||b.top<0||b.right>1||b.bottom>1||b.left>=b.right||b.top>=b.bottom) return false;
        const auto x=(b.left+b.right)*.5f, y=(b.top+b.bottom)*.5f;
        return x>=r.left&&x<=r.right&&y>=r.top&&y<=r.bottom;
    });
    std::optional<DeskEvent> event;
    if(hit) {
        absent_since_.reset();
        if(!present_ && ++consecutive_>=config_.min_consecutive_frames) {
            present_=true; queued_=false;
            event=DeskEvent::ArrivalConfirmed; queue_allowed_=!cooling;
        }
    } else {
        consecutive_=0;
        if(present_) {
            if(!absent_since_) absent_since_=now;
            if(now-*absent_since_>=config_.absence_reset) {
                present_=false; queued_=false; absent_since_.reset();
                event=DeskEvent::DepartureConfirmed;
            }
        }
    }
    state_=present_ ? (queued_ ? DeskState::Greeted : DeskState::Present)
                    : (consecutive_ ? DeskState::PersonCandidate : cooling ? DeskState::Cooldown : DeskState::Absent);
    return {state_,event,queue_allowed_};
}

void DeskStateMachine::mark_greeting_queued(TimePoint now) {
    check_time(now);
    if(!present_||queued_||!queue_allowed_) throw std::logic_error("no eligible arrival to acknowledge");
    queued_=true; queue_allowed_=false;
    cooldown_until_=now+config_.cooldown;
    state_=DeskState::Greeted;
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
