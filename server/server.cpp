#include "server.h"

#include "../common/TlsRelay.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cstring>
#include <cstdint>
#include <ctime>
#include <fstream>
#include <iostream>
#include <regex>
#include <sstream>
#include <utility>

#include <openssl/err.h>
#include <openssl/x509.h>

#ifdef _WIN32
#error "The WA Proxy VPS relay server is intended to run on Linux."
#else
#include <arpa/inet.h>
#include <cerrno>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace {
std::string ReadFile(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) return {};
    std::ostringstream data;
    data << file.rdbuf();
    return data.str();
}
std::string JsonString(const std::string& json, const std::string& key, const std::string& fallback) {
    const std::regex pattern("\\\"" + key + "\\\"\\s*:\\s*\\\"([^\\\"]*)\\\"");
    std::smatch match;
    return std::regex_search(json, match, pattern) ? match[1].str() : fallback;
}
unsigned int JsonUnsigned(const std::string& json, const std::string& key, unsigned int fallback) {
    const std::regex pattern("\\\"" + key + "\\\"\\s*:\\s*([0-9]+)");
    std::smatch match;
    if (!std::regex_search(json, match, pattern)) return fallback;
    const auto value = std::stoul(match[1].str());
    if (value > 0xFFFFFFFFUL) throw std::runtime_error("numeric configuration value is too large: " + key);
    return static_cast<unsigned int>(value);
}

bool IsValidHost(const std::string& host) {
    if (host.empty() || host.size() > 253 || host.find_first_of(" \t\r\n/@") != std::string::npos) return false;
    for (char raw_character : host) {
        const auto c = static_cast<unsigned char>(raw_character);
        if (!(std::isalnum(c) || c == '.' || c == '-' || c == ':')) return false;
    }
    return true;
}

bool IsPublicAddress(const sockaddr* address) {
    if (address->sa_family == AF_INET) {
        const auto* a = reinterpret_cast<const sockaddr_in*>(address);
        const std::uint32_t ip = ntohl(a->sin_addr.s_addr);
        const unsigned int first = (ip >> 24U) & 0xFFU;
        const unsigned int second = (ip >> 16U) & 0xFFU;
        if (first == 0U || first == 10U || first == 127U || first >= 224U) return false;
        if (first == 100U && second >= 64U && second <= 127U) return false; // CGNAT
        if (first == 169U && second == 254U) return false;
        if (first == 172U && second >= 16U && second <= 31U) return false;
        if (first == 192U && second == 168U) return false;
        if (first == 192U && second == 0U) return false; // IETF protocol assignments (conservative)
        if (first == 192U && second == 88U && ((ip >> 8U) & 0xFFU) == 99U) return false; // 6to4 relay anycast
        if (first == 198U && (second == 18U || second == 19U)) return false;
        if (first == 198U && second == 51U && ((ip >> 8U) & 0xFFU) == 100U) return false;
        if (first == 203U && second == 0U && ((ip >> 8U) & 0xFFU) == 113U) return false;
        return true;
    }
    if (address->sa_family == AF_INET6) {
        const auto* a = reinterpret_cast<const sockaddr_in6*>(address);
        const auto* b = reinterpret_cast<const unsigned char*>(&a->sin6_addr);
        bool all_zero = true;
        for (int i = 0; i < 16; ++i) if (b[i] != 0) all_zero = false;
        if (all_zero) return false;
        bool loopback = true;
        for (int i = 0; i < 15; ++i) if (b[i] != 0) loopback = false;
        if (loopback && b[15] == 1) return false;
        // Only global-unicast IPv6 (2000::/3) is eligible. This also blocks
        // mapped, transition, link-local, ULA, multicast and reserved ranges.
        if ((b[0] & 0xE0U) != 0x20U) return false;
        if (b[0] == 0x20U && b[1] == 0x01U && b[2] == 0x0DU && b[3] == 0xB8U) return false; // documentation
        if (b[0] == 0x20U && b[1] == 0x01U && b[2] <= 0x01U) return false; // special-purpose 2001::/23
        if (b[0] == 0x20U && b[1] == 0x02U) return false; // 6to4 transition (2002::/16)
        return true;
    }
    return false;
}

WaSocket ConnectResolvedPublic(const std::string& host, unsigned short port,
                               unsigned int timeout_ms, std::string& error) {
    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;
    addrinfo* list = nullptr;
    const std::string service = std::to_string(port);
    const int rc = getaddrinfo(host.c_str(), service.c_str(), &hints, &list);
    if (rc != 0) {
        error = std::string("remote DNS lookup failed: ") + gai_strerror(rc);
        return WA_INVALID_SOCKET;
    }
    bool saw_address = false;
    for (addrinfo* item = list; item != nullptr; item = item->ai_next) {
        saw_address = true;
        if (!IsPublicAddress(item->ai_addr)) {
            error = "destination resolved to a private, local, link-local or reserved IP address";
            freeaddrinfo(list);
            return WA_INVALID_SOCKET;
        }
    }
    if (!saw_address) {
        error = "destination has no usable DNS records";
        freeaddrinfo(list);
        return WA_INVALID_SOCKET;
    }

    WaSocket connected = WA_INVALID_SOCKET;
    for (addrinfo* item = list; item != nullptr; item = item->ai_next) {
        const WaSocket candidate = socket(item->ai_family, item->ai_socktype, item->ai_protocol);
        if (candidate == WA_INVALID_SOCKET) continue;
        const int flags = fcntl(candidate, F_GETFL, 0);
        if (flags < 0 || fcntl(candidate, F_SETFL, flags | O_NONBLOCK) < 0) {
            wa::CloseSocket(candidate);
            continue;
        }
        const int result = connect(candidate, item->ai_addr, item->ai_addrlen);
        if (result != 0 && errno != EINPROGRESS) {
            wa::CloseSocket(candidate);
            continue;
        }
        if (result != 0) {
            fd_set writable;
            FD_ZERO(&writable);
            FD_SET(candidate, &writable);
            timeval timeout{};
            timeout.tv_sec = static_cast<long>(timeout_ms / 1000U);
            timeout.tv_usec = static_cast<long>((timeout_ms % 1000U) * 1000U);
            const int ready = select(candidate + 1, nullptr, &writable, nullptr, &timeout);
            if (ready <= 0) {
                wa::CloseSocket(candidate);
                continue;
            }
            int socket_error = 0;
            socklen_t length = sizeof(socket_error);
            if (getsockopt(candidate, SOL_SOCKET, SO_ERROR, &socket_error, &length) != 0 || socket_error != 0) {
                wa::CloseSocket(candidate);
                continue;
            }
        }
        if (fcntl(candidate, F_SETFL, flags) < 0) {
            wa::CloseSocket(candidate);
            continue;
        }
        connected = candidate;
        break;
    }
    freeaddrinfo(list);
    if (connected == WA_INVALID_SOCKET) error = "could not connect to the destination within timeout";
    return connected;
}

bool ReadTlsLine(SSL* ssl, std::string& line, std::string& error) {
    line.clear();
    while (line.size() < 512U) {
        char c = 0;
        const int result = SSL_read(ssl, &c, 1);
        if (result == 1) {
            if (c == '\n') return true;
            if (c != '\r') line.push_back(c);
        } else {
            const int ssl_error = SSL_get_error(ssl, result);
            error = ssl_error == SSL_ERROR_ZERO_RETURN ? "client closed during request" : wa::OpenSslError();
            return false;
        }
    }
    error = "control request is too long";
    return false;
}

bool WriteTlsLine(SSL* ssl, const std::string& line) {
    std::size_t offset = 0;
    while (offset < line.size()) {
        const int result = SSL_write(ssl, line.data() + offset,
                                     static_cast<int>(std::min<std::size_t>(line.size() - offset, 4096U)));
        if (result <= 0) return false;
        offset += static_cast<std::size_t>(result);
    }
    return true;
}

std::string PeerText(const sockaddr_storage& peer) {
    char host[NI_MAXHOST]{};
    if (getnameinfo(reinterpret_cast<const sockaddr*>(&peer), sizeof(peer), host, sizeof(host),
                    nullptr, 0, NI_NUMERICHOST) == 0) return host;
    return "unknown";
}
} // namespace

bool LoadServerConfig(const std::string& path, ServerConfig& config, std::string& error) {
    const std::string json = ReadFile(path);
    if (json.empty()) {
        error = "could not read server configuration: " + path;
        return false;
    }
    try {
        config.bind_address = JsonString(json, "bind_address", config.bind_address);
        config.server_certificate = JsonString(json, "server_certificate", config.server_certificate);
        config.server_private_key = JsonString(json, "server_private_key", config.server_private_key);
        config.client_ca_certificate = JsonString(json, "client_ca_certificate", config.client_ca_certificate);
        const unsigned int port = JsonUnsigned(json, "port", config.port);
        config.max_connections = JsonUnsigned(json, "max_connections", config.max_connections);
        config.connect_timeout_ms = JsonUnsigned(json, "connect_timeout_ms", config.connect_timeout_ms);
        config.tls_handshake_timeout_ms = JsonUnsigned(json, "tls_handshake_timeout_ms", config.tls_handshake_timeout_ms);
        config.tunnel_idle_timeout_ms = JsonUnsigned(json, "tunnel_idle_timeout_ms", config.tunnel_idle_timeout_ms);
        if (port < 1 || port > 65535) throw std::runtime_error("port must be between 1 and 65535");
        config.port = static_cast<unsigned short>(port);
        if (config.max_connections < 1 || config.max_connections > 512) throw std::runtime_error("max_connections must be 1..512");
        if (config.connect_timeout_ms < 500 || config.connect_timeout_ms > 30000) throw std::runtime_error("connect_timeout_ms must be 500..30000");
        if (config.tls_handshake_timeout_ms < 1000 || config.tls_handshake_timeout_ms > 60000) throw std::runtime_error("tls_handshake_timeout_ms must be 1000..60000");
        if (config.tunnel_idle_timeout_ms != 0 && (config.tunnel_idle_timeout_ms < 10000 || config.tunnel_idle_timeout_ms > 86400000)) throw std::runtime_error("tunnel_idle_timeout_ms must be 0 or 10000..86400000");
    } catch (const std::exception& ex) {
        error = ex.what();
        return false;
    }
    return true;
}

WapServer::WapServer(ServerConfig config) : config_(std::move(config)) {}
WapServer::~WapServer() {
    RequestStop();
    if (listener_ != WA_INVALID_SOCKET) wa::CloseSocket(listener_);
    ReapWorkers(true);
    if (tls_context_ != nullptr) SSL_CTX_free(tls_context_);
}

void WapServer::Log(const char* level, const std::string& message) const {
    const auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::tm tm{};
    localtime_r(&now, &tm);
    char stamp[32]{};
    std::strftime(stamp, sizeof(stamp), "%Y-%m-%d %H:%M:%S", &tm);
    std::cerr << stamp << " [" << level << "] " << message << std::endl;
}

bool WapServer::InitializeTls(std::string& error) {
    tls_context_ = SSL_CTX_new(TLS_server_method());
    if (tls_context_ == nullptr) {
        error = "could not create TLS server context: " + wa::OpenSslError();
        return false;
    }
    SSL_CTX_set_min_proto_version(tls_context_, TLS1_2_VERSION);
    SSL_CTX_set_options(tls_context_, SSL_OP_NO_COMPRESSION | SSL_OP_NO_RENEGOTIATION);
    SSL_CTX_set_mode(tls_context_, SSL_MODE_RELEASE_BUFFERS);
    if (SSL_CTX_use_certificate_chain_file(tls_context_, config_.server_certificate.c_str()) != 1) {
        error = "could not load server certificate: " + wa::OpenSslError();
        return false;
    }
    if (SSL_CTX_use_PrivateKey_file(tls_context_, config_.server_private_key.c_str(), SSL_FILETYPE_PEM) != 1 ||
        SSL_CTX_check_private_key(tls_context_) != 1) {
        error = "could not load or validate server private key: " + wa::OpenSslError();
        return false;
    }
    if (SSL_CTX_load_verify_locations(tls_context_, config_.client_ca_certificate.c_str(), nullptr) != 1) {
        error = "could not load client CA certificate: " + wa::OpenSslError();
        return false;
    }
    SSL_CTX_set_verify(tls_context_, SSL_VERIFY_PEER | SSL_VERIFY_FAIL_IF_NO_PEER_CERT, nullptr);
    SSL_CTX_set_verify_depth(tls_context_, 3);
    return true;
}

bool WapServer::OpenListener(std::string& error) {
    listener_ = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listener_ == WA_INVALID_SOCKET) {
        error = std::string("socket failed: ") + std::strerror(errno);
        return false;
    }
    int reuse = 1;
    setsockopt(listener_, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(config_.port);
    if (inet_pton(AF_INET, config_.bind_address.c_str(), &address.sin_addr) != 1) {
        error = "bind_address must be a valid IPv4 address";
        wa::CloseSocket(listener_); listener_ = WA_INVALID_SOCKET;
        return false;
    }
    if (bind(listener_, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) != 0 ||
        listen(listener_, SOMAXCONN) != 0) {
        error = std::string("could not bind/listen: ") + std::strerror(errno);
        wa::CloseSocket(listener_); listener_ = WA_INVALID_SOCKET;
        return false;
    }
    return true;
}

int WapServer::Run(const std::atomic_bool& external_shutdown) {
    std::string error;
    if (!InitializeTls(error)) {
        Log("ERROR", error);
        return 1;
    }
    if (!OpenListener(error)) {
        Log("ERROR", error);
        return 1;
    }
    workers_.reserve(config_.max_connections);
    Log("INFO", "WA Proxy VPS relay listening on " + config_.bind_address + ":" + std::to_string(config_.port) + " (mutual TLS required; CONNECT port 443 only). ");

    while (!stop_requested_.load(std::memory_order_relaxed) &&
           !external_shutdown.load(std::memory_order_relaxed)) {
        fd_set read_set;
        FD_ZERO(&read_set);
        FD_SET(listener_, &read_set);
        timeval timeout{};
        timeout.tv_sec = 0;
        timeout.tv_usec = 250000;
        const int ready = select(listener_ + 1, &read_set, nullptr, nullptr, &timeout);
        if (ready < 0) {
            if (errno == EINTR) continue;
            Log("ERROR", "listener select failed; stopping server");
            break;
        }
        if (ready == 0) {
            ReapWorkers(false);
            continue;
        }
        sockaddr_storage peer{};
        socklen_t peer_length = sizeof(peer);
        const WaSocket client = accept(listener_, reinterpret_cast<sockaddr*>(&peer), &peer_length);
        if (client == WA_INVALID_SOCKET) {
            if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK) continue;
            Log("WARN", std::string("accept failed: ") + std::strerror(errno));
            continue;
        }
        if (workers_.size() >= config_.max_connections) {
            Log("WARN", "connection limit reached; rejected client " + PeerText(peer));
            wa::CloseSocket(client);
            continue;
        }
        const std::string peer_name = PeerText(peer);
        auto done = std::make_shared<std::atomic_bool>(false);
        try {
            workers_.push_back(Worker{});
            Worker& worker = workers_.back();
            worker.finished = done;
            worker.thread = std::thread([this, client, peer_name, done]() {
                try { HandleClient(client, peer_name); }
                catch (const std::exception& ex) { Log("WARN", "client session ended with exception: " + std::string(ex.what())); }
                catch (...) { Log("WARN", "client session ended with unknown exception"); }
                done->store(true, std::memory_order_release);
            });
        } catch (const std::exception& ex) {
            if (!workers_.empty() && !workers_.back().thread.joinable()) workers_.pop_back();
            wa::CloseSocket(client);
            Log("ERROR", "could not create client worker: " + std::string(ex.what()));
        }
        ReapWorkers(false);
    }
    RequestStop();
    if (listener_ != WA_INVALID_SOCKET) { wa::CloseSocket(listener_); listener_ = WA_INVALID_SOCKET; }
    ReapWorkers(true);
    Log("INFO", "WA Proxy VPS relay stopped cleanly");
    return 0;
}

void WapServer::RequestStop() { stop_requested_.store(true, std::memory_order_relaxed); }

void WapServer::HandleClient(WaSocket client, const std::string& peer) {
    struct SocketGuard { WaSocket s; ~SocketGuard() { wa::CloseSocket(s); } } socket_guard{client};
    timeval timeout{};
    timeout.tv_sec = static_cast<long>(config_.tls_handshake_timeout_ms / 1000U);
    timeout.tv_usec = static_cast<long>((config_.tls_handshake_timeout_ms % 1000U) * 1000U);
    setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    setsockopt(client, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
    SSL* ssl = SSL_new(tls_context_);
    if (ssl == nullptr) { Log("WARN", "could not allocate TLS session for " + peer); return; }
    struct SslGuard { SSL* s; ~SslGuard() { SSL_free(s); } } ssl_guard{ssl};
    if (SSL_set_fd(ssl, client) != 1 || SSL_accept(ssl) != 1) {
        Log("WARN", "TLS handshake rejected for " + peer + " (client certificate required)");
        return;
    }
    if (SSL_get_verify_result(ssl) != X509_V_OK) {
        Log("WARN", "client certificate verification failed for " + peer);
        return;
    }
    X509* peer_cert = SSL_get1_peer_certificate(ssl);
    if (peer_cert == nullptr) {
        Log("WARN", "client did not present a certificate: " + peer);
        return;
    }
    X509_free(peer_cert);

    std::string line;
    std::string read_error;
    if (!ReadTlsLine(ssl, line, read_error)) {
        Log("WARN", "invalid encrypted control request from " + peer + ": " + read_error);
        return;
    }
    std::istringstream command(line);
    std::string magic, verb, host, port_text, extra;
    if (!(command >> magic >> verb >> host >> port_text) || (command >> extra) ||
        magic != "WAPX/1" || verb != "CONNECT" || !IsValidHost(host)) {
        WriteTlsLine(ssl, "WAPX/1 400 BAD_REQUEST\r\n");
        Log("WARN", "malformed encrypted control request from " + peer);
        return;
    }
    unsigned int port_value = 0;
    bool valid_port = !port_text.empty() && port_text.size() <= 5U;
    for (char raw_character : port_text) {
        const auto character = static_cast<unsigned char>(raw_character);
        if (!std::isdigit(character)) {
            valid_port = false;
            break;
        }
        port_value = port_value * 10U + static_cast<unsigned int>(character - '0');
        if (port_value > 65535U) {
            valid_port = false;
            break;
        }
    }
    if (!valid_port) {
        WriteTlsLine(ssl, "WAPX/1 400 BAD_REQUEST\r\n");
        Log("WARN", "rejected malformed destination port from " + peer);
        return;
    }
    if (port_value != 443U) {
        WriteTlsLine(ssl, "WAPX/1 403 PORT_NOT_ALLOWED\r\n");
        Log("WARN", "rejected non-443 tunnel request from " + peer);
        return;
    }

    std::string connect_error;
    WaSocket target = ConnectResolvedPublic(host, static_cast<unsigned short>(port_value),
                                            config_.connect_timeout_ms, connect_error);
    if (target == WA_INVALID_SOCKET) {
        WriteTlsLine(ssl, "WAPX/1 502 UPSTREAM_UNAVAILABLE\r\n");
        Log("WARN", "remote DNS/connection failed for authenticated client " + peer + ": " + connect_error);
        return;
    }
    struct TargetGuard { WaSocket s; ~TargetGuard() { wa::CloseSocket(s); } } target_guard{target};
    if (!WriteTlsLine(ssl, "WAPX/1 200 OK\r\n")) {
        Log("WARN", "could not acknowledge tunnel to " + peer);
        return;
    }
    // Remove handshake timeouts before the long-lived tunnel begins.
    timeval no_timeout{};
    setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, &no_timeout, sizeof(no_timeout));
    setsockopt(client, SOL_SOCKET, SO_SNDTIMEO, &no_timeout, sizeof(no_timeout));
    wa::RelayStats stats;
    std::string relay_reason;
    const std::atomic_bool& stop_flag = stop_requested_;
    wa::RelayTlsAndSocket(ssl, target, config_.tunnel_idle_timeout_ms, stop_flag, stats, relay_reason);
    Log("INFO", "authenticated tunnel ended for " + peer + " (bytes up=" +
        std::to_string(stats.plain_to_tls) + ", bytes down=" + std::to_string(stats.tls_to_plain) + ", " + relay_reason + ")");
}

void WapServer::ReapWorkers(bool all) {
    auto it = workers_.begin();
    while (it != workers_.end()) {
        if (all || it->finished->load(std::memory_order_acquire)) {
            if (it->thread.joinable()) it->thread.join();
            it = workers_.erase(it);
        } else ++it;
    }
}
