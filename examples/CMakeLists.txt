cmake_minimum_required(VERSION 3.16)
project(gtlibcpp_examples CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# The example links against the cross-platform gtlibcpp::gtlibcpp_core
# target. The Windows-only backend (gtlibcpp::gtlibcpp_win32) is also
# available; the example build links it conditionally.
find_package(gtlibcpp 1.0 REQUIRED CONFIG)

if(WIN32)
    add_executable(inspect_and_read inspect_and_read.cpp)
    target_link_libraries(inspect_and_read PRIVATE
        gtlibcpp::gtlibcpp_core
        gtlibcpp::gtlibcpp_win32
    )
endif()
