#include "order_execution.h"
#include <iostream>
#include <cstring>
#include <fstream>

OrderExecutionEngine::OrderExecutionEngine(TelemetryEngine& telemetry) : m_port_id(0), m_tx_pool(nullptr), m_tx_mbuf(nullptr), m_ouch_payload(nullptr), m_telemetry(telemetry), m_pcap_mode(false) {}

OrderExecutionEngine::~OrderExecutionEngine() {
    flush_ledger();
    if (m_tx_mbuf) {
        rte_pktmbuf_free(m_tx_mbuf);
    }
}

bool OrderExecutionEngine::initialize(uint16_t port_id, bool pcap_mode) {
    m_port_id = port_id;
    m_pcap_mode = pcap_mode;
    if (m_pcap_mode) m_trade_ledger.reserve(1000000);

    // 1. Create a dedicated Mempool for TX packets strictly on the NIC's NUMA node
    int numa_node = rte_eth_dev_socket_id(port_id);
    if (numa_node == SOCKET_ID_ANY) numa_node = 0;

    m_tx_pool = rte_pktmbuf_pool_create("OUCH_TX_POOL", 1023, 0, 0, RTE_MBUF_DEFAULT_BUF_SIZE, numa_node);
    if (!m_tx_pool) {
        std::cerr << "Failed to create TX mempool for OUCH" << std::endl;
        return false;
    }

    // 2. Pre-fetch and construct the single reusable mbuf
    m_tx_mbuf = rte_pktmbuf_alloc(m_tx_pool);
    if (!m_tx_mbuf) {
        std::cerr << "Failed to allocate TX mbuf" << std::endl;
        return false;
    }

    // 3. Obtain pointers to the Ethernet, IPv4, UDP, and OUCH headers
    struct rte_ether_hdr* eth_hdr = rte_pktmbuf_mtod(m_tx_mbuf, struct rte_ether_hdr*);
    struct rte_ipv4_hdr* ip_hdr = (struct rte_ipv4_hdr*)(eth_hdr + 1);
    struct rte_udp_hdr* udp_hdr = (struct rte_udp_hdr*)(ip_hdr + 1);
    m_ouch_payload = (OUCH_EnterOrder*)(udp_hdr + 1);

    // Calculate standard sizes
    uint16_t eth_len = sizeof(struct rte_ether_hdr);
    uint16_t ip_len = sizeof(struct rte_ipv4_hdr);
    uint16_t udp_len = sizeof(struct rte_udp_hdr);
    uint16_t ouch_len = sizeof(OUCH_EnterOrder);
    uint16_t pkt_len = eth_len + ip_len + udp_len + ouch_len;

    // Setup mbuf metadata properties
    m_tx_mbuf->data_len = pkt_len;
    m_tx_mbuf->pkt_len = pkt_len;

    // Zero out memory block to prevent garbage
    std::memset(eth_hdr, 0, pkt_len);

    // Pre-fill static Network fields (Assuming dummy MACs/IPs for scaffold)
    eth_hdr->ether_type = rte_cpu_to_be_16(RTE_ETHER_TYPE_IPV4);
    
    ip_hdr->version_ihl = 0x45; // IPv4, 5 words
    ip_hdr->total_length = rte_cpu_to_be_16(ip_len + udp_len + ouch_len);
    ip_hdr->next_proto_id = IPPROTO_UDP;
    
    udp_hdr->dgram_len = rte_cpu_to_be_16(udp_len + ouch_len);
    // Note: Hardware checksum offloading is enabled on the NIC, bypassing CPU sum generation

    // Pre-fill static OUCH payload fields
    m_ouch_payload->message_type = 'O';
    std::memcpy(m_ouch_payload->order_token, "ORDER123456789", 14);
    std::memcpy(m_ouch_payload->stock, "AAPL    ", 8);
    m_ouch_payload->time_in_force = rte_cpu_to_be_32(99999);
    std::memcpy(m_ouch_payload->firm, "FIRM", 4);
    m_ouch_payload->display = 'Y';
    m_ouch_payload->capacity = 'P';
    m_ouch_payload->intermarket_sweep = 'N';
    m_ouch_payload->cross_type = 'N';
    m_ouch_payload->customer_type = 'R';

    return true;
}

void OrderExecutionEngine::flush_ledger() {
    if (!m_pcap_mode || m_trade_ledger.empty()) return;
    std::cout << "[Execution] Flushing Trade Ledger to trades_ledger.csv..." << std::endl;
    std::ofstream out("trades_ledger.csv");
    out << "timestamp,side,shares,price\n";
    for (const auto& t : m_trade_ledger) {
        out << t.timestamp << "," << t.side << "," << t.shares << "," << t.price << "\n";
    }
}
