cmake_minimum_required(VERSION 3.16)
project(gtlibcpp_examples CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(gtlibcpp 1.0 REQUIRED CONFIG)

if(WIN32)
    add_executable(inspect_and_read inspect_and_read.cpp)
    add_executable(discover_and_read discover_and_read.cpp)
    add_executable(read_write_restore read_write_restore.cpp)
    foreach(example_target IN ITEMS inspect_and_read discover_and_read read_write_restore)
        target_link_libraries(${example_target} PRIVATE gtlibcpp::gtlibcpp_core gtlibcpp::gtlibcpp_win32)
    endforeach()
endif()

add_executable(assaultcube_trainer assaultcube_trainer.cpp)
target_link_libraries(assaultcube_trainer PRIVATE gtlibcpp::gtlibcpp_core)
if(WIN32)
    target_link_libraries(assaultcube_trainer PRIVATE gtlibcpp::gtlibcpp_win32)
endif()
if(APPLE)
    add_custom_command(TARGET assaultcube_trainer POST_BUILD
        COMMAND codesign -s - -f $<TARGET_FILE:assaultcube_trainer>
        COMMENT "Codesigning assaultcube_trainer"
    )
endif()
