#ifndef ORDER_EXECUTION_H
#define ORDER_EXECUTION_H

#include <cstdint>
#include <rte_mbuf.h>
#include <rte_ether.h>
#include <rte_ip.h>
#include <rte_udp.h>
#include <rte_byteorder.h>
#include <rte_ethdev.h>
#include <rte_cycles.h>
#include "telemetry.h"
#include <vector>

struct TradeLog {
    uint64_t timestamp;
    char side;
    uint32_t shares;
    uint32_t price;
};

// NASDAQ OUCH 4.2 'Enter Order' Message
struct OUCH_EnterOrder {
    uint8_t  message_type;      // 'O'
    char     order_token[14];   
    char     buy_sell_indicator;// 'B' or 'S'
    uint32_t shares;            
    char     stock[8];          
    uint32_t price;             
    uint32_t time_in_force;     
    char     firm[4];           
    char     display;           
    char     capacity;          
    char     intermarket_sweep; 
    char     cross_type;        
    char     customer_type;     
} __attribute__((packed));

class OrderExecutionEngine {
public:
    OrderExecutionEngine(TelemetryEngine& telemetry);
    ~OrderExecutionEngine();

    /**
     * @brief Initialize the TX mempool and pre-fill the outbound payload.
     */
    bool initialize(uint16_t port_id, bool pcap_mode);
    void flush_ledger();

    /**
     * @brief Blasts a pre-allocated order directly to the NIC.
     */
    inline void fire_order(char side, uint32_t shares, uint32_t price, uint64_t ingress_tsc);

private:
    uint16_t m_port_id;
    struct rte_mempool* m_tx_pool;
    TelemetryEngine& m_telemetry;
    bool m_pcap_mode;
    std::vector<TradeLog> m_trade_ledger;
    
    // The single, pre-allocated, pre-filled mbuf ready to be fired
    struct rte_mbuf* m_tx_mbuf;
    
    // Direct pointer to the OUCH payload deep inside m_tx_mbuf
    OUCH_EnterOrder* m_ouch_payload;
};

// Extremely tight inline hot-path implementation
inline void OrderExecutionEngine::fire_order(char side, uint32_t shares, uint32_t price, uint64_t ingress_tsc) {
    if (unlikely(m_tx_mbuf == nullptr)) return;

    // --- PRE-TRADE RISK CHECK (Fat-Finger Kill Switch) ---
    // Use unlikely to ensure the branch predictor favors the valid path.
    if (unlikely(shares > 10000 || price == 0 || price > 100000000)) {
        return; // Drop the order, it violates risk constraints!
    }

    // 1. Update ONLY the dynamic fields of the OUCH order
    m_ouch_payload->buy_sell_indicator = side;
    m_ouch_payload->shares = rte_cpu_to_be_32(shares);
    m_ouch_payload->price = rte_cpu_to_be_32(price);

    if (unlikely(m_pcap_mode)) {
        m_trade_ledger.push_back({ingress_tsc, side, shares, price});
    }

    // 2. Increment reference count so the NIC driver doesn't return this mbuf to the pool after sending
    rte_mbuf_refcnt_update(m_tx_mbuf, 1);

    struct rte_mbuf* tx_pkts[1] = { m_tx_mbuf };
    
    // --- NANOSECOND TELEMETRY ---
    // Capture exact egress TSC right before hardware transmission
    uint64_t egress_tsc = rte_rdtsc();
    m_telemetry.record_latency(egress_tsc - ingress_tsc);

    // 3. Blast out of NIC immediately, completely bypassing kernel TCP/IP stack
    rte_eth_tx_burst(m_port_id, 0, tx_pkts, 1);
}

#endif // ORDER_EXECUTION_H
