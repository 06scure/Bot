#pragma once
#include "audio.hpp"
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <iostream>
#include <mutex>
#include <thread>

// Demo 的应用装配层：播放线程只读已经合成的 WAV，不共享 RKNN 上下文。
class AudioWorker {
    std::string path_;
    std::mutex mutex_;
    std::condition_variable ready_;
    bool stopping_{false}, pending_{false}, busy_{false};
    std::chrono::steady_clock::time_point enqueued_;
    std::atomic<bool> failed_{false};
    std::thread worker_;
    void loop() {
        for (;;) {
            std::unique_lock<std::mutex> lock(mutex_);
            ready_.wait(lock,[&]{return stopping_||pending_;});
            if(stopping_ && !pending_) return;
            pending_=false;
            const auto start=enqueued_;
            lock.unlock();
            try {
                std::cout<<"playback_started queue_ms="<<std::chrono::duration<double,std::milli>(
                    std::chrono::steady_clock::now()-start).count()<<'\n';
                speech::play(path_);
                std::cout<<"playback_complete\n";
            } catch(const std::exception& e) {
                std::cerr<<"playback_failed "<<e.what()<<'\n'; failed_=true;
            }
            lock.lock(); busy_=false;
        }
    }
public:
    explicit AudioWorker(std::string path):path_(std::move(path)),worker_([this]{loop();}) {}
    ~AudioWorker() { finish(); }
    void finish() {
        { std::lock_guard<std::mutex> lock(mutex_); stopping_=true; }
        ready_.notify_one();
        if(worker_.joinable()) worker_.join();
    }
    AudioWorker(const AudioWorker&)=delete;
    AudioWorker& operator=(const AudioWorker&)=delete;
    bool failed() const { return failed_.load(); }
    bool enqueue() {
        std::lock_guard<std::mutex> lock(mutex_);
        if(stopping_||busy_||failed_) return false;
        enqueued_=std::chrono::steady_clock::now(); busy_=true; pending_=true;
        ready_.notify_one(); return true;
    }
};
