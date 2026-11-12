#include "RemoteTls.h"

#include "../common/Socket.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <sstream>

#include <openssl/err.h>
#include <openssl/x509v3.h>

#ifdef _WIN32
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#endif

#ifdef _WIN32
#include <winsock2.h>
#else
#include <sys/time.h>
#endif

namespace {

bool IsIpLiteral(const std::string& value) {
    in_addr ipv4{};
    in6_addr ipv6{};
#ifdef _WIN32
    return InetPtonA(AF_INET, value.c_str(), &ipv4) == 1 ||
           InetPtonA(AF_INET6, value.c_str(), &ipv6) == 1;
#else
    return inet_pton(AF_INET, value.c_str(), &ipv4) == 1 ||
           inet_pton(AF_INET6, value.c_str(), &ipv6) == 1;
#endif
}

bool WriteSslAll(SSL* ssl, const void* data, std::size_t size, std::string& error) {
    const auto* bytes = static_cast<const char*>(data);
    std::size_t offset = 0;
    while (offset < size) {
        const int amount = static_cast<int>(std::min<std::size_t>(size - offset, 16U * 1024U));
        const int result = SSL_write(ssl, bytes + offset, amount);
        if (result <= 0) {
            const int ssl_error = SSL_get_error(ssl, result);
            error = ssl_error == SSL_ERROR_WANT_READ || ssl_error == SSL_ERROR_WANT_WRITE
                ? "TLS stream is not ready for writing" : wa::OpenSslError();
            return false;
        }
        offset += static_cast<std::size_t>(result);
    }
    return true;
}

bool ReadSslLine(SSL* ssl, std::string& line, std::string& error) {
    line.clear();
    while (line.size() < 512U) {
        char character = 0;
        const int result = SSL_read(ssl, &character, 1);
        if (result == 1) {
            if (character == '\n') return true;
            if (character != '\r') line.push_back(character);
        } else {
            const int ssl_error = SSL_get_error(ssl, result);
            error = ssl_error == SSL_ERROR_ZERO_RETURN ? "remote server closed during protocol negotiation" : wa::OpenSslError();
            return false;
        }
    }
    error = "remote protocol response exceeded 512 bytes";
    return false;
}
} // namespace

RemoteTlsSession::~RemoteTlsSession() {
    if (ssl_ != nullptr) SSL_free(ssl_);
    if (socket_ != WA_INVALID_SOCKET) wa::CloseSocket(socket_);
    if (context_ != nullptr) SSL_CTX_free(context_);
}

bool RemoteTlsSession::WriteAll(const void* data, std::size_t size, std::string& error) {
    return WriteSslAll(ssl_, data, size, error);
}

bool RemoteTlsSession::Open(const AppConfig& config, const std::string& target_host,
                            unsigned short target_port, std::string& error) {
    context_ = SSL_CTX_new(TLS_client_method());
    if (context_ == nullptr) {
        error = "could not create TLS client context: " + wa::OpenSslError();
        return false;
    }
    SSL_CTX_set_min_proto_version(context_, TLS1_2_VERSION);
    SSL_CTX_set_options(context_, SSL_OP_NO_COMPRESSION | SSL_OP_NO_RENEGOTIATION);
    SSL_CTX_set_verify(context_, SSL_VERIFY_PEER, nullptr);
    if (SSL_CTX_load_verify_locations(context_, config.ca_certificate.c_str(), nullptr) != 1) {
        error = "could not load remote CA certificate: " + wa::OpenSslError();
        return false;
    }
    if (SSL_CTX_use_certificate_chain_file(context_, config.client_certificate.c_str()) != 1) {
        error = "could not load client certificate: " + wa::OpenSslError();
        return false;
    }
    if (SSL_CTX_use_PrivateKey_file(context_, config.client_private_key.c_str(), SSL_FILETYPE_PEM) != 1) {
        error = "could not load client private key: " + wa::OpenSslError();
        return false;
    }
    if (SSL_CTX_check_private_key(context_) != 1) {
        error = "client certificate and private key do not match";
        return false;
    }

    socket_ = wa::ConnectTcp(config.remote_host, config.remote_port, config.connect_timeout_ms, error);
    if (socket_ == WA_INVALID_SOCKET) return false;
#ifdef _WIN32
    const DWORD timeout = static_cast<DWORD>(config.connect_timeout_ms);
    setsockopt(socket_, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeout), sizeof(timeout));
    setsockopt(socket_, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char*>(&timeout), sizeof(timeout));
#else
    timeval timeout{};
    timeout.tv_sec = static_cast<long>(config.connect_timeout_ms / 1000U);
    timeout.tv_usec = static_cast<long>((config.connect_timeout_ms % 1000U) * 1000U);
    setsockopt(socket_, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    setsockopt(socket_, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
#endif

    ssl_ = SSL_new(context_);
    if (ssl_ == nullptr) {
        error = "could not create TLS session: " + wa::OpenSslError();
        return false;
    }
    if (SSL_set_fd(ssl_, static_cast<int>(socket_)) != 1) {
        error = "could not attach TLS to socket: " + wa::OpenSslError();
        return false;
    }
    const std::string verify_name = config.remote_server_name.empty() ? config.remote_host : config.remote_server_name;
    int identity_result = 0;
    if (IsIpLiteral(verify_name)) {
        // IP subjectAltName entries must be checked as IP addresses, not DNS names.
        identity_result = X509_VERIFY_PARAM_set1_ip_asc(SSL_get0_param(ssl_), verify_name.c_str());
    } else {
        identity_result = SSL_set_tlsext_host_name(ssl_, verify_name.c_str()) == 1
            ? SSL_set1_host(ssl_, verify_name.c_str()) : 0;
    }
    if (identity_result != 1) {
        error = "could not configure TLS server identity verification: " + wa::OpenSslError();
        return false;
    }
    if (SSL_connect(ssl_) != 1) {
        error = "TLS connection/authentication failed: " + wa::OpenSslError();
        return false;
    }
    if (SSL_get_verify_result(ssl_) != X509_V_OK) {
        error = "remote server certificate verification failed";
        return false;
    }

    const std::string command = "WAPX/1 CONNECT " + target_host + " " + std::to_string(target_port) + "\r\n";
    if (!WriteSslAll(ssl_, command.data(), command.size(), error)) return false;
    std::string response;
    if (!ReadSslLine(ssl_, response, error)) return false;
    if (response != "WAPX/1 200 OK") {
        error = "remote server rejected tunnel request: " + response;
        return false;
    }
#ifdef _WIN32
    const DWORD no_timeout = 0;
    setsockopt(socket_, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&no_timeout), sizeof(no_timeout));
    setsockopt(socket_, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char*>(&no_timeout), sizeof(no_timeout));
#else
    timeval no_timeout{};
    setsockopt(socket_, SOL_SOCKET, SO_RCVTIMEO, &no_timeout, sizeof(no_timeout));
    setsockopt(socket_, SOL_SOCKET, SO_SNDTIMEO, &no_timeout, sizeof(no_timeout));
#endif
    return true;
}
