#include "network_ingress.h"
#include <iostream>

// DPDK specific includes
#include <rte_eal.h>
#include <rte_ethdev.h>
#include <rte_mbuf.h>
#include <rte_ether.h>
#include <rte_ip.h>
#include <rte_udp.h>
#include <rte_byteorder.h>
#include <rte_ring_elem.h>
#include <rte_flow.h>
#include <netinet/in.h>

extern volatile bool g_force_quit;

// DPDK Configuration constants
#define NUM_MBUFS 8191
#define MBUF_CACHE_SIZE 250
#define BURST_SIZE 32

DPDKReceiver::DPDKReceiver() : m_port_id(0), m_initialized(false), m_mbuf_pool(nullptr), m_market_data_ring(nullptr) {}

DPDKReceiver::~DPDKReceiver() {
    // Cleanup will be implemented here (e.g., stopping port, freeing memory pool)
}

bool DPDKReceiver::initialize(int argc, char** argv, uint16_t port_id) {
    // 1. Initialize the Environment Abstraction Layer (EAL).
    // This is mandatory for any DPDK application and sets up memory (hugepages),
    // threads, and underlying hardware resources.
    int ret = rte_eal_init(argc, argv);
    if (ret < 0) {
        std::cerr << "Error: EAL initialization failed" << std::endl;
        return false;
    }

    m_port_id = port_id;

    // 2. Create an mbuf pool.
    // In DPDK, packets are stored in memory buffers called "mbufs". The NIC uses this
    // pool to DMA incoming packets directly into userspace memory.
    // Get the exact NUMA socket the NIC is attached to
    int numa_node = rte_eth_dev_socket_id(m_port_id);
    if (numa_node == SOCKET_ID_ANY) numa_node = 0;

    m_mbuf_pool = rte_pktmbuf_pool_create("MBUF_POOL", NUM_MBUFS,
        MBUF_CACHE_SIZE, 0, RTE_MBUF_DEFAULT_BUF_SIZE, numa_node);

    if (m_mbuf_pool == nullptr) {
        std::cerr << "Error: Cannot create mbuf pool" << std::endl;
        return false;
    }

    // 2b. Create the SPSC ring for market data hand-off.
    // We use rte_ring_create_elem to enforce pass-by-value of the NormalizedOrder struct.
    m_market_data_ring = rte_ring_create_elem("market_data_ring",
        sizeof(NormalizedOrder),
        65536, // Must be power of 2
        numa_node,
        RING_F_SP_ENQ | RING_F_SC_DEQ);

    if (m_market_data_ring == nullptr) {
        std::cerr << "Error: Cannot create market data ring" << std::endl;
        return false;
    }

    // 3. Configure the Ethernet device.
    struct rte_eth_conf port_conf = {};
    // Configure the device with 1 RX queue and 1 TX queue.
    ret = rte_eth_dev_configure(m_port_id, 1, 1, &port_conf);
    if (ret < 0) {
        std::cerr << "Error: Cannot configure device: port=" << m_port_id << std::endl;
        return false;
    }

    // 4. Set up the RX queue.
    // Allocate rings for the NIC's receive queue and associate it with our mbuf pool.
    ret = rte_eth_rx_queue_setup(m_port_id, 0, 1024,
        rte_eth_dev_socket_id(m_port_id), nullptr, m_mbuf_pool);
    if (ret < 0) {
        std::cerr << "Error: RX queue setup failed: port=" << m_port_id << std::endl;
        return false;
    }

    // 5. Set up the TX queue.
    // Some drivers require setting up a TX queue even if we are only receiving.
    ret = rte_eth_tx_queue_setup(m_port_id, 0, 1024,
        rte_eth_dev_socket_id(m_port_id), nullptr);
    if (ret < 0) {
         std::cerr << "Error: TX queue setup failed: port=" << m_port_id << std::endl;
         return false;
    }

    // 6. Start the Ethernet port.
    ret = rte_eth_dev_start(m_port_id);
    if (ret < 0) {
        std::cerr << "Error: Cannot start device: port=" << m_port_id << std::endl;
        return false;
    }

    // --- MICRO-OPTIMIZATION: HARDWARE NIC FILTERING ---
    // Use rte_flow to drop packets in the ASIC before they consume PCIe bandwidth.
    struct rte_flow_error error;
    struct rte_flow_attr attr;
    struct rte_flow_item pattern[4];
    struct rte_flow_action action[2];

    std::memset(&attr, 0, sizeof(struct rte_flow_attr));
    std::memset(&pattern, 0, sizeof(pattern));
    std::memset(&action, 0, sizeof(action));

    attr.ingress = 1;

    // Action: Queue to index 0
    struct rte_flow_action_queue queue = { .index = 0 };
    action[0].type = RTE_FLOW_ACTION_TYPE_QUEUE;
    action[0].conf = &queue;
    action[1].type = RTE_FLOW_ACTION_TYPE_END;

    // Pattern 0: Ethernet (Match all)
    struct rte_flow_item_eth eth_spec = {};
    struct rte_flow_item_eth eth_mask = {};
    pattern[0].type = RTE_FLOW_ITEM_TYPE_ETH;
    pattern[0].spec = &eth_spec;
    pattern[0].mask = &eth_mask;

    // Pattern 1: IPv4 (Match Multicast 224.0.0.1)
    struct rte_flow_item_ipv4 ip_spec = {};
    struct rte_flow_item_ipv4 ip_mask = {};
    ip_spec.hdr.dst_addr = rte_cpu_to_be_32(RTE_IPV4(224, 0, 0, 1));
    ip_mask.hdr.dst_addr = RTE_BE32(0xFFFFFFFF);
    pattern[1].type = RTE_FLOW_ITEM_TYPE_IPV4;
    pattern[1].spec = &ip_spec;
    pattern[1].mask = &ip_mask;

    // Pattern 2: UDP (Match Port 12345)
    struct rte_flow_item_udp udp_spec = {};
    struct rte_flow_item_udp udp_mask = {};
    udp_spec.hdr.dst_port = rte_cpu_to_be_16(12345);
    udp_mask.hdr.dst_port = RTE_BE16(0xFFFF);
    pattern[2].type = RTE_FLOW_ITEM_TYPE_UDP;
    pattern[2].spec = &udp_spec;
    pattern[2].mask = &udp_mask;

    // Pattern 3: END
    pattern[3].type = RTE_FLOW_ITEM_TYPE_END;

    // Validate and push rule to the NIC ASIC
    if (rte_flow_validate(m_port_id, &attr, pattern, action, &error) == 0) {
        std::cout << "[DPDK] Hardware RTE Flow rule successfully programmed." << std::endl;
        rte_flow_create(m_port_id, &attr, pattern, action, &error);
    } else {
        std::cerr << "[DPDK] Hardware Flow API not supported by NIC. Falling back to promiscuous mode." << std::endl;
        rte_eth_promiscuous_enable(m_port_id);
    }

    m_initialized = true;
    return true;
}

void DPDKReceiver::start_polling() {
    if (!m_initialized) {
        std::cerr << "Error: DPDKReceiver not initialized before polling" << std::endl;
        return;
    }

    std::cout << "Starting to poll for packets on port " << m_port_id << std::endl;

    // Array of pointers to hold the batch (burst) of received packets
    struct rte_mbuf* bufs[BURST_SIZE];

    uint64_t total_packets_received = 0;

    // Core packet processing loop.
    // In an HFT context, this runs continuously on an isolated, pinned CPU core.
    while (!g_force_quit) {
        // rte_eth_rx_burst polls the NIC queue to retrieve a batch of received packets.
        // It fetches up to BURST_SIZE packets and stores their mbuf pointers in 'bufs'.
        // It returns the actual number of packets successfully retrieved.
        const uint16_t nb_rx = rte_eth_rx_burst(m_port_id, 0, bufs, BURST_SIZE);

        // unlikely() is a compiler hint (wrapper for __builtin_expect) used in DPDK 
        // to optimize branch prediction for the high-performance path.
        if (unlikely(nb_rx == 0)) {
            continue; // No packets received, try again immediately
        }

        total_packets_received += nb_rx;

        // --- MICRO-OPTIMIZATION: L1 CACHE PREFETCHING ---
        // Hide RAM latency by instructing the CPU to prefetch the packet headers 
        // into the L1 cache before the processing loop begins.
        for (uint16_t i = 0; i < nb_rx; i++) {
            rte_prefetch0(rte_pktmbuf_mtod(bufs[i], void*));
        }

        // Capture nanosecond telemetry TSC right after prefetch
        uint64_t ingress_tsc = rte_rdtsc();
        
        // Process the received burst
        for (uint16_t i = 0; i < nb_rx; i++) {
            // Parse the headers and extract the UDP payload
            parse_packet(bufs[i], ingress_tsc);
            
            // FREE THE PACKET: rte_pktmbuf_free returns the mbuf back to the memory pool.
            // If we omit this, the mbuf pool will quickly exhaust, and the NIC will drop 
            // all subsequent incoming packets because there's no memory to store them.
            rte_pktmbuf_free(bufs[i]);
        }

        // Occasional logging (usually avoided in ultra-low latency loops, but 
        // useful here to observe that the polling is working without exhausting memory).
        if (unlikely(total_packets_received % 1000000 == 0)) {
            std::cout << "Successfully received and freed " << total_packets_received << " packets." << std::endl;
        }
    }
}

inline void DPDKReceiver::parse_packet(struct rte_mbuf* m, uint64_t ingress_tsc) {
    // Extract the Ethernet header
    struct rte_ether_hdr* eth_hdr = rte_pktmbuf_mtod(m, struct rte_ether_hdr*);

    // Check if it's an IPv4 packet. In HFT, we often use rte_be_to_cpu_16 to handle endianness,
    // or we can compare against RTE_ETHER_TYPE_IPV4 directly.
    if (eth_hdr->ether_type != rte_cpu_to_be_16(RTE_ETHER_TYPE_IPV4)) {
        return; // Not an IPv4 packet, ignore
    }

    // Extract the IPv4 header
    // We offset by the size of the Ethernet header
    struct rte_ipv4_hdr* ipv4_hdr = rte_pktmbuf_mtod_offset(m, struct rte_ipv4_hdr*, sizeof(struct rte_ether_hdr));

    // Check if it's a UDP packet
    if (ipv4_hdr->next_proto_id != IPPROTO_UDP) {
        return; // Not UDP, ignore
    }

    // Extract the UDP header
    // Offset by size of Ethernet + size of IPv4 headers
    // Note: This assumes no IPv4 options. In typical HFT protocols, IP options are absent.
    // A safer but slightly slower approach is to use (ipv4_hdr->version_ihl & 0x0f) * 4.
    uint16_t ipv4_hdr_len = (ipv4_hdr->version_ihl & 0x0f) * 4;
    struct rte_udp_hdr* udp_hdr = rte_pktmbuf_mtod_offset(m, struct rte_udp_hdr*, sizeof(struct rte_ether_hdr) + ipv4_hdr_len);

    // Get the payload and its length
    // The length in the UDP header includes the header itself (8 bytes)
    uint16_t udp_len = rte_be_to_cpu_16(udp_hdr->dgram_len);
    if (udp_len <= sizeof(struct rte_udp_hdr)) {
        return; // Empty payload
    }
    
    uint16_t payload_len = udp_len - sizeof(struct rte_udp_hdr);
    
    // Pointer to the start of the payload
    const uint8_t* payload = (const uint8_t*)(udp_hdr + 1);

    // Dispatch to processing logic
    process_payload(payload, payload_len, ingress_tsc);
}

inline void DPDKReceiver::process_payload(const uint8_t* payload, uint16_t length, uint64_t ingress_tsc) {
    NormalizedOrder order;
    if (ITCHParser::parse_add_order(payload, length, ingress_tsc, order)) {
        // Enqueue directly by value into the SPSC ring
        int ret = rte_ring_sp_enqueue_elem(m_market_data_ring, &order, sizeof(NormalizedOrder));
        if (unlikely(ret < 0)) {
            // Ring is full (micro-burst exceeded 65536).
            // In a production system, we'd increment a dropped counter here.
        }
    }
}
