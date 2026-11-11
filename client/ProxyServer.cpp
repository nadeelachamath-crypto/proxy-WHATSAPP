#include "ProxyServer.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstring>
#include <sstream>
#include <stdexcept>
#include <utility>

#ifdef _WIN32
    #include <ws2tcpip.h>
#else
    #include <arpa/inet.h>
    #include <cerrno>
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
bool IsTimeout(int error) { return error == WSAETIMEDOUT || error == WSAEWOULDBLOCK; }
#else
int LastSocketError() { return errno; }
void CloseSocket(WaSocket socket) {
    if (socket != WA_INVALID_SOCKET) {
        shutdown(socket, SHUT_RDWR);
        close(socket);
    }
}
bool IsInterrupted(int error) { return error == EINTR; }
bool IsTimeout(int error) { return error == EAGAIN || error == EWOULDBLOCK || error == EINTR; }
#endif

std::string SocketErrorText(int error) {
    return "socket error " + std::to_string(error);
}

bool SendAll(WaSocket socket, const std::string& data, ConnectionManager& connections) {
    std::size_t offset = 0;
    while (offset < data.size()) {
#ifdef _WIN32
        const int sent = send(socket, data.data() + offset,
                              static_cast<int>(data.size() - offset), 0);
#else
        const auto sent = send(socket, data.data() + offset, data.size() - offset, MSG_NOSIGNAL);
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
        connections.AddBytesSent(count);
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

std::string ReadRequest(WaSocket socket, const AppConfig& config,
                        ConnectionManager& connections) {
#ifdef _WIN32
    const DWORD timeout = static_cast<DWORD>(config.request_timeout_ms);
    setsockopt(socket, SOL_SOCKET, SO_RCVTIMEO,
               reinterpret_cast<const char*>(&timeout), sizeof(timeout));
#else
    timeval timeout{};
    timeout.tv_sec = static_cast<long>(config.request_timeout_ms / 1000);
    timeout.tv_usec = static_cast<long>((config.request_timeout_ms % 1000) * 1000);
    setsockopt(socket, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
#endif

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
            if (IsTimeout(error) && !request.empty()) {
                break;
            }
            if (IsTimeout(error)) {
                break;
            }
            throw std::runtime_error(SocketErrorText(error));
        }

        const auto count = static_cast<std::size_t>(received);
        request.append(buffer.data(), count);
        connections.AddBytesReceived(count);
        if (request.find("\r\n\r\n") != std::string::npos) {
            break;
        }
    }
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
    // Prevent another local process from sharing this endpoint where supported.
    const BOOL exclusive = TRUE;
    if (setsockopt(listener_, SOL_SOCKET, SO_EXCLUSIVEADDRUSE,
                   reinterpret_cast<const char*>(&exclusive), sizeof(exclusive)) != 0) {
        logger_.Warn("SO_EXCLUSIVEADDRUSE could not be enabled: " +
                     SocketErrorText(LastSocketError()));
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
                      std::to_string(config_.port) + ": " +
                      SocketErrorText(LastSocketError()));
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
    workers_.reserve(static_cast<std::size_t>(config_.max_connections) + 1);
    if (!OpenListener()) {
        return 1;
    }

    logger_.Info("WA Proxy v0.1 diagnostic listener started at " +
                 config_.listen_address + ":" + std::to_string(config_.port));
    logger_.Info("Loopback-only mode is enforced; HTTP CONNECT forwarding is not implemented.");

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
            if (IsInterrupted(error)) {
                continue;
            }
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
        const WaSocket client = accept(listener_,
                                       reinterpret_cast<sockaddr*>(&peer_address),
                                       &peer_length);
        if (client == WA_INVALID_SOCKET) {
            const int error = LastSocketError();
            if (!IsInterrupted(error)) {
                logger_.Warn("accept() failed: " + SocketErrorText(error));
            }
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
                "WA Proxy v0.1: connection limit reached.\n");
            SendAll(client, response, connections_);
            CloseSocket(client);
            logger_.Warn("Rejected local connection from " + peer + " (connection limit).");
        } else {
            AcceptClient(client, peer);
        }
        ReapFinishedWorkers(false);
    }

    logger_.Info("Stopping listener and draining active local connections...");
    CloseListener();
    ReapFinishedWorkers(true);
    const auto stats = connections_.Snapshot();
    logger_.Info("Stopped. accepted=" + std::to_string(stats.accepted_connections) +
                 ", rejected=" + std::to_string(stats.rejected_connections) +
                 ", bytes_in=" + std::to_string(stats.bytes_received) +
                 ", bytes_out=" + std::to_string(stats.bytes_sent));
    return 0;
}

void ProxyServer::RequestStop() {
    stop_requested_.store(true, std::memory_order_relaxed);
}

void ProxyServer::AcceptClient(WaSocket client_socket, const std::string& peer) {
    connections_.BeginConnection();
    auto finished = std::make_shared<std::atomic_bool>(false);
    bool worker_slot_created = false;
    try {
        // Allocate the vector slot before launching the thread. If allocation fails,
        // no worker exists yet and socket/counter cleanup remains unambiguous.
        workers_.emplace_back();
        worker_slot_created = true;
        Worker& worker = workers_.back();
        worker.finished = finished;
        worker.thread = std::thread([this, client_socket, peer, finished]() {
            try {
                HandleClient(client_socket, peer);
            } catch (const std::exception& exception) {
                // HandleClient's session guard owns socket closure and active-counter cleanup.
                logger_.Warn("Connection from " + peer + " ended with an error: " +
                             exception.what());
            } catch (...) {
                logger_.Warn("Connection from " + peer + " ended with an unknown error.");
            }
            finished->store(true, std::memory_order_release);
        });
    } catch (const std::exception& exception) {
        if (worker_slot_created && !workers_.back().thread.joinable()) {
            workers_.pop_back();
        }
        connections_.EndConnection();
        CloseSocket(client_socket);
        logger_.Error(std::string("Could not start connection worker: ") + exception.what());
    }
}

void ProxyServer::HandleClient(WaSocket client_socket, const std::string& peer) {
    struct SessionGuard {
        WaSocket socket;
        ConnectionManager& manager;
        ~SessionGuard() {
            CloseSocket(socket);
            manager.EndConnection();
        }
    } guard{client_socket, connections_};

    const std::string request = ReadRequest(client_socket, config_, connections_);
    if (request.empty()) {
        logger_.Info("Closed idle local connection from " + peer + ".");
        return;
    }

    std::string method;
    std::string target;
    std::string version;
    if (!ParseRequestLine(request, method, target, version)) {
        const auto response = MakeHttpResponse(400, "Bad Request",
                                                "text/plain; charset=utf-8",
                                                "Malformed HTTP request.\n");
        SendAll(client_socket, response, connections_);
        logger_.Warn("Returned HTTP 400 to " + peer + " (malformed request line).");
        return;
    }

    if (method == "CONNECT") {
        const auto response = MakeHttpResponse(
            501, "Not Implemented", "text/plain; charset=utf-8",
            "WA Proxy v0.1 is a diagnostic listener only. HTTP CONNECT tunneling is not implemented.\n");
        SendAll(client_socket, response, connections_);
        logger_.Info("Returned HTTP 501 to " + peer + " for CONNECT (tunneling not implemented).");
        return;
    }

    if (method == "GET" && (target == "/health" || target == "/health/")) {
        const auto stats = connections_.Snapshot();
        std::ostringstream body;
        body << "{\n"
             << "  \"application\": \"WA Proxy\",\n"
             << "  \"version\": \"0.1.0\",\n"
             << "  \"status\": \"ok\",\n"
             << "  \"mode\": \"diagnostic-listener-only\",\n"
             << "  \"connect_tunneling\": false,\n"
             << "  \"accepted_connections\": " << stats.accepted_connections << ",\n"
             << "  \"active_connections\": " << stats.active_connections << ",\n"
             << "  \"bytes_received\": " << stats.bytes_received << ",\n"
             << "  \"bytes_sent\": " << stats.bytes_sent << "\n"
             << "}\n";
        const auto response = MakeHttpResponse(200, "OK",
                                               "application/json; charset=utf-8",
                                               body.str());
        SendAll(client_socket, response, connections_);
        logger_.Info("Returned HTTP 200 /health to " + peer + ".");
        return;
    }

    if (method == "GET" && (target == "/" || target == "/index.html")) {
        const std::string body =
            "WA Proxy v0.1 diagnostic listener\n"
            "GET /health returns JSON status.\n"
            "CONNECT tunneling, forwarding, TLS, and remote-server support are not part of v0.1.\n";
        const auto response = MakeHttpResponse(200, "OK", "text/plain; charset=utf-8", body);
        SendAll(client_socket, response, connections_);
        logger_.Info("Returned HTTP 200 / to " + peer + ".");
        return;
    }

    const auto response = MakeHttpResponse(404, "Not Found",
                                           "text/plain; charset=utf-8",
                                           "Endpoint not found. Try GET /health.\n");
    SendAll(client_socket, response, connections_);
    logger_.Info("Returned HTTP 404 to " + peer + ".");
}

void ProxyServer::ReapFinishedWorkers(bool join_all) {
    auto iterator = workers_.begin();
    while (iterator != workers_.end()) {
        if (join_all || iterator->finished->load(std::memory_order_acquire)) {
            if (iterator->thread.joinable()) {
                iterator->thread.join();
            }
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
