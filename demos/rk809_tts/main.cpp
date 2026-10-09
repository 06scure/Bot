#include "audio.hpp"
#include "tts.hpp"
#include <chrono>
#include <iostream>
#include <stdexcept>
int main(int argc, char** argv) {
    try {
        std::cout.setf(std::ios::unitbuf);
        if (argc < 2 || (std::string(argv[1])!="tone" && std::string(argv[1])!="tts"))
            throw std::invalid_argument("Usage: rk809_tts_demo tone | tts [text] [model_dir]");
        speech::configure_speaker();
        std::string path;
        if (std::string(argv[1]) == "tone") {
            path = "tone.wav";
            speech::save_wav(path, speech::test_tone());
        } else {
            const auto start = std::chrono::steady_clock::now();
            speech::MeloTts tts(argc>3 ? argv[3] : "model");
            const auto samples = tts.synthesize(argc>2 ? argv[2] : "早上好，欢迎回来。");
            path = "greeting.wav";
            speech::save_wav(path, samples);
            std::cout << "tts_ready seconds=" << samples.size()/44100.0 << " cold_ms="
                      << std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count() << '\n';
        }
        speech::play(path);
        std::cout << "playback_complete file=" << path << '\n';
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "ERROR " << e.what() << '\n';
        return 1;
    }
}
