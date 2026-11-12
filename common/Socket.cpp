#include "Socket.h"

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <sstream>

#include <openssl/err.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <unistd.h>
#endif

namespace wa {
namespace {
std::string SocketError(int code) {
#ifdef _WIN32
    return "Winsock error " + std::to_string(code);
#else
    return std::string(std::strerror(code));
#endif
}

bool IsInProgress(int code) {
#ifdef _WIN32
    return code == WSAEWOULDBLOCK || code == WSAEINPROGRESS || code == WSAEINVAL;
#else
    return code == EINPROGRESS || code == EWOULDBLOCK;
#endif
}
} // namespace

bool InitializeSockets(std::string& error) {
#ifdef _WIN32
    WSADATA data{};
    const int result = WSAStartup(MAKEWORD(2, 2), &data);
    if (result != 0) {
        error = SocketError(result);
        return false;
    }
#else
    (void)error;
#endif
    return true;
}

void CleanupSockets() {
#ifdef _WIN32
    WSACleanup();
#endif
}

int LastSocketError() {
#ifdef _WIN32
    return WSAGetLastError();
#else
    return errno;
#endif
}

void CloseSocket(WaSocket socket) {
    if (socket == WA_INVALID_SOCKET) return;
#ifdef _WIN32
    shutdown(socket, SD_BOTH);
    closesocket(socket);
#else
    shutdown(socket, SHUT_RDWR);
    close(socket);
#endif
}

bool SetSocketNonBlocking(WaSocket socket, bool enabled, std::string& error) {
#ifdef _WIN32
    u_long mode = enabled ? 1UL : 0UL;
    if (ioctlsocket(socket, FIONBIO, &mode) != 0) {
#else
    const int flags = fcntl(socket, F_GETFL, 0);
    if (flags < 0 || fcntl(socket, F_SETFL, enabled ? (flags | O_NONBLOCK) : (flags & ~O_NONBLOCK)) < 0) {
#endif
        error = SocketError(LastSocketError());
        return false;
    }
    return true;
}

WaSocket ConnectTcp(const std::string& host, unsigned short port,
                    unsigned int timeout_ms, std::string& error) {
    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;
    addrinfo* addresses = nullptr;
    const std::string service = std::to_string(port);
    const int result = getaddrinfo(host.c_str(), service.c_str(), &hints, &addresses);
    if (result != 0) {
#ifdef _WIN32
        error = "remote host name resolution failed (code " + std::to_string(result) + ")";
#else
        error = std::string("remote host name resolution failed: ") + gai_strerror(result);
#endif
        return WA_INVALID_SOCKET;
    }

    WaSocket connected = WA_INVALID_SOCKET;
    for (addrinfo* address = addresses; address != nullptr; address = address->ai_next) {
        WaSocket candidate = socket(address->ai_family, address->ai_socktype, address->ai_protocol);
        if (candidate == WA_INVALID_SOCKET) {
            error = SocketError(LastSocketError());
            continue;
        }
        std::string nonblocking_error;
        if (!SetSocketNonBlocking(candidate, true, nonblocking_error)) {
            error = nonblocking_error;
            CloseSocket(candidate);
            continue;
        }
        const int connected_now = connect(candidate, address->ai_addr,
#ifdef _WIN32
                                          static_cast<int>(address->ai_addrlen)
#else
                                          static_cast<socklen_t>(address->ai_addrlen)
#endif
        );
        if (connected_now != 0) {
            const int connect_error = LastSocketError();
            if (!IsInProgress(connect_error)) {
                error = SocketError(connect_error);
                CloseSocket(candidate);
                continue;
            }
            fd_set writable;
            FD_ZERO(&writable);
            FD_SET(candidate, &writable);
            timeval timeout{};
            timeout.tv_sec = static_cast<long>(timeout_ms / 1000U);
            timeout.tv_usec = static_cast<long>((timeout_ms % 1000U) * 1000U);
#ifdef _WIN32
            const int ready = select(0, nullptr, &writable, nullptr, &timeout);
#else
            const int ready = select(candidate + 1, nullptr, &writable, nullptr, &timeout);
#endif
            if (ready <= 0) {
                error = ready == 0 ? "TCP connect timed out" : SocketError(LastSocketError());
                CloseSocket(candidate);
                continue;
            }
            int socket_error = 0;
#ifdef _WIN32
            int error_length = sizeof(socket_error);
#else
            socklen_t error_length = sizeof(socket_error);
#endif
            if (getsockopt(candidate, SOL_SOCKET, SO_ERROR,
#ifdef _WIN32
                           reinterpret_cast<char*>(&socket_error), &error_length
#else
                           &socket_error, &error_length
#endif
            ) != 0 || socket_error != 0) {
                error = SocketError(socket_error != 0 ? socket_error : LastSocketError());
                CloseSocket(candidate);
                continue;
            }
        }
        if (!SetSocketNonBlocking(candidate, false, nonblocking_error)) {
            error = nonblocking_error;
            CloseSocket(candidate);
            continue;
        }
        connected = candidate;
        break;
    }
    freeaddrinfo(addresses);
    if (connected == WA_INVALID_SOCKET && error.empty()) error = "no usable address for remote endpoint";
    return connected;
}

bool SendAll(WaSocket socket, const void* data, std::size_t size, std::string& error) {
    const auto* bytes = static_cast<const char*>(data);
    std::size_t offset = 0;
    while (offset < size) {
        const auto amount = std::min<std::size_t>(size - offset, 64U * 1024U);
#ifdef _WIN32
        const int sent = send(socket, bytes + offset, static_cast<int>(amount), 0);
#else
        const auto sent = send(socket, bytes + offset, amount, MSG_NOSIGNAL);
#endif
        if (sent < 0) {
            const int code = LastSocketError();
#ifdef _WIN32
            if (code == WSAEINTR) continue;
#else
            if (code == EINTR) continue;
#endif
            error = SocketError(code);
            return false;
        }
        if (sent == 0) {
            error = "socket closed while sending";
            return false;
        }
        offset += static_cast<std::size_t>(sent);
    }
    return true;
}

void ShutdownSend(WaSocket socket) {
    if (socket == WA_INVALID_SOCKET) return;
#ifdef _WIN32
    shutdown(socket, SD_SEND);
#else
    shutdown(socket, SHUT_WR);
#endif
}

std::string OpenSslError() {
    const unsigned long code = ERR_get_error();
    if (code == 0) return "OpenSSL reported an unspecified error";
    char buffer[256]{};
    ERR_error_string_n(code, buffer, sizeof(buffer));
    return buffer;
}
} // namespace wa
