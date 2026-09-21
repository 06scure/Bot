# 手动开启 BOT_BUILD_TESTS 前，将固定版本 Catch2 v3 的完整源码及许可证
# 放入 third_party/Catch2，并落实 desk_state_machine_test.cpp 中的测试。
# 不使用 FetchContent，以保证后续板端可以离线构建。
if(NOT EXISTS "${PROJECT_SOURCE_DIR}/third_party/Catch2/CMakeLists.txt")
    message(FATAL_ERROR "Tests require vendored Catch2 v3 at third_party/Catch2. See docs/开发起步.md")
endif()

add_subdirectory("${PROJECT_SOURCE_DIR}/third_party/Catch2"
                 "${PROJECT_BINARY_DIR}/third_party/Catch2" EXCLUDE_FROM_ALL)
add_executable(bot_logic_tests desk_state_machine_test.cpp)
target_link_libraries(bot_logic_tests PRIVATE bot_logic Catch2::Catch2WithMain)
if(MSVC)
    target_compile_options(bot_logic_tests PRIVATE /utf-8)
endif()

# 当前没有注册实际用例；补全后由 CTest 执行。
# 不允许“零用例”冒充测试通过，故不传 allow-running-no-tests。
add_test(NAME bot_logic_tests COMMAND bot_logic_tests)
