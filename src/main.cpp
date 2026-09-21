#include "app/config.hpp"
#include "trigger/desk_state_machine.hpp"

#include <exception>
#include <iostream>
#include <string>

// 入口只做参数处理和顶层错误报告。未来把启动/停止交给 App，
// 不在这里堆积摄像头、推理、HTTP 或播放代码。
int main(int argc, char* argv[]) {
    try {
        if (argc == 2 && std::string(argv[1]) == "--help") {
            std::cout << "Usage: bot [--config PATH]\n"
                         "Scaffold only; JSON loading and runtime are not implemented.\n";
            return 0;
        }
        if (argc != 1 &&
            !(argc == 3 && std::string(argv[1]) == "--config")) {
            std::cerr << "Usage: bot [--config PATH]\n";
            return 2;
        }

        // 无参数使用内置默认值；显式指定配置时，未实现的加载器会报错。
        // 不把“没有读取文件”伪装成“成功加载配置”。
        const auto config = argc == 3
            ? bot::load_config(argv[2])
            : bot::Config{};
        bot::DeskStateMachine state_machine(config.desk);
        std::cout << "desk_bot scaffold started.\n"
                  << "Configuration source: built-in defaults\n"
                  << "Initial desk state: " << bot::to_string(state_machine.state()) << '\n'
                  << "No camera, inference, speech, HTTP or worker threads are running.\n"
                  << "See docs/开发起步.md for implementation tasks.\n";

        // TODO: App app(config); return app.run();
        // App 负责配置校验、资源初始化、语音缓存预热、线程启动及按序关闭。
        // 停止信号处理不能直接调用普通 HTTP/日志函数；后续在正常线程中处理。
        return 0;
    } catch (const std::exception& error) {
        // TODO: vendor spdlog 后统一日志格式，并为模块故障记录结构化事件。
        std::cerr << "Startup failed: " << error.what() << '\n';
        return 1;
    }
}
