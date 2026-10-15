cmake_minimum_required(VERSION 3.16)
project(gtlibcpp
    VERSION 1.0.0
    DESCRIPTION "Reliable, capability-scoped memory operations for authorized offline trainers and local automation agents"
    LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(GTLIBCPP_BUILD_TESTS "Build the cross-platform regression test suite" ON)
option(GTLIBCPP_BUILD_DEMOS  "Build the demo trainers" OFF)
option(GTLIBCPP_ENABLE_WERROR "Treat warnings as errors" ON)

if(MSVC)
    add_compile_options(/W4 /permissive- /Zc:__cplusplus)
    if(GTLIBCPP_ENABLE_WERROR)
        add_compile_options(/WX)
    endif()
else()
    add_compile_options(-Wall -Wextra -Wpedantic -Wshadow -Wnon-virtual-dtor)
    if(GTLIBCPP_ENABLE_WERROR)
        add_compile_options(-Werror)
    endif()
endif()

add_library(gtlibcpp_core STATIC
    src/policy.cpp
    src/parser.cpp
    src/agent.cpp
    src/freeze.cpp
    src/inproc_transport.cpp
    src/darwin_backend.cpp
)
target_include_directories(gtlibcpp_core
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)
set_target_properties(gtlibcpp_core PROPERTIES
    POSITION_INDEPENDENT_CODE ON
)
add_library(gtlibcpp::core ALIAS gtlibcpp_core)

if(WIN32)
    add_library(gtlibcpp_win32 STATIC
        src/windows_backend.cpp
        src/named_pipe_transport.cpp
    )
    target_include_directories(gtlibcpp_win32
        PUBLIC
            $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
            $<INSTALL_INTERFACE:include>
    )
    target_link_libraries(gtlibcpp_win32 PUBLIC gtlibcpp::core psapi bcrypt)
    target_compile_definitions(gtlibcpp_win32 PRIVATE _WIN32_WINNT=0x0601)
    add_library(gtlibcpp::win32 ALIAS gtlibcpp_win32)
endif()

# Minimal, read-only CLI tooling; no target process attachment.
add_executable(gtctl tools/gtctl.cpp)
target_compile_definitions(gtctl PRIVATE GTLIBCPP_VERSION="${PROJECT_VERSION}")
install(TARGETS gtctl RUNTIME DESTINATION bin)
if(GTLIBCPP_BUILD_TESTS)
    add_test(NAME gtctl_version COMMAND gtctl --version)
    add_test(NAME gtctl_health COMMAND gtctl --health)
endif()

# CLI application using gtlibcpp for Cheat Table parsing, hotkey detection, and safe execution
add_executable(gt_trainer_cli tools/gt_trainer_cli.cpp)
target_link_libraries(gt_trainer_cli PRIVATE gtlibcpp::core)
target_compile_definitions(gt_trainer_cli PRIVATE GTLIBCPP_VERSION="${PROJECT_VERSION}")
if(WIN32)
    target_link_libraries(gt_trainer_cli PRIVATE gtlibcpp::win32)
endif()
if(APPLE)
    add_custom_command(TARGET gt_trainer_cli POST_BUILD
        COMMAND codesign -s - --entitlements ${CMAKE_CURRENT_SOURCE_DIR}/cmake/entitlements.plist -f $<TARGET_FILE:gt_trainer_cli>
        COMMENT "Codesigning gt_trainer_cli with get-task-allow entitlement"
    )
endif()
install(TARGETS gt_trainer_cli RUNTIME DESTINATION bin)
if(GTLIBCPP_BUILD_TESTS)
    if(APPLE)
        add_test(NAME gt_trainer_cli_test COMMAND gt_trainer_cli --test ${CMAKE_CURRENT_SOURCE_DIR}/demos/CheatTable/assaultcube_mac.ct)
    else()
        add_test(NAME gt_trainer_cli_test COMMAND gt_trainer_cli --test ${CMAKE_CURRENT_SOURCE_DIR}/demos/CheatTable/assaultcube.ct)
    endif()
endif()

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

install(TARGETS gtlibcpp_core
    EXPORT gtlibcppTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    PUBLIC_HEADER DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)
if(WIN32)
    install(TARGETS gtlibcpp_win32
        EXPORT gtlibcppTargets
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    )
endif()

install(DIRECTORY include/gtlibcpp DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

configure_package_config_file(
    cmake/gtlibcppConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/gtlibcppConfig.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/gtlibcpp
)
write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/gtlibcppConfigVersion.cmake
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)
install(EXPORT gtlibcppTargets
    FILE gtlibcppTargets.cmake
    NAMESPACE gtlibcpp::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/gtlibcpp
)
install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/gtlibcppConfig.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/gtlibcppConfigVersion.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/gtlibcpp
)

if(GTLIBCPP_BUILD_TESTS)
    enable_testing()
    add_executable(gtlibcpp_log_header_compile tests/log_header_compile.cpp)
    target_link_libraries(gtlibcpp_log_header_compile PRIVATE gtlibcpp::core)
    add_test(NAME gtlibcpp_log_header_compile COMMAND gtlibcpp_log_header_compile)

    add_executable(gtlibcpp_core_tests tests/core_tests.cpp)
    target_link_libraries(gtlibcpp_core_tests PRIVATE gtlibcpp::core)
    add_test(NAME gtlibcpp_core_tests COMMAND gtlibcpp_core_tests)

    add_executable(gtlibcpp_parser_tests tests/parser_tests.cpp)
    target_link_libraries(gtlibcpp_parser_tests PRIVATE gtlibcpp::core)
    target_compile_definitions(gtlibcpp_parser_tests PRIVATE GTLIBCPP_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    add_test(NAME gtlibcpp_parser_tests COMMAND gtlibcpp_parser_tests)

    add_executable(gtlibcpp_agent_tests tests/agent_tests.cpp)
    target_link_libraries(gtlibcpp_agent_tests PRIVATE gtlibcpp::core)
    target_include_directories(gtlibcpp_agent_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/tests
    )
    add_test(NAME gtlibcpp_agent_tests COMMAND gtlibcpp_agent_tests)

    add_executable(gtlibcpp_security_tests tests/security_tests.cpp)
    target_link_libraries(gtlibcpp_security_tests PRIVATE gtlibcpp::core)
    add_test(NAME gtlibcpp_security_tests COMMAND gtlibcpp_security_tests)
endif()

if(GTLIBCPP_BUILD_DEMOS)
    if(WIN32)
        add_library(gtlibc_legacy STATIC
            libs/gtlibc/src/GTLibc.cpp
            libs/gtlibc/src/CEParser.cpp
        )
        target_include_directories(gtlibc_legacy PUBLIC libs/gtlibc/include)
        target_link_libraries(gtlibc_legacy PUBLIC psapi)

        add_executable(IGITrainer demos/IGITrainer.cpp)
        target_link_libraries(IGITrainer PRIVATE gtlibc_legacy gtlibcpp::win32)

        add_executable(GenericTrainer demos/GenericTrainer.cpp)
        target_link_libraries(GenericTrainer PRIVATE gtlibc_legacy gtlibcpp::win32)
    else()
        message(STATUS "Demo trainers are Windows-only; skipping on ${CMAKE_SYSTEM_NAME}")
    endif()
endif()
