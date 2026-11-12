#pragma once

#include "Config.h"
#include "ConnectionManager.h"
#include "Logger.h"
#include "../common/Socket.h"

#include <atomic>
#include <memory>
#include <string>
#include <thread>
#include <vector>

class ProxyServer {
public:
    ProxyServer(const AppConfig& config, Logger& logger, ConnectionManager& connections);
    ~ProxyServer();

    ProxyServer(const ProxyServer&) = delete;
    ProxyServer& operator=(const ProxyServer&) = delete;

    int Run(const std::atomic_bool& shutdown_requested);
    void RequestStop();

private:
    struct Worker {
        std::thread thread;
        std::shared_ptr<std::atomic_bool> finished;
    };

    bool OpenListener();
    void AcceptClient(WaSocket client_socket, const std::string& peer,
                      const std::atomic_bool& shutdown_requested);
    void HandleClient(WaSocket client_socket, const std::string& peer,
                      const std::atomic_bool& shutdown_requested);
    void ReapFinishedWorkers(bool join_all);
    void CloseListener();

    AppConfig config_;
    Logger& logger_;
    ConnectionManager& connections_;
    WaSocket listener_ = WA_INVALID_SOCKET;
    std::atomic_bool stop_requested_{false};
#ifdef _WIN32
    bool socket_library_initialized_ = false;
#endif
    std::vector<Worker> workers_;
};
