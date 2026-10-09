#include "audio_worker.hpp"
#include "tts.hpp"
#include "camera.hpp"
#include "detector.hpp"
#include "trigger/desk_state_machine.hpp"
#include <opencv2/highgui.hpp>
#include <opencv2/imgproc.hpp>
#include <algorithm>
#include <csignal>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <stdexcept>

namespace {
volatile std::sig_atomic_t stopped=0;
void stop(int) { stopped=1; }
struct TempAudio {
    std::string directory;
    TempAudio() {
        char pattern[]="/tmp/bot-greeting-XXXXXX";
        const auto* path=mkdtemp(pattern);
        if(!path) throw std::runtime_error("cannot create private audio cache");
        directory=path;
    }
    ~TempAudio() { std::error_code error; std::filesystem::remove_all(directory,error); }
    std::string wav() const { return directory+"/greeting.wav"; }
};
}
int main(int argc,char** argv) {
    std::signal(SIGINT,stop); std::signal(SIGTERM,stop);
    std::cout.setf(std::ios::unitbuf);
    try {
        bool headless=false; int seconds=0;
        for(int i=1;i<argc;++i) {
            const std::string arg=argv[i];
            if(arg=="--headless") headless=true;
            else if(arg=="--seconds" && i+1<argc) {
                const std::string value=argv[++i]; std::size_t end=0;
                seconds=std::stoi(value,&end);
                if(seconds<=0 || end!=value.size()) throw std::invalid_argument("invalid --seconds");
            } else throw std::invalid_argument("Usage: greet_at_desk_demo [--headless] [--seconds N]");
        }
        cv::setNumThreads(2);
        speech::configure_speaker();
        TempAudio cache;
        speech::MeloTts tts("../rk809_tts/model");
        speech::save_wav(cache.wav(),tts.synthesize("早上好，欢迎回来。"));
        std::cout<<"tts_cache_ready text=早上好，欢迎回来。\n";
        demo::Detector detector("../yolo_camera/model/yolo26n-rk3568-i8.rknn");
        demo::Camera camera("/dev/video0",640,480);
        bot::DeskStateMachine state(bot::DeskStateMachineConfig{});
        AudioWorker audio(cache.wav());
        if(!headless) { cv::namedWindow("Greet at desk",cv::WINDOW_NORMAL); cv::resizeWindow("Greet at desk",960,720); }
        const auto start=bot::MonotonicClock::now(); auto report=start;
        unsigned frames=0,interval=0,arrivals=0,greetings=0;
        demo::Frame frame;
        while(!stopped) {
            if(seconds && bot::MonotonicClock::now()-start>=std::chrono::seconds(seconds)) break;
            if(audio.failed()) throw std::runtime_error("audio worker failed");
            if(!camera.read(frame,stopped)) {
                if(stopped) break;
                throw std::runtime_error("camera stopped unexpectedly");
            }
            demo::Timing timing;
            const auto detections=detector.run(frame.bgr,.60f,timing);
            std::vector<bot::Detection> people;
            for(int i=0;i<detections.count;++i) {
                const auto& d=detections.results[i]; if(d.cls_id!=0) continue;
                const auto clamp=[](float v){return std::clamp(v,0.f,1.f);};
                people.push_back({{clamp(float(d.box.left)/frame.bgr.cols),clamp(float(d.box.top)/frame.bgr.rows),
                    clamp(float(d.box.right)/frame.bgr.cols),clamp(float(d.box.bottom)/frame.bgr.rows)},d.prop,true});
                if(!headless) cv::rectangle(frame.bgr,{d.box.left,d.box.top},{d.box.right,d.box.bottom},{0,255,0},2);
            }
            const auto now=bot::MonotonicClock::now();
            const auto update=state.update(people,now);
            if(update.event) {
                if(*update.event==bot::DeskEvent::ArrivalConfirmed) {
                    ++arrivals;
                    std::cout<<"arrival_confirmed greeting_allowed="<<update.greeting_allowed<<'\n';
                } else std::cout<<"departure_confirmed\n";
            }
            if(update.greeting_allowed) {
                if(audio.enqueue()) { state.mark_greeting_queued(now); ++greetings; std::cout<<"greeting_queued\n"; }
                else std::cerr<<"greeting_rejected busy_or_failed\n";
            }
            if(!headless) {
                cv::rectangle(frame.bgr,{96,120},{544,456},{255,150,0},2);
                cv::putText(frame.bgr,bot::to_string(state.state()),{12,30},cv::FONT_HERSHEY_SIMPLEX,.7,{0,255,255},2);
                cv::imshow("Greet at desk",frame.bgr);
                const int key=cv::waitKey(1)&255; if(key=='q'||key==27) stopped=1;
            }
            ++frames; ++interval;
            const auto elapsed=std::chrono::duration<double>(now-report).count();
            if(elapsed>=1) {
                std::cout<<"frames="<<frames<<" fps="<<interval/elapsed<<" npu_ms="<<timing.inference_ms
                         <<" people="<<people.size()<<" state="<<bot::to_string(state.state())<<'\n';
                report=now; interval=0;
            }
        }
        audio.finish();
        if(audio.failed()) throw std::runtime_error("audio worker failed");
        if(!headless) cv::destroyAllWindows();
        std::cout<<"stopped frames="<<frames<<" arrivals="<<arrivals<<" greetings="<<greetings<<'\n';
        return 0;
    } catch(const std::exception& e) {
        std::cerr<<"ERROR "<<e.what()<<'\n'; return 1;
    }
}
