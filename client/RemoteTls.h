#pragma once

#include "Config.h"
#include "../common/Socket.h"

#include <openssl/ssl.h>
#include <string>

class RemoteTlsSession {
public:
    RemoteTlsSession() = default;
    ~RemoteTlsSession();
    RemoteTlsSession(const RemoteTlsSession&) = delete;
    RemoteTlsSession& operator=(const RemoteTlsSession&) = delete;

    bool Open(const AppConfig& config, const std::string& target_host,
              unsigned short target_port, std::string& error);
    bool WriteAll(const void* data, std::size_t size, std::string& error);
    SSL* GetSsl() const { return ssl_; }

private:
    SSL_CTX* context_ = nullptr;
    SSL* ssl_ = nullptr;
    WaSocket socket_ = WA_INVALID_SOCKET;
};
