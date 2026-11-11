#pragma once

#include <atomic>
#include <cstdint>

struct ConnectionStats {
    std::uint64_t accepted_connections = 0;
    std::uint64_t rejected_connections = 0;
    std::uint64_t active_connections = 0;
    std::uint64_t bytes_received = 0;
    std::uint64_t bytes_sent = 0;
};

class ConnectionManager {
public:
    void BeginConnection();
    void EndConnection();
    void RecordRejected();
    void AddBytesReceived(std::uint64_t count);
    void AddBytesSent(std::uint64_t count);
    ConnectionStats Snapshot() const;

private:
    std::atomic<std::uint64_t> accepted_{0};
    std::atomic<std::uint64_t> rejected_{0};
    std::atomic<std::uint64_t> active_{0};
    std::atomic<std::uint64_t> received_{0};
    std::atomic<std::uint64_t> sent_{0};
};
