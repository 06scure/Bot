#include "trigger/desk_state_machine.hpp"
#include <iostream>
#include <limits>
#include <stdexcept>
using namespace std::chrono_literals;
namespace {
int checks=0;
void require(bool value,const char* message) {
    ++checks;
    if(!value) throw std::runtime_error(message);
}
template<class F> void rejects(F operation) {
    bool rejected=false;
    try { operation(); } catch(const std::exception&) { rejected=true; }
    require(rejected,"expected rejection");
}
const std::vector<bot::Detection> person{{{.3f,.3f,.7f,.9f},.8f,true}};
bot::TimePoint at(int ms) { return bot::TimePoint{}+std::chrono::milliseconds(ms); }
}
int main() {
    try {
        bot::DeskStateMachineConfig config; config.cooldown=3000ms;
        bot::DeskStateMachine m(config);
        rejects([&]{m.mark_greeting_queued(at(0));});
        for(int i=0;i<4;++i) require(!m.update(person,at(i)).event,"premature arrival");
        m.update({},at(4));
        for(int i=5;i<9;++i) require(!m.update(person,at(i)).event,"candidate reset failed");
        auto u=m.update(person,at(9));
        require(u.event==bot::DeskEvent::ArrivalConfirmed && u.greeting_allowed,"arrival missing");
        m.mark_greeting_queued(at(9));
        rejects([&]{m.mark_greeting_queued(at(9));});
        require(!m.update(person,at(10)).event,"repeated arrival");
        m.update({},at(20));
        require(!m.update({},at(1019)).event,"early departure");
        require(!m.update(person,at(1020)).event,"short loss caused repeat");
        m.update({},at(1021));
        require(m.update({},at(2021)).event==bot::DeskEvent::DepartureConfirmed,"departure missing");
        for(int i=2022;i<2026;++i) m.update(person,at(i));
        u=m.update(person,at(2026));
        require(u.event==bot::DeskEvent::ArrivalConfirmed&&!u.greeting_allowed,"cooldown cleared by departure");
        rejects([&]{m.mark_greeting_queued(at(2026));});
        require(!m.update(person,at(3010)).greeting_allowed,"cooldown expiry replay");
        m.update({},at(4000)); m.update({},at(5000));
        for(int i=5001;i<5005;++i) m.update(person,at(i));
        require(m.update(person,at(5005)).greeting_allowed,"new arrival after cooldown");
        rejects([&]{m.update(person,at(4000));});

        config.min_consecutive_frames=1;
        bot::DeskStateMachine boundary(config);
        auto detection=person;
        detection[0].score=.60f; detection[0].box={.10f,.3f,.20f,.9f};
        require(boundary.update(detection,at(0)).greeting_allowed,"inclusive ROI/score boundary");
        bot::DeskStateMachine filtered(config);
        detection[0].score=.59f; require(!filtered.update(detection,at(0)).event,"low score");
        detection=person; detection[0].is_person=false;
        require(!filtered.update(detection,at(1)).event,"nonperson");
        detection=person; detection[0].box={0,0,.1f,.1f};
        require(!filtered.update(detection,at(2)).event,"outside ROI");
        detection=person; detection[0].box.left=std::numeric_limits<float>::quiet_NaN();
        require(!filtered.update(detection,at(3)).event,"NaN detection");
        config.min_consecutive_frames=5; bot::DeskStateMachine multiple(config);
        auto crowd=person; crowd.push_back(person[0]);
        require(!multiple.update(crowd,at(0)).event,"multiple people counted as frames");
        for(int i=1;i<4;++i) require(!multiple.update(crowd,at(i)).event,"crowd early arrival");
        require(multiple.update(crowd,at(4)).greeting_allowed,"crowd arrival");
        config.min_consecutive_frames=0; rejects([&]{bot::DeskStateMachine invalid(config);});
        config.min_consecutive_frames=5; config.roi.left=2;
        rejects([&]{bot::DeskStateMachine invalid(config);});
        std::cout<<"PASS "<<checks<<" state assertions\n";
        return 0;
    } catch(const std::exception& e) { std::cerr<<"FAIL "<<e.what()<<'\n'; return 1; }
}
