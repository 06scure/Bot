#include "audio_worker.hpp"
#include "tts.hpp"
#include "trigger/desk_state_machine.hpp"
#include <iostream>
#include <stdexcept>
// 明确使用合成检测结果，不属于真实摄像头到岗验收。
int main() {
    try {
        std::cout.setf(std::ios::unitbuf);
        std::cout<<"SYNTHETIC_DETECTION_REPLAY (not live camera acceptance)\n";
        speech::configure_speaker();
        speech::MeloTts tts("../rk809_tts/model");
        // 测试文件只存在于当前 demo 目录。
        const std::string wav="replay-test.wav";
        speech::save_wav(wav,tts.synthesize("早上好，欢迎回来。"));
        bot::DeskStateMachineConfig config;
        config.min_consecutive_frames=2;
        config.cooldown=std::chrono::milliseconds(3000);
        bot::DeskStateMachine state(config);
        const std::vector<bot::Detection> person{{{.3f,.3f,.7f,.9f},.8f,true}};
        unsigned greetings=0, arrivals=0;
        auto frame=[&](int time,bool present) {
            const auto now=bot::TimePoint{}+std::chrono::milliseconds(time);
            const auto result=state.update(present ? person : std::vector<bot::Detection>{},now);
            if(result.event==bot::DeskEvent::ArrivalConfirmed) ++arrivals;
            if(result.greeting_allowed) {
                AudioWorker player(wav);
                if(!player.enqueue()) throw std::runtime_error("replay enqueue rejected");
                state.mark_greeting_queued(now);
                // shutdown 要等待已经接受的播放请求完成，之后才能检查结果。
                player.finish();
                if(player.failed()) throw std::runtime_error("replay playback failed");
                ++greetings;
            }
        };
        frame(0,false); frame(100,true); frame(200,true); frame(300,true);
        frame(400,false); frame(500,true); // 短暂漏检不重复
        frame(600,false); frame(1600,false);
        frame(1700,true); frame(1800,true); // 冷却内再次进入，不问候
        frame(3300,true); // 冷却结束不补播
        frame(3400,false); frame(4400,false);
        frame(4500,true); frame(4600,true); // 冷却后新到岗，第二次问候
        if(arrivals!=3 || greetings!=2) throw std::runtime_error("wrong replay counts");
        std::cout<<"PASS replay arrivals=3 greetings=2\n";
        return 0;
    } catch(const std::exception& e) { std::cerr<<"FAIL "<<e.what()<<'\n'; return 1; }
}
