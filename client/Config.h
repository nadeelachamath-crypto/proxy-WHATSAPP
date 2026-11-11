#pragma once

#include <string>

struct AppConfig {
    std::string listen_address = "127.0.0.1";
    unsigned short port = 8080;
    std::string log_file = "logs/wa-proxy.log";
    unsigned int max_connections = 64;
    unsigned int request_timeout_ms = 3000;
    unsigned int max_request_bytes = 8192;

    static AppConfig LoadFromFile(const std::string& path, bool& loaded_from_file);
};
