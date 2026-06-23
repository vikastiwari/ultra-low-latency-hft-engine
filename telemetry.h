#ifndef TELEMETRY_H
#define TELEMETRY_H

#include <rte_ring.h>
#include <cstdint>

class TelemetryEngine {
public:
    TelemetryEngine();
    ~TelemetryEngine();

    bool initialize();
    
    /**
     * @brief Background loop to aggregate histograms.
     */
    void run();

    /**
     * @brief Extremely fast inline method to record latency in the hot path.
     */
    inline void record_latency(uint64_t latency_cycles) {
        // Enqueue directly to the non-blocking telemetry ring.
        // We cast the uint64_t into a void* to store it directly without allocations.
        rte_ring_sp_enqueue(m_latency_ring, (void*)(uintptr_t)latency_cycles);
    }

private:
    struct rte_ring* m_latency_ring;
};

// C-style entry point for rte_eal_remote_launch
extern "C" int telemetry_thread_main(void* arg);

#endif // TELEMETRY_H
