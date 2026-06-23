#include <gtest/gtest.h>
#include <cstring>
#include "../itch_parser.h"
#include <arpa/inet.h>

// Note: rte_byteorder.h usually assumes a little-endian x86 host, 
// so rte_be_to_cpu_32 acts exactly like ntohl. 

TEST(ITCHParserTest, InvalidLength) {
    uint8_t payload[10]; // Too short
    NormalizedOrder order;
    bool success = ITCHParser::parse_add_order(payload, sizeof(payload), 12345, order);
    EXPECT_FALSE(success);
}

TEST(ITCHParserTest, InvalidMessageType) {
    ITCH_AddOrder msg;
    std::memset(&msg, 0, sizeof(msg));
    msg.message_type = 'X'; // Not 'A'
    
    NormalizedOrder order;
    bool success = ITCHParser::parse_add_order((const uint8_t*)&msg, sizeof(msg), 12345, order);
    EXPECT_FALSE(success);
}

TEST(ITCHParserTest, ValidAddOrderParsing) {
    ITCH_AddOrder msg;
    std::memset(&msg, 0, sizeof(msg));
    msg.message_type = 'A';
    
    // Set 8-byte Reference Number = 1, Big Endian
    // In memory: 00 00 00 00 00 00 00 01
    msg.order_reference_number = htobe64(1);
    
    // Set shares = 500, Big Endian
    msg.shares = htonl(500);
    
    // Set price = 15000, Big Endian
    msg.price = htonl(15000);
    
    // Set 6-byte timestamp to something recognizable: e.g. 0x010203040506
    msg.timestamp[0] = 0x01;
    msg.timestamp[1] = 0x02;
    msg.timestamp[2] = 0x03;
    msg.timestamp[3] = 0x04;
    msg.timestamp[4] = 0x05;
    msg.timestamp[5] = 0x06;
    
    msg.buy_sell_indicator = 'B';
    std::memcpy(msg.stock, "AAPL    ", 8);

    NormalizedOrder order;
    bool success = ITCHParser::parse_add_order((const uint8_t*)&msg, sizeof(msg), 99999, order);
    
    EXPECT_TRUE(success);
    EXPECT_EQ(order.order_reference_number, 1);
    EXPECT_EQ(order.shares, 500);
    EXPECT_EQ(order.price, 15000);
    EXPECT_EQ(order.timestamp, 0x010203040506ULL);
    EXPECT_EQ(order.side, 'B');
    EXPECT_EQ(order.ingress_tsc, 99999);
    EXPECT_EQ(std::memcmp(order.stock, "AAPL    ", 8), 0);
}
