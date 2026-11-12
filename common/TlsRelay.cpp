#include "TlsRelay.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstring>

#ifdef _WIN32
#include <winsock2.h>
#else
#include <cerrno>
#include <sys/select.h>
#endif

namespace wa {
namespace {
constexpr std::size_t kBufferSize = 32U * 1024U;
struct PendingBuffer {
    std::array<char, kBufferSize> data{};
    std::size_t begin = 0;
    std::size_t end = 0;
    std::size_t size() const { return end - begin; }
    std::size_t free() const { return data.size() - end; }
    const char* readData() const { return data.data() + begin; }
    char* writeData() { return data.data() + end; }
    void consume(std::size_t count) {
        begin += count;
        if (begin == end) begin = end = 0;
        else if (begin > data.size() / 2U) {
            std::memmove(data.data(), data.data() + begin, end - begin);
            end -= begin;
            begin = 0;
        }
    }
    void produce(std::size_t count) { end += count; }
};

bool WouldBlock(int code) {
#ifdef _WIN32
    return code == WSAEWOULDBLOCK || code == WSAEINPROGRESS;
#else
    return code == EAGAIN || code == EWOULDBLOCK;
#endif
}
} // namespace

bool RelayTlsAndSocket(SSL* tls, WaSocket plain_socket,
                       unsigned int idle_timeout_ms,
                       const std::atomic_bool& stop_requested,
                       RelayStats& stats, std::string& reason) {
    if (tls == nullptr || plain_socket == WA_INVALID_SOCKET) {
        reason = "invalid TLS relay endpoint";
        return false;
    }
    const WaSocket tls_socket = static_cast<WaSocket>(SSL_get_fd(tls));
    if (tls_socket == WA_INVALID_SOCKET) {
        reason = "TLS stream has no socket";
        return false;
    }
    std::string nonblocking_error;
    if (!SetSocketNonBlocking(plain_socket, true, nonblocking_error) ||
        !SetSocketNonBlocking(tls_socket, true, nonblocking_error)) {
        reason = "could not set relay sockets nonblocking: " + nonblocking_error;
        return false;
    }
    SSL_set_mode(tls, SSL_MODE_ENABLE_PARTIAL_WRITE | SSL_MODE_ACCEPT_MOVING_WRITE_BUFFER);

    PendingBuffer plain_to_tls;
    PendingBuffer tls_to_plain;
    bool plain_eof = false;
    bool tls_eof = false;
    bool tls_shutdown_sent = false;
    bool plain_shutdown_sent = false;
    bool tls_write_wants_read = false;
    bool tls_write_wants_write = true;
    bool tls_read_wants_write = false;
    bool shutdown_wants_read = false;
    bool shutdown_wants_write = false;
    auto last_activity = std::chrono::steady_clock::now();

    while (!(plain_eof && tls_eof && plain_to_tls.size() == 0 &&
             tls_to_plain.size() == 0 && tls_shutdown_sent)) {
        if (stop_requested.load(std::memory_order_relaxed)) {
            reason = "shutdown requested";
            return false;
        }
        if (idle_timeout_ms != 0U) {
            const auto idle = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - last_activity).count();
            if (idle >= static_cast<long long>(idle_timeout_ms)) {
                reason = "idle timeout";
                return false;
            }
        }

        if (plain_eof && plain_to_tls.size() == 0 && !tls_shutdown_sent) {
            const int result = SSL_shutdown(tls);
            if (result >= 0) {
                tls_shutdown_sent = true;
                shutdown_wants_read = false;
                shutdown_wants_write = false;
            } else {
                const int ssl_error = SSL_get_error(tls, result);
                if (ssl_error == SSL_ERROR_WANT_WRITE) {
                    shutdown_wants_read = false;
                    shutdown_wants_write = true;
                } else if (ssl_error == SSL_ERROR_WANT_READ) {
                    shutdown_wants_read = true;
                    shutdown_wants_write = false;
                }
                else {
                    reason = "TLS shutdown failed";
                    return false;
                }
            }
        }

        fd_set read_set;
        fd_set write_set;
        FD_ZERO(&read_set);
        FD_ZERO(&write_set);
        if (!plain_eof && plain_to_tls.free() > 0) FD_SET(plain_socket, &read_set);
        if (tls_to_plain.free() > 0 && !tls_eof) FD_SET(tls_socket, &read_set);
        if (plain_to_tls.size() > 0) {
            if (tls_write_wants_read) FD_SET(tls_socket, &read_set);
            if (tls_write_wants_write) FD_SET(tls_socket, &write_set);
        }
        if (tls_to_plain.size() > 0) FD_SET(plain_socket, &write_set);
        if (tls_read_wants_write) FD_SET(tls_socket, &write_set);
        if (shutdown_wants_read) FD_SET(tls_socket, &read_set);
        if (shutdown_wants_write) FD_SET(tls_socket, &write_set);

        timeval wait{};
        wait.tv_sec = 0;
        wait.tv_usec = 200000;
#ifdef _WIN32
        const int ready = select(0, &read_set, &write_set, nullptr, &wait);
#else
        const WaSocket highest = std::max(plain_socket, tls_socket);
        const int ready = select(highest + 1, &read_set, &write_set, nullptr, &wait);
#endif
        if (ready < 0) {
#ifdef _WIN32
            if (WSAGetLastError() == WSAEINTR) continue;
#else
            if (errno == EINTR) continue;
#endif
            reason = "select failed during TLS relay";
            return false;
        }
        const bool plain_readable = FD_ISSET(plain_socket, &read_set) != 0;
        const bool plain_writable = FD_ISSET(plain_socket, &write_set) != 0;
        const bool tls_readable = FD_ISSET(tls_socket, &read_set) != 0;
        const bool tls_writable = FD_ISSET(tls_socket, &write_set) != 0;

        if (plain_readable && !plain_eof && plain_to_tls.free() > 0) {
#ifdef _WIN32
            const int received = recv(plain_socket, plain_to_tls.writeData(),
                                      static_cast<int>(plain_to_tls.free()), 0);
#else
            const auto received = recv(plain_socket, plain_to_tls.writeData(), plain_to_tls.free(), 0);
#endif
            if (received == 0) {
                plain_eof = true;
            } else if (received > 0) {
                plain_to_tls.produce(static_cast<std::size_t>(received));
                stats.plain_to_tls += static_cast<std::uint64_t>(received);
                last_activity = std::chrono::steady_clock::now();
                tls_write_wants_read = false;
                tls_write_wants_write = true;
            } else if (!WouldBlock(LastSocketError())) {
#ifdef _WIN32
                if (LastSocketError() != WSAEINTR) {
#else
                if (LastSocketError() != EINTR) {
#endif
                    reason = "plain socket read failed";
                    return false;
                }
            }
        }

        if (!tls_eof && tls_to_plain.free() > 0 &&
            (tls_readable || (tls_read_wants_write && tls_writable) || SSL_pending(tls) > 0)) {
            const int capacity = static_cast<int>(std::min<std::size_t>(tls_to_plain.free(), 16U * 1024U));
            const int received = SSL_read(tls, tls_to_plain.writeData(), capacity);
            if (received > 0) {
                tls_to_plain.produce(static_cast<std::size_t>(received));
                stats.tls_to_plain += static_cast<std::uint64_t>(received);
                last_activity = std::chrono::steady_clock::now();
                tls_read_wants_write = false;
            } else {
                const int ssl_error = SSL_get_error(tls, received);
                if (ssl_error == SSL_ERROR_ZERO_RETURN) {
                    // Preserve bytes already read from TLS: drain the pending
                    // buffer before half-closing the plain TCP write direction.
                    tls_eof = true;
                } else if (ssl_error == SSL_ERROR_WANT_READ) {
                    tls_read_wants_write = false;
                } else if (ssl_error == SSL_ERROR_WANT_WRITE) {
                    tls_read_wants_write = true;
                } else {
                    reason = "TLS read failed";
                    return false;
                }
            }
        }

        if (plain_to_tls.size() > 0 && (tls_writable || (tls_write_wants_read && tls_readable))) {
            const int amount = static_cast<int>(std::min<std::size_t>(plain_to_tls.size(), 16U * 1024U));
            const int sent = SSL_write(tls, plain_to_tls.readData(), amount);
            if (sent > 0) {
                plain_to_tls.consume(static_cast<std::size_t>(sent));
                tls_write_wants_read = false;
                tls_write_wants_write = plain_to_tls.size() > 0;
                last_activity = std::chrono::steady_clock::now();
            } else {
                const int ssl_error = SSL_get_error(tls, sent);
                if (ssl_error == SSL_ERROR_WANT_READ) {
                    tls_write_wants_read = true;
                    tls_write_wants_write = false;
                } else if (ssl_error == SSL_ERROR_WANT_WRITE) {
                    tls_write_wants_read = false;
                    tls_write_wants_write = true;
                } else {
                    reason = "TLS write failed";
                    return false;
                }
            }
        }

        if (plain_writable && tls_to_plain.size() > 0) {
#ifdef _WIN32
            const int amount = static_cast<int>(std::min<std::size_t>(tls_to_plain.size(), 64U * 1024U));
            const int sent = send(plain_socket, tls_to_plain.readData(), amount, 0);
#else
            const auto amount = std::min<std::size_t>(tls_to_plain.size(), 64U * 1024U);
            const auto sent = send(plain_socket, tls_to_plain.readData(), amount, MSG_NOSIGNAL);
#endif
            if (sent > 0) {
                tls_to_plain.consume(static_cast<std::size_t>(sent));
                last_activity = std::chrono::steady_clock::now();
            } else if (sent == 0) {
                reason = "plain socket closed while writing";
                return false;
            } else if (!WouldBlock(LastSocketError())) {
#ifdef _WIN32
                if (LastSocketError() != WSAEINTR) {
#else
                if (LastSocketError() != EINTR) {
#endif
                    reason = "plain socket write failed";
                    return false;
                }
            }
        }

        if (tls_eof && tls_to_plain.size() == 0 && !plain_shutdown_sent) {
            ShutdownSend(plain_socket);
            plain_shutdown_sent = true;
        }

        if (((shutdown_wants_read && tls_readable) ||
             (shutdown_wants_write && tls_writable)) && !tls_shutdown_sent) {
            const int result = SSL_shutdown(tls);
            if (result >= 0) {
                tls_shutdown_sent = true;
                shutdown_wants_read = false;
                shutdown_wants_write = false;
            } else {
                const int ssl_error = SSL_get_error(tls, result);
                if (ssl_error == SSL_ERROR_WANT_WRITE) {
                    shutdown_wants_read = false;
                    shutdown_wants_write = true;
                } else if (ssl_error == SSL_ERROR_WANT_READ) {
                    shutdown_wants_read = true;
                    shutdown_wants_write = false;
                }
                else {
                    reason = "TLS shutdown failed";
                    return false;
                }
            }
        }
    }
    reason = "both tunnel directions closed";
    return true;
}
} // namespace wa
