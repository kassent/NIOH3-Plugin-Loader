#include "LogUtils.h"
#include "FileUtils.h"
#include <cstdarg>
#include <memory>
#include <filesystem>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/rotating_file_sink.h>


std::string pluginName;
std::shared_ptr<spdlog::logger> globalLogger;

void initLogger(const char* a_pluginName) {
    pluginName = a_pluginName;
    std::string logFilePath = FileUtils::GetDocumentsDirectory().data() + std::string("\\") + a_pluginName + std::string(".log");
    // Create a file rotating logger with 5 MB size max and 3 rotated files
    auto max_size = 1048576 * 5;
    auto max_files = 1;
    globalLogger = spdlog::rotating_logger_mt("logger", logFilePath, max_size, max_files);
    globalLogger->flush_on(spdlog::level::debug);
    globalLogger->set_level(spdlog::level::info);
    globalLogger->set_pattern("[%Y-%m-%d %H:%M:%S.%e][%l][%t] %v");
}

void _MESSAGE(const char* fmt, ...) {
    if (fmt == nullptr) {
        return;
    }

    va_list args;
    va_start(args, fmt);
    auto len = vsnprintf(nullptr, 0, fmt, args);
    if (len <= 0) {
        return;
    }
    std::string buffer;
    buffer.resize(len + 1, 0);
    vsnprintf(buffer.data(), buffer.size(), fmt, args);
    va_end(args);

    if (globalLogger) {
        globalLogger->info(buffer);
    }
}
