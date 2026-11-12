#include "server.h"

#include <atomic>
#include <csignal>
#include <iostream>
#include <string>

namespace {
std::atomic_bool g_shutdown{false};
void HandleSignal(int) { g_shutdown.store(true, std::memory_order_relaxed); }
}

int main(int argc, char* argv[]) {
    std::string config_path = "config/server.example.json";
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--help" || arg == "-h") {
            std::cout << "WA Proxy VPS relay v1.0.0\nUsage: wapserver [--config PATH]\n";
            return 0;
        }
        if (arg == "--config" && i + 1 < argc) { config_path = argv[++i]; continue; }
        std::cerr << "Unknown or incomplete argument: " << arg << '\n';
        return 2;
    }
    ServerConfig config;
    std::string error;
    if (!LoadServerConfig(config_path, config, error)) {
        std::cerr << "Configuration error: " << error << '\n';
        return 2;
    }
    std::string socket_error;
    if (!wa::InitializeSockets(socket_error)) {
        std::cerr << "Socket initialization failed: " << socket_error << '\n';
        return 1;
    }
    std::signal(SIGINT, HandleSignal);
    std::signal(SIGTERM, HandleSignal);
    WapServer server(config);
    const int result = server.Run(g_shutdown);
    wa::CleanupSockets();
    return result;
}
