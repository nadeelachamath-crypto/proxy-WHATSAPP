#pragma once

#include "Socket.h"

#include <atomic>
#include <cstdint>
#include <string>

#include <openssl/ssl.h>

namespace wa {
struct RelayStats {
    std::uint64_t plain_to_tls = 0;
    std::uint64_t tls_to_plain = 0;
};

// Relays bytes between a plain TCP socket and an already-established TLS stream.
// It uses a single event loop so the SSL object is never used concurrently by threads.
bool RelayTlsAndSocket(SSL* tls, WaSocket plain_socket,
                       unsigned int idle_timeout_ms,
                       const std::atomic_bool& stop_requested,
                       RelayStats& stats, std::string& reason);
} // namespace wa
