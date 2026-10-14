cmake_minimum_required(VERSION 3.16)
project(gtlibcpp_examples CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT TARGET gtlibcpp_core)
    find_package(gtlibcpp 1.0 REQUIRED CONFIG)
    if(TARGET gtlibcpp::core)
        set(GTLIBCPP_CORE_TARGET gtlibcpp::core)
    else()
        set(GTLIBCPP_CORE_TARGET gtlibcpp::gtlibcpp_core)
    endif()
    if(TARGET gtlibcpp::win32)
        set(GTLIBCPP_WIN32_TARGET gtlibcpp::win32)
    else()
        set(GTLIBCPP_WIN32_TARGET gtlibcpp::gtlibcpp_win32)
    endif()
else()
    set(GTLIBCPP_CORE_TARGET gtlibcpp_core)
    set(GTLIBCPP_WIN32_TARGET gtlibcpp::win32)
endif()

# End-to-End Demo Projects (Cross-Platform)
set(DEMO_TARGETS
    demo_game_trainer
    demo_freeze_engine
    demo_cheat_parser
    demo_agent_service
    demo_policy_gate
)

foreach(DEMO IN LISTS DEMO_TARGETS)
    add_executable(${DEMO} ${DEMO}.cpp)
    target_link_libraries(${DEMO} PRIVATE ${GTLIBCPP_CORE_TARGET})
    target_include_directories(${DEMO} PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../include>
    )
    # Register each demo as an independent test in CTest
    if(GTLIBCPP_BUILD_TESTS)
        add_test(NAME e2e_${DEMO} COMMAND ${DEMO})
    endif()
endforeach()

# Windows-specific demo
if(WIN32)
    add_executable(inspect_and_read inspect_and_read.cpp)
    target_link_libraries(inspect_and_read PRIVATE
        ${GTLIBCPP_CORE_TARGET}
        ${GTLIBCPP_WIN32_TARGET}
    )
endif()
