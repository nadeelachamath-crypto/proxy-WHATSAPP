#include "ConnectionManager.h"

void ConnectionManager::BeginConnection() {
    accepted_.fetch_add(1, std::memory_order_relaxed);
    active_.fetch_add(1, std::memory_order_relaxed);
}

void ConnectionManager::EndConnection() {
    const auto current = active_.load(std::memory_order_relaxed);
    if (current > 0) {
        active_.fetch_sub(1, std::memory_order_relaxed);
    }
}

void ConnectionManager::RecordRejected() {
    accepted_.fetch_add(1, std::memory_order_relaxed);
    rejected_.fetch_add(1, std::memory_order_relaxed);
}

void ConnectionManager::AddBytesReceived(std::uint64_t count) {
    received_.fetch_add(count, std::memory_order_relaxed);
}

void ConnectionManager::AddBytesSent(std::uint64_t count) {
    sent_.fetch_add(count, std::memory_order_relaxed);
}

ConnectionStats ConnectionManager::Snapshot() const {
    ConnectionStats result;
    result.accepted_connections = accepted_.load(std::memory_order_relaxed);
    result.rejected_connections = rejected_.load(std::memory_order_relaxed);
    result.active_connections = active_.load(std::memory_order_relaxed);
    result.bytes_received = received_.load(std::memory_order_relaxed);
    result.bytes_sent = sent_.load(std::memory_order_relaxed);
    return result;
}
