#include "Config.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <regex>
#include <sstream>
#include <stdexcept>

namespace {
std::string ReadFile(const std::string& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) return {};
    std::ostringstream contents;
    contents << input.rdbuf();
    if (input.bad()) throw std::runtime_error("Failed while reading configuration file: " + path);
    return contents.str();
}

std::string ReadString(const std::string& json, const std::string& key,
                       const std::string& fallback) {
    const std::regex expression("\\\"" + key + "\\\"\\s*:\\s*\\\"([^\\\"]*)\\\"");
    std::smatch match;
    return std::regex_search(json, match, expression) ? match[1].str() : fallback;
}

unsigned int ReadUnsigned(const std::string& json, const std::string& key,
                          unsigned int fallback) {
    const std::regex expression("\\\"" + key + "\\\"\\s*:\\s*([0-9]+)");
    std::smatch match;
    if (!std::regex_search(json, match, expression)) return fallback;
    try {
        const auto value = std::stoul(match[1].str());
        if (value > 0xFFFFFFFFUL) throw std::runtime_error("value too large");
        return static_cast<unsigned int>(value);
    } catch (const std::exception&) {
        throw std::runtime_error("Invalid numeric configuration value: " + key);
    }
}

bool ReadBool(const std::string& json, const std::string& key, bool fallback) {
    const std::regex expression("\\\"" + key + "\\\"\\s*:\\s*(true|false)", std::regex::icase);
    std::smatch match;
    if (!std::regex_search(json, match, expression)) return fallback;
    std::string value = match[1].str();
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char character) { return static_cast<char>(std::tolower(character)); });
    return value == "true";
}
} // namespace

AppConfig AppConfig::LoadFromFile(const std::string& path, bool& loaded_from_file) {
    AppConfig config;
    const std::string json = ReadFile(path);
    loaded_from_file = !json.empty();
    if (!loaded_from_file) return config;

    config.listen_address = ReadString(json, "listen_address", config.listen_address);
    config.log_file = ReadString(json, "log_file", config.log_file);
    config.max_connections = ReadUnsigned(json, "max_connections", config.max_connections);
    config.request_timeout_ms = ReadUnsigned(json, "request_timeout_ms", config.request_timeout_ms);
    config.max_request_bytes = ReadUnsigned(json, "max_request_bytes", config.max_request_bytes);
    config.connect_timeout_ms = ReadUnsigned(json, "connect_timeout_ms", config.connect_timeout_ms);
    config.tunnel_idle_timeout_ms = ReadUnsigned(json, "tunnel_idle_timeout_ms", config.tunnel_idle_timeout_ms);
    config.allow_non_443_ports = ReadBool(json, "allow_non_443_ports", config.allow_non_443_ports);
    config.remote_tls_enabled = ReadBool(json, "remote_tls_enabled", config.remote_tls_enabled);
    config.remote_host = ReadString(json, "remote_host", config.remote_host);
    config.remote_server_name = ReadString(json, "remote_server_name", config.remote_server_name);
    config.ca_certificate = ReadString(json, "ca_certificate", config.ca_certificate);
    config.client_certificate = ReadString(json, "client_certificate", config.client_certificate);
    config.client_private_key = ReadString(json, "client_private_key", config.client_private_key);
    const unsigned int remote_port_value = ReadUnsigned(json, "remote_port", config.remote_port);
    if (remote_port_value < 1U || remote_port_value > 65535U) {
        throw std::runtime_error("remote_port must be between 1 and 65535.");
    }
    config.remote_port = static_cast<unsigned short>(remote_port_value);

    const unsigned int parsed_port = ReadUnsigned(json, "port", config.port);
    if (config.listen_address != "127.0.0.1") {
        throw std::runtime_error("WA Proxy v1.0 only supports listen_address=\"127.0.0.1\".");
    }
    if (parsed_port < 1024U || parsed_port > 65535U) {
        throw std::runtime_error("port must be between 1024 and 65535.");
    }
    config.port = static_cast<unsigned short>(parsed_port);
    if (config.max_connections < 1U || config.max_connections > 256U) {
        throw std::runtime_error("max_connections must be between 1 and 256.");
    }
    if (config.request_timeout_ms < 500U || config.request_timeout_ms > 30000U) {
        throw std::runtime_error("request_timeout_ms must be between 500 and 30000.");
    }
    if (config.max_request_bytes < 1024U || config.max_request_bytes > 65536U) {
        throw std::runtime_error("max_request_bytes must be between 1024 and 65536.");
    }
    if (config.connect_timeout_ms < 500U || config.connect_timeout_ms > 30000U) {
        throw std::runtime_error("connect_timeout_ms must be between 500 and 30000.");
    }
    if (config.tunnel_idle_timeout_ms != 0U &&
        (config.tunnel_idle_timeout_ms < 10000U || config.tunnel_idle_timeout_ms > 86400000U)) {
        throw std::runtime_error("tunnel_idle_timeout_ms must be 0 or between 10000 and 86400000.");
    }
    if (config.log_file.empty()) throw std::runtime_error("log_file cannot be empty.");
    if (config.remote_tls_enabled) {
        if (config.remote_host.empty() || config.ca_certificate.empty() ||
            config.client_certificate.empty() || config.client_private_key.empty()) {
            throw std::runtime_error("remote TLS requires remote_host, ca_certificate, client_certificate and client_private_key.");
        }
        if (config.remote_server_name.empty()) config.remote_server_name = config.remote_host;
        if (config.allow_non_443_ports) {
            throw std::runtime_error("remote TLS mode permits CONNECT port 443 only; allow_non_443_ports must be false.");
        }
    }
    return config;
}
