#pragma once

#include <atomic>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include <openssl/ssl.h>

#include "../common/Socket.h"

struct ServerConfig {
    std::string bind_address = "0.0.0.0";
    unsigned short port = 8443;
    std::string server_certificate = "certs/server.crt";
    std::string server_private_key = "certs/server.key";
    std::string client_ca_certificate = "certs/ca.crt";
    unsigned int max_connections = 128;
    unsigned int connect_timeout_ms = 8000;
    unsigned int tls_handshake_timeout_ms = 10000;
    unsigned int tunnel_idle_timeout_ms = 300000;
};

class WapServer {
public:
    explicit WapServer(ServerConfig config);
    ~WapServer();
    WapServer(const WapServer&) = delete;
    WapServer& operator=(const WapServer&) = delete;
    int Run(const std::atomic_bool& external_shutdown);
    void RequestStop();
private:
    struct Worker {
        std::thread thread;
        std::shared_ptr<std::atomic_bool> finished;
    };
    bool InitializeTls(std::string& error);
    bool OpenListener(std::string& error);
    void HandleClient(WaSocket client, const std::string& peer);
    void ReapWorkers(bool all);
    void Log(const char* level, const std::string& message) const;
    ServerConfig config_;
    SSL_CTX* tls_context_ = nullptr;
    WaSocket listener_ = WA_INVALID_SOCKET;
    std::atomic_bool stop_requested_{false};
    std::vector<Worker> workers_;
};

bool LoadServerConfig(const std::string& path, ServerConfig& config, std::string& error);
