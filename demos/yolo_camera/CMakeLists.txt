cmake_minimum_required(VERSION 3.16)
project(yolo_camera_demo LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(THIRD_PARTY "${CMAKE_CURRENT_SOURCE_DIR}/3rdparty")
find_package(OpenCV REQUIRED COMPONENTS core imgproc highgui)

add_library(rknn_runtime SHARED IMPORTED)
set_target_properties(rknn_runtime PROPERTIES
    IMPORTED_LOCATION "${THIRD_PARTY}/rknpu2/lib/librknnrt.so"
)

add_library(rga_runtime SHARED IMPORTED)
set_target_properties(rga_runtime PROPERTIES
    IMPORTED_LOCATION "${THIRD_PARTY}/librga/lib/librga.so"
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
    "${THIRD_PARTY}/librga/include"
)
target_link_libraries(yolo_camera_demo PRIVATE rknn_runtime rga_runtime ${OpenCV_LIBS})
target_compile_options(yolo_camera_demo PRIVATE -Wall -Wextra)

set(_rpath
    "$ORIGIN/../3rdparty/rknpu2/lib"
    "$ORIGIN/../3rdparty/librga/lib"
)
set_target_properties(yolo_camera_demo PROPERTIES
    BUILD_WITH_INSTALL_RPATH TRUE
    INSTALL_RPATH "${_rpath}"
)