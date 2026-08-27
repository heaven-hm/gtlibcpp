cmake_minimum_required(VERSION 3.16)
project(gtlibcpp
    VERSION 1.0.0
    DESCRIPTION "Reliable, capability-scoped memory operations for authorized offline trainers and local automation agents"
    LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPORTS ON)

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
    add_executable(gtlibcpp_core_tests tests/core_tests.cpp)
    target_link_libraries(gtlibcpp_core_tests PRIVATE gtlibcpp::core)
    add_test(NAME gtlibcpp_core_tests COMMAND gtlibcpp_core_tests)

    add_executable(gtlibcpp_parser_tests tests/parser_tests.cpp)
    target_link_libraries(gtlibcpp_parser_tests PRIVATE gtlibcpp::core)
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
        add_executable(IGITrainer IGITrainer.cpp)
        target_link_libraries(IGITrainer PRIVATE gtlibcpp::win32)
        add_executable(GenericTrainer GenericTrainer.cpp)
        target_link_libraries(GenericTrainer PRIVATE gtlibcpp::win32)
    else()
        message(STATUS "Demo trainers are Windows-only; skipping on ${CMAKE_SYSTEM_NAME}")
    endif()
endif()
