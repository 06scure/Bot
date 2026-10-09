cmake_minimum_required(VERSION 3.16)
project(greet_at_desk_demo LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(YOLO_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../yolo_camera" CACHE PATH "YOLO demo dependencies")
add_subdirectory(../rk809_tts speech_build)
find_package(OpenCV REQUIRED COMPONENTS core imgproc highgui)
add_library(desk_logic ../../src/trigger/desk_state_machine.cpp)
target_include_directories(desk_logic PUBLIC ../../src)
add_executable(greet_at_desk_demo main.cpp "${YOLO_DIR}/camera.cpp" "${YOLO_DIR}/detector.cpp"
    "${YOLO_DIR}/3rdparty/lubancat_example/src/postprocess_det.cc")
target_include_directories(greet_at_desk_demo PRIVATE "${YOLO_DIR}"
    "${YOLO_DIR}/3rdparty/rknpu2/include" "${YOLO_DIR}/3rdparty/librga/include"
    "${YOLO_DIR}/3rdparty/lubancat_example/include" ${OpenCV_INCLUDE_DIRS})
target_link_libraries(greet_at_desk_demo PRIVATE speech desk_logic
    "${YOLO_DIR}/3rdparty/librga/lib/librga.so" ${OpenCV_LIBS})
# RGA 厂商头文件包含 GNU 扩展，与现有视觉 demo 保持相同诊断选项。
target_compile_options(greet_at_desk_demo PRIVATE -Wall -Wextra)
enable_testing()
add_executable(desk_state_test state_test.cpp)
target_link_libraries(desk_state_test PRIVATE desk_logic)
add_test(NAME desk_state_test COMMAND desk_state_test)
add_executable(greet_replay_test replay_test.cpp)
target_link_libraries(greet_replay_test PRIVATE speech desk_logic)
# 回放会真实播放喇叭，手动执行，不混入普通 CTest。
