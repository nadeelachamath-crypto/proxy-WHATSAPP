#include "Config.h"

#include <fstream>
#include <regex>
#include <sstream>
#include <stdexcept>

namespace {
std::string ReadFile(const std::string& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        return {};
    }

    std::ostringstream contents;
    contents << input.rdbuf();
    if (input.bad()) {
        throw std::runtime_error("Failed while reading configuration file: " + path);
    }
    return contents.str();
}

std::string ReadString(const std::string& json, const std::string& key,
                       const std::string& fallback) {
    const std::regex expression("\\\"" + key + "\\\"\\s*:\\s*\\\"([^\\\"]*)\\\"");
    std::smatch match;
    if (std::regex_search(json, match, expression)) {
        return match[1].str();
    }
    return fallback;
}

unsigned int ReadUnsigned(const std::string& json, const std::string& key,
                          unsigned int fallback) {
    const std::regex expression("\\\"" + key + "\\\"\\s*:\\s*([0-9]+)");
    std::smatch match;
    if (!std::regex_search(json, match, expression)) {
        return fallback;
    }

    try {
        const auto value = std::stoul(match[1].str());
        if (value > 0xFFFFFFFFUL) {
            throw std::runtime_error("Configuration value is too large: " + key);
        }
        return static_cast<unsigned int>(value);
    } catch (const std::exception&) {
        throw std::runtime_error("Invalid numeric configuration value: " + key);
    }
}
} // namespace

AppConfig AppConfig::LoadFromFile(const std::string& path, bool& loaded_from_file) {
    AppConfig config;
    const std::string json = ReadFile(path);
    loaded_from_file = !json.empty();
    if (!loaded_from_file) {
        return config;
    }

    config.listen_address = ReadString(json, "listen_address", config.listen_address);
    config.port = static_cast<unsigned short>(ReadUnsigned(json, "port", config.port));
    config.log_file = ReadString(json, "log_file", config.log_file);
    config.max_connections = ReadUnsigned(json, "max_connections", config.max_connections);
    config.request_timeout_ms = ReadUnsigned(json, "request_timeout_ms", config.request_timeout_ms);
    config.max_request_bytes = ReadUnsigned(json, "max_request_bytes", config.max_request_bytes);

    // v0.1 is intentionally loopback-only. Do not allow a config file to expose it to a LAN.
    if (config.listen_address != "127.0.0.1") {
        throw std::runtime_error("v0.1 only supports listen_address=\"127.0.0.1\".");
    }
    const unsigned int parsed_port = ReadUnsigned(json, "port", 8080);
    if (parsed_port < 1024 || parsed_port > 65535) {
        throw std::runtime_error("port must be between 1024 and 65535 in v0.1.");
    }
    config.port = static_cast<unsigned short>(parsed_port);

    if (config.max_connections < 1 || config.max_connections > 256) {
        throw std::runtime_error("max_connections must be between 1 and 256.");
    }
    if (config.request_timeout_ms < 500 || config.request_timeout_ms > 15000) {
        throw std::runtime_error("request_timeout_ms must be between 500 and 15000.");
    }
    if (config.max_request_bytes < 1024 || config.max_request_bytes > 65536) {
        throw std::runtime_error("max_request_bytes must be between 1024 and 65536.");
    }
    if (config.log_file.empty()) {
        throw std::runtime_error("log_file cannot be empty.");
    }

    return config;
}
