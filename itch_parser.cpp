#include "itch_parser.h"

// Define a safe fallback if __builtin_memcpy isn't available
#include <cstring>

bool ITCHParser::parse_add_order(const uint8_t* payload, uint16_t length, uint64_t ingress_tsc, NormalizedOrder& out_order) {
    if (length < sizeof(ITCH_AddOrder)) {
        return false; // Too short to be a valid ITCH_AddOrder message
    }

    const ITCH_AddOrder* msg = reinterpret_cast<const ITCH_AddOrder*>(payload);
    
    // Check if it is an Add Order - No MPID message (Type 'A')
    if (msg->message_type != 'A') {
        return false;
    }

    // Perform Big-Endian to CPU endianness conversion
    out_order.order_reference_number = rte_be_to_cpu_64(msg->order_reference_number);
    out_order.shares = rte_be_to_cpu_32(msg->shares);
    out_order.price = rte_be_to_cpu_32(msg->price);
    
    // 6-byte big endian timestamp extraction using explicit shifting.
    out_order.timestamp = ((uint64_t)msg->timestamp[0] << 40) |
                          ((uint64_t)msg->timestamp[1] << 32) |
                          ((uint64_t)msg->timestamp[2] << 24) |
                          ((uint64_t)msg->timestamp[3] << 16) |
                          ((uint64_t)msg->timestamp[4] <<  8) |
                          ((uint64_t)msg->timestamp[5]);
                          
    out_order.side = msg->buy_sell_indicator;
    
    // Copy the 8-byte ASCII stock symbol
    std::memcpy(out_order.stock, msg->stock, 8);
    
    out_order.ingress_tsc = ingress_tsc;
    return true;
}
