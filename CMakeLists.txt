cmake_minimum_required(VERSION 3.16)
project(desk_bot VERSION 0.1.0 LANGUAGES CXX)

# 第一批框架仅使用标准库，无须板卡 SDK，也不会联网下载依赖。
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(bot_logic
    src/trigger/desk_state_machine.cpp
)
target_include_directories(bot_logic PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")

# 当前 bot 只是框架入口，运行后打印说明并退出，不启动实际机器人。
add_executable(bot
    src/main.cpp
    src/app/config.cpp
)
target_link_libraries(bot PRIVATE bot_logic)

foreach(target bot_logic bot)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /utf-8)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endforeach()

option(BOT_BUILD_TESTS "构建逻辑测试（需先补充测试并 vendor Catch2 v3）" OFF)
if(BOT_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

# TODO(板端集成): 增加硬件构建选项及 RKNN、RGA、OpenCV、ALSA 依赖。
# 硬件库只链接到 bot/对应硬件目标，不要引入 bot_logic。
# TODO(应用装配): 增加 app.cpp，集中管理线程、TTS 队列和退出流程。
