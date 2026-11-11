#include "Logger.h"

#include <chrono>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <utility>

Logger::Logger(std::string file_path) : file_path_(std::move(file_path)) {}

bool Logger::Initialize(std::string& error) {
    try {
        const std::filesystem::path path(file_path_);
        if (path.has_parent_path()) {
            std::filesystem::create_directories(path.parent_path());
        }
        file_.open(path, std::ios::out | std::ios::app);
        if (!file_.is_open()) {
            error = "Could not open log file: " + file_path_;
            return false;
        }
        return true;
    } catch (const std::exception& exception) {
        error = exception.what();
        return false;
    }
}

void Logger::Info(const std::string& message) { Write("INFO", message); }
void Logger::Warn(const std::string& message) { Write("WARN", message); }
void Logger::Error(const std::string& message) { Write("ERROR", message); }

void Logger::Write(const char* level, const std::string& message) {
    const auto now = std::chrono::system_clock::now();
    const auto time = std::chrono::system_clock::to_time_t(now);
    std::tm local_time{};
#ifdef _WIN32
    localtime_s(&local_time, &time);
#else
    localtime_r(&time, &local_time);
#endif
    std::ostringstream line;
    line << std::put_time(&local_time, "%Y-%m-%d %H:%M:%S")
         << " [" << level << "] " << message;

    std::lock_guard<std::mutex> lock(mutex_);
    std::cout << line.str() << std::endl;
    if (file_.is_open()) {
        file_ << line.str() << '\n';
        file_.flush();
    }
}
