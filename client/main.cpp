#include "Config.h"
#include "ConnectionManager.h"
#include "Logger.h"
#include "ProxyServer.h"

#include <atomic>
#include <iostream>
#include <stdexcept>
#include <string>

#ifdef _WIN32
    #include <windows.h>
#else
    #include <csignal>
#endif

static_assert(std::atomic_bool::is_always_lock_free,
              "The shutdown signal flag must be lock-free.");

namespace {
std::atomic_bool g_shutdown_requested{false};

#ifdef _WIN32
BOOL WINAPI ConsoleHandler(DWORD event_type) {
    switch (event_type) {
        case CTRL_C_EVENT:
        case CTRL_BREAK_EVENT:
        case CTRL_CLOSE_EVENT:
        case CTRL_SHUTDOWN_EVENT:
            g_shutdown_requested.store(true, std::memory_order_relaxed);
            return TRUE;
        default:
            return FALSE;
    }
}
#else
void SignalHandler(int) {
    g_shutdown_requested.store(true, std::memory_order_relaxed);
}
#endif

void PrintUsage() {
    std::cout << "WA Proxy v1.0.0 - authenticated TLS/VPS proxy\n\n"
              << "Usage: WAProxy.exe [--config PATH] [--help]\n\n"
              << "Default endpoint: 127.0.0.1:8080\n"
              << "Supports HTTPS CONNECT with optional mutual-TLS VPS transport. Remote mode fails closed.\n";
}
} // namespace

int main(int argc, char* argv[]) {
    std::string config_path = "config/client.json";
    for (int index = 1; index < argc; ++index) {
        const std::string argument = argv[index];
        if (argument == "--help" || argument == "-h") {
            PrintUsage();
            return 0;
        }
        if (argument == "--config") {
            if (index + 1 >= argc) {
                std::cerr << "Error: --config requires a file path.\n";
                return 2;
            }
            config_path = argv[++index];
            continue;
        }
        std::cerr << "Unknown argument: " << argument << "\n\n";
        PrintUsage();
        return 2;
    }

    try {
        bool loaded_from_file = false;
        const AppConfig config = AppConfig::LoadFromFile(config_path, loaded_from_file);
        if (!loaded_from_file) {
            throw std::runtime_error("Client configuration not found. Copy config/client.example.json to config/client.json, then fill in the VPS and certificate settings. WA Proxy will not silently start in direct mode.");
        }
        Logger logger(config.log_file);
        std::string logger_error;
        if (!logger.Initialize(logger_error)) {
            std::cerr << "Warning: " << logger_error << " (continuing with console logging only).\n";
        }
        logger.Info("WA Proxy v1.0.0 starting.");
        logger.Info("Loaded configuration from " + config_path + ".");

#ifdef _WIN32
        if (!SetConsoleCtrlHandler(ConsoleHandler, TRUE)) logger.Warn("Could not register the Windows console shutdown handler.");
#else
        std::signal(SIGINT, SignalHandler);
        std::signal(SIGTERM, SignalHandler);
#endif

        ConnectionManager connections;
        ProxyServer server(config, logger, connections);
        const int result = server.Run(g_shutdown_requested);
#ifdef _WIN32
        SetConsoleCtrlHandler(ConsoleHandler, FALSE);
#endif
        return result;
    } catch (const std::exception& exception) {
        std::cerr << "Fatal error: " << exception.what() << '\n';
        return 1;
    } catch (...) {
        std::cerr << "Fatal error: unknown exception.\n";
        return 1;
    }
}
