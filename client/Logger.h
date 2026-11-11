#pragma once

#include <fstream>
#include <mutex>
#include <string>

class Logger {
public:
    explicit Logger(std::string file_path);
    bool Initialize(std::string& error);
    void Info(const std::string& message);
    void Warn(const std::string& message);
    void Error(const std::string& message);

private:
    void Write(const char* level, const std::string& message);

    std::string file_path_;
    std::ofstream file_;
    std::mutex mutex_;
};
