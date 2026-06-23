#ifndef NETWORK_INGRESS_H
#define NETWORK_INGRESS_H

#include <cstdint>

// Forward declaration for DPDK memory pool
struct rte_mempool;
struct rte_ring;

#include <rte_memory.h> // For __rte_cache_aligned
#include "itch_parser.h"

class DPDKReceiver {
public:
    DPDKReceiver();
    ~DPDKReceiver();

    /**
     * @brief Initialize the DPDK EAL and configure the network port for RX.
     * 
     * @param argc Argument count (passed to rte_eal_init)
     * @param argv Argument vector (passed to rte_eal_init)
     * @param port_id The NIC port ID to configure for receiving packets
     * @return true on success, false on failure
     */
    bool initialize(int argc, char** argv, uint16_t port_id);

    /**
     * @brief Core polling loop utilizing rte_eth_rx_burst.
     * 
     * This method enters an infinite loop, constantly polling the configured
     * DPDK port for new packets. Currently, it just counts and frees them.
     */
    void start_polling();

    /**
     * @brief Accessor for the strategy thread to bind to the SPSC ring.
     */
    struct rte_ring* get_market_data_ring() const { return m_market_data_ring; }

private:
    /**
     * @brief Parses the Ethernet, IPv4, and UDP headers.
     */
    inline void parse_packet(struct rte_mbuf* m, uint64_t ingress_tsc);

    /**
     * @brief Stub for processing the actual UDP payload.
     */
    inline void process_payload(const uint8_t* payload, uint16_t length, uint64_t ingress_tsc);

    uint16_t m_port_id;
    bool m_initialized;
    
    // Memory pool used by the NIC to store incoming packet data
    struct rte_mempool* m_mbuf_pool;

    // Lock-free ring for transferring data to the strategy thread
    struct rte_ring* m_market_data_ring;
};

#endif // NETWORK_INGRESS_H
