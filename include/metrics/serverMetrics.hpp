#pragma once

#include <atomic>
#include <chrono>

struct ServerMetrics {
    using Clock = std::chrono::steady_clock;

    ServerMetrics() : startTime(Clock::now()) {}

    void onConnectionAccepted() {
        totalConnectionsReceived.fetch_add(1, std::memory_order_relaxed);
        connectedClients.fetch_add(1, std::memory_order_relaxed);
    }

    void onConnectionClosed() {
        long long current = connectedClients.load(std::memory_order_relaxed);
        while (current > 0 &&
               !connectedClients.compare_exchange_weak(
                   current, current - 1, std::memory_order_relaxed, std::memory_order_relaxed)) {
        }
    }

    void onCommandProcessed() {
        totalCommandsProcessed.fetch_add(1, std::memory_order_relaxed);
    }

    long long uptimeSeconds() const {
        const auto now = Clock::now();
        return std::chrono::duration_cast<std::chrono::seconds>(now - startTime).count();
    }

    Clock::time_point startTime;
    std::atomic<int> tcpPort {0};
    std::atomic<long long> connectedClients {0};
    std::atomic<long long> totalConnectionsReceived {0};
    std::atomic<long long> totalCommandsProcessed {0};
};
