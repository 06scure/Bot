cmake_minimum_required(VERSION 3.16)
project(yolo_camera_demo LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# RKNN 和野火后处理来自 Demo；OpenCV 仍由板端 apt 安装。
set(THIRD_PARTY "${CMAKE_CURRENT_SOURCE_DIR}/3rdparty")
find_package(OpenCV REQUIRED COMPONENTS core imgproc highgui)

# IMPORTED 表示使用已经编译好的库，不是重新编译 RKNN。
add_library(rknn_runtime SHARED IMPORTED)
set_target_properties(rknn_runtime PROPERTIES
    IMPORTED_LOCATION "${THIRD_PARTY}/rknpu2/lib/librknnrt.so"
)

add_executable(yolo_camera_demo
    main.cpp
    camera.cpp
    detector.cpp
    3rdparty/lubancat_example/src/postprocess_det.cc
)
target_include_directories(yolo_camera_demo PRIVATE
    "${THIRD_PARTY}/rknpu2/include"
    "${THIRD_PARTY}/lubancat_example/include"
    ${OpenCV_INCLUDE_DIRS}
)
target_link_libraries(yolo_camera_demo PRIVATE rknn_runtime ${OpenCV_LIBS})
target_compile_options(yolo_camera_demo PRIVATE -Wall -Wextra)

# 程序在 build/，库在同一 Demo 的 3rdparty/；整体移动 Demo 后仍有效。
# 不再复制到 build/lib，也不依赖 run.sh 设置 LD_LIBRARY_PATH。
set_target_properties(yolo_camera_demo PROPERTIES
    BUILD_WITH_INSTALL_RPATH TRUE
    INSTALL_RPATH "$ORIGIN/../3rdparty/rknpu2/lib"
)
