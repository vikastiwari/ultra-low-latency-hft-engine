#include "telemetry.h"
#include <iostream>
#include <rte_lcore.h>
#include <rte_cycles.h>
#include <vector>
#include <algorithm>

extern volatile bool g_force_quit;

TelemetryEngine::TelemetryEngine() : m_latency_ring(nullptr) {}

TelemetryEngine::~TelemetryEngine() {}

bool TelemetryEngine::initialize() {
    // Single-producer (TX Thread) to Single-consumer (Telemetry Thread)
    m_latency_ring = rte_ring_create("telemetry_ring", 65536, rte_socket_id(), RING_F_SP_ENQ | RING_F_SC_DEQ);
    if (!m_latency_ring) {
        std::cerr << "Failed to create telemetry ring" << std::endl;
        return false;
    }
    return true;
}

void TelemetryEngine::run() {
    std::vector<uint64_t> latencies;
    latencies.reserve(10000000); // Pre-allocate to avoid latency spikes
    std::cout << "[Telemetry] Background aggregator started on lcore " << rte_lcore_id() << std::endl;
    
    uint64_t total_cycles = 0;
    uint64_t count = 0;

    while (!g_force_quit) {
        void* msg = nullptr;
        if (rte_ring_sc_dequeue(m_latency_ring, &msg) == 0) {
            uint64_t cycles = (uint64_t)(uintptr_t)msg;
            latencies.push_back(cycles);
            total_cycles += cycles;
            count++;

            // Periodically print the average (e.g., every 10,000 orders)
            // In production, this would export to Prometheus, InfluxDB, or a file.
            if (count % 10000 == 0) {
                std::cout << "[Telemetry] Sampled 10,000 trades. Avg Tick-to-Trade Latency: " 
                          << (total_cycles / count) << " CPU cycles." << std::endl;
            }
        }
    }

    // Flush any remaining items in the ring upon shutdown
    void* msg = nullptr;
    while (rte_ring_sc_dequeue(m_latency_ring, &msg) == 0) {
        latencies.push_back((uint64_t)(uintptr_t)msg);
    }

    if (!latencies.empty()) {
        std::sort(latencies.begin(), latencies.end());
        uint64_t hz = rte_get_tsc_hz();
        auto to_ns = [hz](uint64_t c) { return (c * 1000000000ULL) / hz; };
        
        std::cout << "\n========================================\n";
        std::cout << "        HFT TELEMETRY REPORT\n";
        std::cout << "========================================\n";
        std::cout << "Total Orders Fired : " << latencies.size() << "\n";
        std::cout << "p50 Latency (ns)   : " << to_ns(latencies[latencies.size() * 0.50]) << " ns\n";
        std::cout << "p90 Latency (ns)   : " << to_ns(latencies[latencies.size() * 0.90]) << " ns\n";
        std::cout << "p99 Latency (ns)   : " << to_ns(latencies[latencies.size() * 0.99]) << " ns\n";
        std::cout << "========================================\n";
    }
}

extern "C" int telemetry_thread_main(void* arg) {
    TelemetryEngine* engine = static_cast<TelemetryEngine*>(arg);
    if (engine) engine->run();
    return 0;
}
