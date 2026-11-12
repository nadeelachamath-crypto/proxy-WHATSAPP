#include "ProxyServer.h"
#include "RemoteTls.h"
#include "../common/TlsRelay.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cctype>
#include <cstring>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>

#ifdef _WIN32
    #include <ws2tcpip.h>
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
#ifdef _WIN32
int LastSocketError() { return WSAGetLastError(); }
void CloseSocket(WaSocket socket) {
    if (socket != WA_INVALID_SOCKET) {
        shutdown(socket, SD_BOTH);
        closesocket(socket);
    }
}
bool IsInterrupted(int error) { return error == WSAEINTR; }
bool IsWouldBlock(int error) { return error == WSAEWOULDBLOCK || error == WSAEINPROGRESS; }
#else
int LastSocketError() { return errno; }
void CloseSocket(WaSocket socket) {
    if (socket != WA_INVALID_SOCKET) {
        shutdown(socket, SHUT_RDWR);
        close(socket);
    }
}
bool IsInterrupted(int error) { return error == EINTR; }
bool IsWouldBlock(int error) { return error == EAGAIN || error == EWOULDBLOCK || error == EINPROGRESS; }
#endif

std::string SocketErrorText(int error) {
    return "socket error " + std::to_string(error);
}

void SetReceiveTimeout(WaSocket socket, unsigned int milliseconds) {
#ifdef _WIN32
    const DWORD timeout = static_cast<DWORD>(milliseconds);
    setsockopt(socket, SOL_SOCKET, SO_RCVTIMEO,
               reinterpret_cast<const char*>(&timeout), sizeof(timeout));
#else
    timeval timeout{};
    timeout.tv_sec = static_cast<long>(milliseconds / 1000U);
    timeout.tv_usec = static_cast<long>((milliseconds % 1000U) * 1000U);
    setsockopt(socket, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
#endif
}

void ClearReceiveTimeout(WaSocket socket) {
#ifdef _WIN32
    const DWORD timeout = 0;
    setsockopt(socket, SOL_SOCKET, SO_RCVTIMEO,
               reinterpret_cast<const char*>(&timeout), sizeof(timeout));
#else
    timeval timeout{};
    setsockopt(socket, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
#endif
}

bool SendAll(WaSocket socket, const char* data, std::size_t size,
             ConnectionManager* client_counter = nullptr) {
    std::size_t offset = 0;
    while (offset < size) {
#ifdef _WIN32
        const int amount = static_cast<int>(std::min<std::size_t>(size - offset, 64U * 1024U));
        const int sent = send(socket, data + offset, amount, 0);
#else
        const auto amount = std::min<std::size_t>(size - offset, 64U * 1024U);
        const auto sent = send(socket, data + offset, amount, MSG_NOSIGNAL);
#endif
        if (sent <= 0) {
            const int error = LastSocketError();
            if (IsInterrupted(error)) {
                continue;
            }
            return false;
        }
        const auto count = static_cast<std::size_t>(sent);
        offset += count;
        if (client_counter != nullptr) {
            client_counter->AddBytesSent(count);
        }
    }
    return true;
}

std::string MakeHttpResponse(int status, const std::string& reason,
                             const std::string& content_type,
                             const std::string& body) {
    std::ostringstream response;
    response << "HTTP/1.1 " << status << ' ' << reason << "\r\n"
             << "Content-Type: " << content_type << "\r\n"
             << "Content-Length: " << body.size() << "\r\n"
             << "Connection: close\r\n"
             << "Cache-Control: no-store\r\n"
             << "X-Content-Type-Options: nosniff\r\n\r\n"
             << body;
    return response.str();
}

bool SendHttpResponse(WaSocket client, const std::string& response,
                      ConnectionManager& connections) {
    return SendAll(client, response.data(), response.size(), &connections);
}

std::string ReadRequest(WaSocket socket, const AppConfig& config,
                        ConnectionManager& connections) {
    SetReceiveTimeout(socket, config.request_timeout_ms);
    std::string request;
    request.reserve(std::min<std::size_t>(config.max_request_bytes, 8192));
    std::array<char, 2048> buffer{};

    while (request.size() < config.max_request_bytes) {
        const auto remaining = config.max_request_bytes - request.size();
        const auto capacity = std::min<std::size_t>(buffer.size(), remaining);
#ifdef _WIN32
        const int received = recv(socket, buffer.data(), static_cast<int>(capacity), 0);
#else
        const auto received = recv(socket, buffer.data(), capacity, 0);
#endif
        if (received == 0) {
            break;
        }
        if (received < 0) {
            const int error = LastSocketError();
            if (IsInterrupted(error)) {
                continue;
            }
            // A partial HTTP request is rejected by the caller; a quiet client is closed.
            break;
        }

        const auto count = static_cast<std::size_t>(received);
        request.append(buffer.data(), count);
        connections.AddBytesReceived(count);
        if (request.find("\r\n\r\n") != std::string::npos) {
            break;
        }
    }
    ClearReceiveTimeout(socket);
    return request;
}

bool ParseRequestLine(const std::string& request, std::string& method,
                      std::string& target, std::string& version) {
    const auto line_end = request.find("\r\n");
    if (line_end == std::string::npos) {
        return false;
    }
    std::istringstream line(request.substr(0, line_end));
    std::string extra;
    if (!(line >> method >> target >> version) || (line >> extra)) {
        return false;
    }
    return version == "HTTP/1.0" || version == "HTTP/1.1";
}

bool ParseConnectAuthority(const std::string& authority, std::string& host,
                           unsigned short& port) {
    if (authority.empty() || authority.size() > 300 ||
        authority.find_first_of(" \t\r\n@/") != std::string::npos) {
        return false;
    }

    std::string port_text;
    if (authority.front() == '[') {
        const auto closing = authority.find(']');
        if (closing == std::string::npos || closing + 1 >= authority.size() ||
            authority[closing + 1] != ':') {
            return false;
        }
        host = authority.substr(1, closing - 1);
        port_text = authority.substr(closing + 2);
    } else {
        const auto colon = authority.rfind(':');
        if (colon == std::string::npos || authority.find(':') != colon) {
            return false; // IPv6 literals must use [address]:port form.
        }
        host = authority.substr(0, colon);
        port_text = authority.substr(colon + 1);
    }
    if (host.empty() || port_text.empty() || host.size() > 253 || port_text.size() > 5) {
        return false;
    }
    for (const char raw_character : host) {
        const auto character = static_cast<unsigned char>(raw_character);
        if (!(std::isalnum(character) || character == '.' || character == '-' || character == ':')) {
            return false;
        }
    }
    unsigned int parsed_port = 0;
    for (const char raw_character : port_text) {
        const auto character = static_cast<unsigned char>(raw_character);
        if (!std::isdigit(character)) {
            return false;
        }
        parsed_port = parsed_port * 10U + static_cast<unsigned int>(character - '0');
        if (parsed_port > 65535U) {
            return false;
        }
    }
    if (parsed_port == 0U) {
        return false;
    }
    port = static_cast<unsigned short>(parsed_port);
    return true;
}

WaSocket ConnectTarget(const std::string& host, unsigned short port,
                       unsigned int timeout_ms, std::string& error_text) {
    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;
    addrinfo* addresses = nullptr;
    const std::string service = std::to_string(port);
    const int resolve_result = getaddrinfo(host.c_str(), service.c_str(), &hints, &addresses);
    if (resolve_result != 0) {
#ifdef _WIN32
        error_text = "name resolution failed (code " + std::to_string(resolve_result) + ")";
#else
        error_text = std::string("name resolution failed: ") + gai_strerror(resolve_result);
#endif
        return WA_INVALID_SOCKET;
    }

    WaSocket connected = WA_INVALID_SOCKET;
    for (addrinfo* address = addresses; address != nullptr; address = address->ai_next) {
        WaSocket candidate = socket(address->ai_family, address->ai_socktype, address->ai_protocol);
        if (candidate == WA_INVALID_SOCKET) {
            error_text = SocketErrorText(LastSocketError());
            continue;
        }

#ifdef _WIN32
        u_long nonblocking = 1;
        if (ioctlsocket(candidate, FIONBIO, &nonblocking) != 0) {
            error_text = SocketErrorText(LastSocketError());
            CloseSocket(candidate);
            continue;
        }
        const int connect_result = connect(candidate, address->ai_addr,
                                           static_cast<int>(address->ai_addrlen));
        if (connect_result != 0) {
            const int connect_error = LastSocketError();
            if (!IsWouldBlock(connect_error)) {
                error_text = SocketErrorText(connect_error);
                CloseSocket(candidate);
                continue;
            }
            fd_set writable;
            FD_ZERO(&writable);
            FD_SET(candidate, &writable);
            timeval wait_time{};
            wait_time.tv_sec = static_cast<long>(timeout_ms / 1000U);
            wait_time.tv_usec = static_cast<long>((timeout_ms % 1000U) * 1000U);
            const int ready = select(0, nullptr, &writable, nullptr, &wait_time);
            if (ready <= 0) {
                error_text = ready == 0 ? "upstream connection timed out" : SocketErrorText(LastSocketError());
                CloseSocket(candidate);
                continue;
            }
            int socket_error = 0;
            int error_length = sizeof(socket_error);
            if (getsockopt(candidate, SOL_SOCKET, SO_ERROR,
                           reinterpret_cast<char*>(&socket_error), &error_length) != 0 || socket_error != 0) {
                error_text = SocketErrorText(socket_error != 0 ? socket_error : LastSocketError());
                CloseSocket(candidate);
                continue;
            }
        }
        nonblocking = 0;
        if (ioctlsocket(candidate, FIONBIO, &nonblocking) != 0) {
            error_text = SocketErrorText(LastSocketError());
            CloseSocket(candidate);
            continue;
        }
#else
        const int original_flags = fcntl(candidate, F_GETFL, 0);
        if (original_flags < 0 || fcntl(candidate, F_SETFL, original_flags | O_NONBLOCK) < 0) {
            error_text = SocketErrorText(LastSocketError());
            CloseSocket(candidate);
            continue;
        }
        const int connect_result = connect(candidate, address->ai_addr, address->ai_addrlen);
        if (connect_result != 0) {
            const int connect_error = LastSocketError();
            if (!IsWouldBlock(connect_error)) {
                error_text = SocketErrorText(connect_error);
                CloseSocket(candidate);
                continue;
            }
            fd_set writable;
            FD_ZERO(&writable);
            FD_SET(candidate, &writable);
            timeval wait_time{};
            wait_time.tv_sec = static_cast<long>(timeout_ms / 1000U);
            wait_time.tv_usec = static_cast<long>((timeout_ms % 1000U) * 1000U);
            const int ready = select(candidate + 1, nullptr, &writable, nullptr, &wait_time);
            if (ready <= 0) {
                error_text = ready == 0 ? "upstream connection timed out" : SocketErrorText(LastSocketError());
                CloseSocket(candidate);
                continue;
            }
            int socket_error = 0;
            socklen_t error_length = sizeof(socket_error);
            if (getsockopt(candidate, SOL_SOCKET, SO_ERROR, &socket_error, &error_length) != 0 || socket_error != 0) {
                error_text = SocketErrorText(socket_error != 0 ? socket_error : LastSocketError());
                CloseSocket(candidate);
                continue;
            }
        }
        if (fcntl(candidate, F_SETFL, original_flags) < 0) {
            error_text = SocketErrorText(LastSocketError());
            CloseSocket(candidate);
            continue;
        }
#endif
        connected = candidate;
        break;
    }
    freeaddrinfo(addresses);
    if (connected == WA_INVALID_SOCKET && error_text.empty()) {
        error_text = "no usable upstream address";
    }
    return connected;
}

void ShutdownSend(WaSocket socket) {
#ifdef _WIN32
    shutdown(socket, SD_SEND);
#else
    shutdown(socket, SHUT_WR);
#endif
}

bool RelayTunnel(WaSocket client, WaSocket upstream, const AppConfig& config,
                 ConnectionManager& connections,
                 const std::atomic_bool& stop_requested,
                 const std::atomic_bool& shutdown_requested) {
    std::array<char, 16U * 1024U> buffer{};
    bool client_eof = false;
    bool upstream_eof = false;
    auto last_activity = std::chrono::steady_clock::now();

    while (!(client_eof && upstream_eof) &&
           !stop_requested.load(std::memory_order_relaxed) &&
           !shutdown_requested.load(std::memory_order_relaxed)) {
        if (config.tunnel_idle_timeout_ms > 0U) {
            const auto idle = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - last_activity).count();
            if (idle >= static_cast<long long>(config.tunnel_idle_timeout_ms)) {
                return false;
            }
        }

        fd_set readable;
        FD_ZERO(&readable);
        if (!client_eof) FD_SET(client, &readable);
        if (!upstream_eof) FD_SET(upstream, &readable);
        timeval wait_time{};
        wait_time.tv_sec = 0;
        wait_time.tv_usec = 250000;
#ifdef _WIN32
        const int ready = select(0, &readable, nullptr, nullptr, &wait_time);
#else
        const auto highest = std::max(client, upstream);
        const int ready = select(highest + 1, &readable, nullptr, nullptr, &wait_time);
#endif
        if (ready == 0) continue;
        if (ready < 0) {
            if (IsInterrupted(LastSocketError())) continue;
            return false;
        }

        if (!client_eof && FD_ISSET(client, &readable)) {
#ifdef _WIN32
            const int received = recv(client, buffer.data(), static_cast<int>(buffer.size()), 0);
#else
            const auto received = recv(client, buffer.data(), buffer.size(), 0);
#endif
            if (received == 0) {
                client_eof = true;
                ShutdownSend(upstream);
            } else if (received < 0) {
                if (!IsInterrupted(LastSocketError())) return false;
            } else {
                const auto count = static_cast<std::size_t>(received);
                connections.AddBytesReceived(count);
                if (!SendAll(upstream, buffer.data(), count)) return false;
                last_activity = std::chrono::steady_clock::now();
            }
        }

        if (!upstream_eof && FD_ISSET(upstream, &readable)) {
#ifdef _WIN32
            const int received = recv(upstream, buffer.data(), static_cast<int>(buffer.size()), 0);
#else
            const auto received = recv(upstream, buffer.data(), buffer.size(), 0);
#endif
            if (received == 0) {
                upstream_eof = true;
                ShutdownSend(client);
            } else if (received < 0) {
                if (!IsInterrupted(LastSocketError())) return false;
            } else {
                const auto count = static_cast<std::size_t>(received);
                if (!SendAll(client, buffer.data(), count, &connections)) return false;
                last_activity = std::chrono::steady_clock::now();
            }
        }
    }
    return true;
}
} // namespace

ProxyServer::ProxyServer(const AppConfig& config, Logger& logger,
                         ConnectionManager& connections)
    : config_(config), logger_(logger), connections_(connections) {}

ProxyServer::~ProxyServer() {
    RequestStop();
    CloseListener();
    ReapFinishedWorkers(true);
#ifdef _WIN32
    if (socket_library_initialized_) {
        WSACleanup();
    }
#endif
}

bool ProxyServer::OpenListener() {
#ifdef _WIN32
    WSADATA data{};
    const int startup_result = WSAStartup(MAKEWORD(2, 2), &data);
    if (startup_result != 0) {
        logger_.Error("WSAStartup failed: " + SocketErrorText(startup_result));
        return false;
    }
    socket_library_initialized_ = true;
#endif

    listener_ = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listener_ == WA_INVALID_SOCKET) {
        logger_.Error("socket() failed: " + SocketErrorText(LastSocketError()));
        return false;
    }

#ifdef _WIN32
    const BOOL exclusive = TRUE;
    if (setsockopt(listener_, SOL_SOCKET, SO_EXCLUSIVEADDRUSE,
                   reinterpret_cast<const char*>(&exclusive), sizeof(exclusive)) != 0) {
        logger_.Warn("SO_EXCLUSIVEADDRUSE could not be enabled: " + SocketErrorText(LastSocketError()));
    }
#endif

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(config_.port);
    if (inet_pton(AF_INET, config_.listen_address.c_str(), &address.sin_addr) != 1) {
        logger_.Error("Invalid IPv4 listen address: " + config_.listen_address);
        CloseListener();
        return false;
    }
    if (bind(listener_, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) != 0) {
        logger_.Error("bind() failed on " + config_.listen_address + ":" +
                      std::to_string(config_.port) + ": " + SocketErrorText(LastSocketError()));
        CloseListener();
        return false;
    }
    if (listen(listener_, SOMAXCONN) != 0) {
        logger_.Error("listen() failed: " + SocketErrorText(LastSocketError()));
        CloseListener();
        return false;
    }
    return true;
}

int ProxyServer::Run(const std::atomic_bool& shutdown_requested) {
    workers_.reserve(static_cast<std::size_t>(config_.max_connections) + 1U);
    if (!OpenListener()) return 1;

    logger_.Info("WA Proxy v1.0.0 started at " + config_.listen_address + ":" +
                 std::to_string(config_.port) + ". HTTP CONNECT forwarding is enabled.");
    if (config_.remote_tls_enabled) {
        logger_.Info("Mutual-TLS VPS mode is enabled. Destination hostnames are sent inside TLS; failures do not fall back to direct mode.");
    } else {
        logger_.Warn("Direct mode is enabled. Destination DNS and TCP connections use this PC's network and may be visible to its ISP.");
    }
    logger_.Info("Local-only listener: only processes on this PC can connect.");

    while (!stop_requested_.load(std::memory_order_relaxed) &&
           !shutdown_requested.load(std::memory_order_relaxed)) {
        fd_set readable;
        FD_ZERO(&readable);
        FD_SET(listener_, &readable);
        timeval wait_time{};
        wait_time.tv_sec = 0;
        wait_time.tv_usec = 250000;
#ifdef _WIN32
        const int ready = select(0, &readable, nullptr, nullptr, &wait_time);
#else
        const int ready = select(listener_ + 1, &readable, nullptr, nullptr, &wait_time);
#endif
        if (ready == 0) {
            ReapFinishedWorkers(false);
            continue;
        }
        if (ready < 0) {
            const int error = LastSocketError();
            if (IsInterrupted(error)) continue;
            logger_.Error("select() failed: " + SocketErrorText(error));
            RequestStop();
            break;
        }

        sockaddr_in peer_address{};
#ifdef _WIN32
        int peer_length = sizeof(peer_address);
#else
        socklen_t peer_length = sizeof(peer_address);
#endif
        const WaSocket client = accept(listener_, reinterpret_cast<sockaddr*>(&peer_address), &peer_length);
        if (client == WA_INVALID_SOCKET) {
            const int error = LastSocketError();
            if (!IsInterrupted(error)) logger_.Warn("accept() failed: " + SocketErrorText(error));
            ReapFinishedWorkers(false);
            continue;
        }

        char peer_ip[INET_ADDRSTRLEN]{};
        if (inet_ntop(AF_INET, &peer_address.sin_addr, peer_ip, sizeof(peer_ip)) == nullptr) {
            std::memcpy(peer_ip, "unknown", sizeof("unknown"));
        }
        const std::string peer = std::string(peer_ip) + ":" +
                                 std::to_string(ntohs(peer_address.sin_port));

        const auto stats = connections_.Snapshot();
        if (stats.active_connections >= config_.max_connections) {
            connections_.RecordRejected();
            const std::string response = MakeHttpResponse(
                503, "Service Unavailable", "text/plain; charset=utf-8",
                "WA Proxy: connection limit reached.\n");
            SendHttpResponse(client, response, connections_);
            CloseSocket(client);
            logger_.Warn("Rejected local connection from " + peer + " (connection limit).");
        } else {
            AcceptClient(client, peer, shutdown_requested);
        }
        ReapFinishedWorkers(false);
    }

    RequestStop();
    logger_.Info("Stopping listener and closing active tunnels...");
    CloseListener();
    ReapFinishedWorkers(true);
    const auto stats = connections_.Snapshot();
    logger_.Info("Stopped. accepted=" + std::to_string(stats.accepted_connections) +
                 ", rejected=" + std::to_string(stats.rejected_connections) +
                 ", bytes_from_clients=" + std::to_string(stats.bytes_received) +
                 ", bytes_to_clients=" + std::to_string(stats.bytes_sent));
    return 0;
}

void ProxyServer::RequestStop() {
    stop_requested_.store(true, std::memory_order_relaxed);
}

void ProxyServer::AcceptClient(WaSocket client_socket, const std::string& peer,
                               const std::atomic_bool& shutdown_requested) {
    connections_.BeginConnection();
    auto finished = std::make_shared<std::atomic_bool>(false);
    bool worker_slot_created = false;
    try {
        workers_.emplace_back();
        worker_slot_created = true;
        Worker& worker = workers_.back();
        worker.finished = finished;
        worker.thread = std::thread([this, client_socket, peer, finished, &shutdown_requested]() {
            try {
                HandleClient(client_socket, peer, shutdown_requested);
            } catch (const std::exception& exception) {
                logger_.Warn("Connection from " + peer + " ended with an error: " + exception.what());
            } catch (...) {
                logger_.Warn("Connection from " + peer + " ended with an unknown error.");
            }
            finished->store(true, std::memory_order_release);
        });
    } catch (const std::exception& exception) {
        if (worker_slot_created && !workers_.back().thread.joinable()) workers_.pop_back();
        connections_.EndConnection();
        CloseSocket(client_socket);
        logger_.Error(std::string("Could not start connection worker: ") + exception.what());
    }
}

void ProxyServer::HandleClient(WaSocket client_socket, const std::string& peer,
                               const std::atomic_bool& shutdown_requested) {
    WaSocket upstream_socket = WA_INVALID_SOCKET;
    struct SessionGuard {
        WaSocket client;
        WaSocket& upstream;
        ConnectionManager& manager;
        ~SessionGuard() {
            CloseSocket(upstream);
            CloseSocket(client);
            manager.EndConnection();
        }
    } guard{client_socket, upstream_socket, connections_};

    const std::string request = ReadRequest(client_socket, config_, connections_);
    if (request.empty()) {
        logger_.Info("Closed idle local connection from " + peer + ".");
        return;
    }
    const auto header_end = request.find("\r\n\r\n");
    if (header_end == std::string::npos) {
        const auto response = MakeHttpResponse(431, "Request Header Fields Too Large",
                                               "text/plain; charset=utf-8",
                                               "HTTP request headers were incomplete or too large.\n");
        SendHttpResponse(client_socket, response, connections_);
        logger_.Warn("Returned HTTP 431 to " + peer + " (incomplete/oversized headers).");
        return;
    }

    std::string method;
    std::string target;
    std::string version;
    if (!ParseRequestLine(request, method, target, version)) {
        const auto response = MakeHttpResponse(400, "Bad Request", "text/plain; charset=utf-8",
                                               "Malformed HTTP request line.\n");
        SendHttpResponse(client_socket, response, connections_);
        logger_.Warn("Returned HTTP 400 to " + peer + " (malformed request line).");
        return;
    }

    if (method == "GET" && (target == "/health" || target == "/health/")) {
        const auto stats = connections_.Snapshot();
        std::ostringstream body;
        body << "{\n"
             << "  \"application\": \"WA Proxy\",\n"
             << "  \"version\": \"1.0.0\",\n"
             << "  \"status\": \"ok\",\n"
             << "  \"mode\": \"" << (config_.remote_tls_enabled ? "mutual-tls-vps-proxy" : "local-direct-connect-proxy") << "\",\n"
             << "  \"connect_tunneling\": true,\n"
             << "  \"remote_tls\": " << (config_.remote_tls_enabled ? "true" : "false") << ",\n"
             << "  \"accepted_connections\": " << stats.accepted_connections << ",\n"
             << "  \"active_connections\": " << stats.active_connections << ",\n"
             << "  \"bytes_received\": " << stats.bytes_received << ",\n"
             << "  \"bytes_sent\": " << stats.bytes_sent << "\n"
             << "}\n";
        const auto response = MakeHttpResponse(200, "OK", "application/json; charset=utf-8", body.str());
        SendHttpResponse(client_socket, response, connections_);
        logger_.Info("Returned HTTP 200 /health to " + peer + ".");
        return;
    }

    if (method != "CONNECT") {
        const auto response = MakeHttpResponse(501, "Not Implemented",
                                               "text/plain; charset=utf-8",
                                               "WA Proxy v1.0 supports HTTPS CONNECT only.\n");
        SendHttpResponse(client_socket, response, connections_);
        logger_.Info("Returned HTTP 501 to " + peer + " for a non-CONNECT request.");
        return;
    }

    std::string host;
    unsigned short port = 0;
    if (!ParseConnectAuthority(target, host, port)) {
        const auto response = MakeHttpResponse(400, "Bad Request", "text/plain; charset=utf-8",
                                               "CONNECT target must be hostname:port or [IPv6]:port.\n");
        SendHttpResponse(client_socket, response, connections_);
        logger_.Warn("Rejected malformed CONNECT authority from " + peer + ".");
        return;
    }
    if (port != 443U && !config_.allow_non_443_ports) {
        const auto response = MakeHttpResponse(403, "Forbidden", "text/plain; charset=utf-8",
                                               "Only CONNECT port 443 is allowed by default.\n");
        SendHttpResponse(client_socket, response, connections_);
        logger_.Warn("Rejected CONNECT request to a non-443 port from " + peer + ".");
        return;
    }

    RemoteTlsSession remote_tls;
    std::string connect_error;
    if (config_.remote_tls_enabled) {
        if (!remote_tls.Open(config_, host, port, connect_error)) {
            const auto response = MakeHttpResponse(502, "Bad Gateway", "text/plain; charset=utf-8",
                                                   "Could not establish the authenticated remote TLS tunnel.\n");
            SendHttpResponse(client_socket, response, connections_);
            logger_.Warn("Remote TLS tunnel setup failed for local peer " + peer + ": " + connect_error + ".");
            return; // Fail closed: never fall back to a direct connection.
        }
    } else {
        upstream_socket = ConnectTarget(host, port, config_.connect_timeout_ms, connect_error);
        if (upstream_socket == WA_INVALID_SOCKET) {
            const auto response = MakeHttpResponse(502, "Bad Gateway", "text/plain; charset=utf-8",
                                                   "Could not connect to the requested upstream destination.\n");
            SendHttpResponse(client_socket, response, connections_);
            logger_.Warn("CONNECT upstream failed for local peer " + peer + ": " + connect_error + ".");
            return;
        }
    }

    static const std::string established =
        "HTTP/1.1 200 Connection Established\r\n"
        "Proxy-Agent: WAProxy/1.0.0\r\n"
        "Connection: keep-alive\r\n\r\n";
    if (!SendHttpResponse(client_socket, established, connections_)) {
        logger_.Warn("Could not send CONNECT success response to " + peer + ".");
        return;
    }
    logger_.Info(config_.remote_tls_enabled
        ? "Established authenticated TLS-to-VPS tunnel (destination omitted from logs) for " + peer + "."
        : "Established direct HTTPS CONNECT tunnel (destination omitted from logs) for " + peer + ".");

    // Preserve bytes received after the CONNECT headers before the 200 response.
    const std::size_t body_offset = header_end + 4U;
    if (request.size() > body_offset) {
        const std::size_t extra_size = request.size() - body_offset;
        if (config_.remote_tls_enabled) {
            std::string write_error;
            if (!remote_tls.WriteAll(request.data() + body_offset, extra_size, write_error)) {
                logger_.Warn("Could not forward buffered client data into TLS tunnel: " + write_error + ".");
                return;
            }
        } else if (!SendAll(upstream_socket, request.data() + body_offset, extra_size)) {
            return;
        }
    }

    bool relay_ok = false;
    std::string relay_reason;
    if (config_.remote_tls_enabled) {
        wa::RelayStats relay_stats;
        relay_ok = wa::RelayTlsAndSocket(remote_tls.GetSsl(), client_socket,
                                         config_.tunnel_idle_timeout_ms,
                                         stop_requested_, relay_stats, relay_reason);
        connections_.AddBytesReceived(relay_stats.plain_to_tls);
        connections_.AddBytesSent(relay_stats.tls_to_plain);
    } else {
        relay_ok = RelayTunnel(client_socket, upstream_socket, config_, connections_,
                               stop_requested_, shutdown_requested);
        relay_reason = relay_ok ? "completed" : "peer disconnect, idle timeout, or shutdown";
    }
    logger_.Info("HTTPS CONNECT tunnel closed for " + peer + " (" + relay_reason + ").");
}

void ProxyServer::ReapFinishedWorkers(bool join_all) {
    auto iterator = workers_.begin();
    while (iterator != workers_.end()) {
        if (join_all || iterator->finished->load(std::memory_order_acquire)) {
            if (iterator->thread.joinable()) iterator->thread.join();
            iterator = workers_.erase(iterator);
        } else {
            ++iterator;
        }
    }
}

void ProxyServer::CloseListener() {
    if (listener_ != WA_INVALID_SOCKET) {
        CloseSocket(listener_);
        listener_ = WA_INVALID_SOCKET;
    }
}
