#include "gtlibcpp/log.hpp"
int main() {
    gtlibcpp::Logger::instance().set_min_level(gtlibcpp::LogLevel::warn);
    return 0;
}
