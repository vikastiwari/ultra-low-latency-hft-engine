#ifndef ITCH_PARSER_H
#define ITCH_PARSER_H

#include <cstdint>
#include <rte_byteorder.h>

// If not running within DPDK environment, stub rte_byteorder
// Actually, rte_byteorder.h is standalone enough, but if tests run without DPDK EAL, we might just rely on standard arpa/inet.h
// Let's use standard POSIX for pure logic if needed, or stick to rte_byteorder since it's a DPDK project.

#include <rte_memory.h> // For __rte_cache_aligned

// NASDAQ TotalView-ITCH 5.0 'Add Order - No MPID' Message (Type 'A')
struct ITCH_AddOrder {
    uint8_t  message_type;      // 'A'
    uint16_t stock_locate;      // 2 bytes
    uint16_t tracking_number;   // 2 bytes
    uint8_t  timestamp[6];      // 6 bytes timestamp
    uint64_t order_reference_number; // 8 bytes
    uint8_t  buy_sell_indicator;// 1 byte
    uint32_t shares;            // 4 bytes
    char     stock[8];          // 8 bytes
    uint32_t price;             // 4 bytes
} __attribute__((packed));

// Lightweight internal structure aligned to a 64-byte cache line
struct alignas(64) NormalizedOrder {
    uint64_t order_reference_number;
    uint64_t timestamp;
    uint32_t shares;
    uint32_t price;
    char stock[8];
    char side; // 'B' or 'S'
    uint64_t ingress_tsc; // Timestamp counter at packet ingress
} __rte_cache_aligned;

class ITCHParser {
public:
    /**
     * @brief Parses a raw ITCH payload into a NormalizedOrder.
     * 
     * @param payload Pointer to the raw UDP payload.
     * @param length Length of the payload.
     * @param ingress_tsc Timestamp counter at ingress.
     * @param out_order The order to populate if successful.
     * @return true if successfully parsed, false if invalid or not an AddOrder.
     */
    static bool parse_add_order(const uint8_t* payload, uint16_t length, uint64_t ingress_tsc, NormalizedOrder& out_order);
};

#endif // ITCH_PARSER_H
