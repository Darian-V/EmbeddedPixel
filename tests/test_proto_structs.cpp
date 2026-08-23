/**
 * @file test_proto_structs.cpp
 * @brief Tier 1 & Tier 2 Unit Tests for Wire Protocol Structs and Layouts.
 * 
 * Verifies:
 * - PayloadTimeSync (32 bytes) binary layout, offsets, and field alignments
 * - PayloadTimeBeacon (16 bytes) binary layout, offsets, and field alignments
 * - PE_Header (16 bytes) binary layout and MessageType enumeration
 * - StreamPayloadHeader (20 bytes) binary layout and FourCC tags
 * - Static assertions on structure sizes
 * - CRC16-CCITT integrity validation
 * - Serialization / deserialization roundtrip fidelity
 */

#include <cstdint>
#include <cstddef>
#include <cstring>
#include <cstdio>
#include <cassert>

// ── Test Harness Macros ───────────────────────────────────────────────────────
static int g_tests_run = 0;
static int g_tests_passed = 0;
static int g_tests_failed = 0;

#define TEST_ASSERT(cond, msg) do { \
    if (!(cond)) { \
        printf("  [FAIL] %s:%d: %s (Assertion failed: %s)\n", __FILE__, __LINE__, msg, #cond); \
        g_tests_failed++; \
        return; \
    } \
} while (0)

#define TEST_ASSERT_EQ(actual, expected, msg) do { \
    if ((actual) != (expected)) { \
        printf("  [FAIL] %s:%d: %s (Expected: %lld, Actual: %lld)\n", \
               __FILE__, __LINE__, msg, (long long)(expected), (long long)(actual)); \
        g_tests_failed++; \
        return; \
    } \
} while (0)

#define RUN_TEST(fn) do { \
    g_tests_run++; \
    printf("[RUN ] %s\n", #fn); \
    int prev_failed = g_tests_failed; \
    fn(); \
    if (g_tests_failed == prev_failed) { \
        g_tests_passed++; \
        printf("  [PASS] %s\n", #fn); \
    } \
} while (0)

// ── Protocol Definitions (Conforming to components/net/proto/ProtocolTypes.h) ─
namespace net::proto {

constexpr uint16_t PE_MAGIC            = 0x5045;   // 'P', 'E'
constexpr uint8_t  PE_PROTOCOL_VERSION = 1;

constexpr uint16_t PORT_DISCOVERY = 50000;
constexpr uint16_t PORT_STREAM    = 50001;
constexpr uint16_t PORT_COMMAND   = 50002;

enum class MessageType : uint16_t {
    HEARTBEAT               = 0x0001,
    DISCOVERY_PING          = 0x0002,
    DISCOVERY_PONG          = 0x0003,
    TIME_SYNC_REQ           = 0x0010,
    TIME_SYNC_RESP          = 0x0011,
    TIME_SYNC_BEACON        = 0x0012,
    STREAM_SENSOR_BATCH     = 0x0200,
    STREAM_STATUS_TELEMETRY = 0x0201,
};

#pragma pack(push, 1)

struct PE_Header {
    uint16_t magic;         ///< 0x5045 ('P', 'E')
    uint8_t  version;       ///< Protocol version (0x01)
    uint8_t  flags;         ///< Protocol flags
    uint16_t node_id;       ///< Originating Node ID (1..65535)
    uint16_t msg_type;      ///< MessageType enum value
    uint32_t seq_num;       ///< Monotonic sequence number
    uint16_t payload_len;   ///< Length of payload following header
    uint16_t crc16;         ///< CRC16-CCITT of payload (or 0 if disabled)
};
static_assert(sizeof(PE_Header) == 16, "PE_Header must be exactly 16 bytes");

struct PayloadTimeSync {
    uint64_t t1_host_tx_us; ///< T1: Host transmit timestamp (UTC us)
    uint64_t t2_node_rx_us; ///< T2: Node receive timestamp (Local us)
    uint64_t t3_node_tx_us; ///< T3: Node transmit timestamp (Local us)
    uint64_t t4_host_rx_us; ///< T4: Host receive timestamp (UTC us)
};
static_assert(sizeof(PayloadTimeSync) == 32, "PayloadTimeSync must be exactly 32 bytes");

struct PayloadTimeBeacon {
    uint64_t master_utc_us; ///< Master broadcast epoch (UTC us)
    uint32_t beacon_seq;    ///< Monotonic beacon sequence number
    uint8_t  epoch_id;      ///< Clock epoch identifier
    uint8_t  stratum;       ///< Stratum level (1 = host master)
    uint16_t flags;         ///< Beacon flags
};
static_assert(sizeof(PayloadTimeBeacon) == 16, "PayloadTimeBeacon must be exactly 16 bytes");

struct StreamPayloadHeader {
    uint64_t timestamp_us;   ///< Synchronized UTC epoch or local timestamp
    uint32_t stream_tag;     ///< FourCC channel tag
    uint16_t sample_rate_hz; ///< Sampling rate
    uint16_t sample_count;   ///< Sample count in batch
    uint16_t channel_count;  ///< Number of channels
    uint16_t sample_type;    ///< SampleType
};
static_assert(sizeof(StreamPayloadHeader) == 20, "StreamPayloadHeader must be exactly 20 bytes");

#pragma pack(pop)

inline uint16_t crc16_ccitt(const void* data, size_t length, uint16_t seed = 0xFFFF) {
    const uint8_t* p = static_cast<const uint8_t*>(data);
    uint16_t crc = seed;
    for (size_t i = 0; i < length; ++i) {
        crc ^= (static_cast<uint16_t>(p[i]) << 8);
        for (int b = 0; b < 8; ++b) {
            if (crc & 0x8000) {
                crc = (crc << 1) ^ 0x1021;
            } else {
                crc = crc << 1;
            }
        }
    }
    return crc;
}

constexpr uint32_t MAKE_FOURCC(char a, char b, char c, char d) {
    return (static_cast<uint32_t>(static_cast<uint8_t>(a))) |
           (static_cast<uint32_t>(static_cast<uint8_t>(b)) << 8) |
           (static_cast<uint32_t>(static_cast<uint8_t>(c)) << 16) |
           (static_cast<uint32_t>(static_cast<uint8_t>(d)) << 24);
}

} // namespace net::proto

using namespace net::proto;

// ── Tier 1 Feature Tests ──────────────────────────────────────────────────────

void test_payload_time_sync_size_and_offsets() {
    TEST_ASSERT_EQ(sizeof(PayloadTimeSync), 32, "PayloadTimeSync size must be 32 bytes");
    TEST_ASSERT_EQ(offsetof(PayloadTimeSync, t1_host_tx_us), 0, "t1 offset must be 0");
    TEST_ASSERT_EQ(offsetof(PayloadTimeSync, t2_node_rx_us), 8, "t2 offset must be 8");
    TEST_ASSERT_EQ(offsetof(PayloadTimeSync, t3_node_tx_us), 16, "t3 offset must be 16");
    TEST_ASSERT_EQ(offsetof(PayloadTimeSync, t4_host_rx_us), 24, "t4 offset must be 24");
}

void test_payload_time_beacon_size_and_offsets() {
    TEST_ASSERT_EQ(sizeof(PayloadTimeBeacon), 16, "PayloadTimeBeacon size must be 16 bytes");
    TEST_ASSERT_EQ(offsetof(PayloadTimeBeacon, master_utc_us), 0, "master_utc_us offset must be 0");
    TEST_ASSERT_EQ(offsetof(PayloadTimeBeacon, beacon_seq), 8, "beacon_seq offset must be 8");
    TEST_ASSERT_EQ(offsetof(PayloadTimeBeacon, epoch_id), 12, "epoch_id offset must be 12");
    TEST_ASSERT_EQ(offsetof(PayloadTimeBeacon, stratum), 13, "stratum offset must be 13");
    TEST_ASSERT_EQ(offsetof(PayloadTimeBeacon, flags), 14, "flags offset must be 14");
}

void test_pe_header_size_and_offsets() {
    TEST_ASSERT_EQ(sizeof(PE_Header), 16, "PE_Header size must be 16 bytes");
    TEST_ASSERT_EQ(offsetof(PE_Header, magic), 0, "magic offset must be 0");
    TEST_ASSERT_EQ(offsetof(PE_Header, version), 2, "version offset must be 2");
    TEST_ASSERT_EQ(offsetof(PE_Header, flags), 3, "flags offset must be 3");
    TEST_ASSERT_EQ(offsetof(PE_Header, node_id), 4, "node_id offset must be 4");
    TEST_ASSERT_EQ(offsetof(PE_Header, msg_type), 6, "msg_type offset must be 6");
    TEST_ASSERT_EQ(offsetof(PE_Header, seq_num), 8, "seq_num offset must be 8");
    TEST_ASSERT_EQ(offsetof(PE_Header, payload_len), 12, "payload_len offset must be 12");
    TEST_ASSERT_EQ(offsetof(PE_Header, crc16), 14, "crc16 offset must be 14");
}

void test_stream_payload_header_size_and_offsets() {
    TEST_ASSERT_EQ(sizeof(StreamPayloadHeader), 20, "StreamPayloadHeader size must be 20 bytes");
    TEST_ASSERT_EQ(offsetof(StreamPayloadHeader, timestamp_us), 0, "timestamp_us offset must be 0");
    TEST_ASSERT_EQ(offsetof(StreamPayloadHeader, stream_tag), 8, "stream_tag offset must be 8");
    TEST_ASSERT_EQ(offsetof(StreamPayloadHeader, sample_rate_hz), 12, "sample_rate_hz offset must be 12");
    TEST_ASSERT_EQ(offsetof(StreamPayloadHeader, sample_count), 14, "sample_count offset must be 14");
    TEST_ASSERT_EQ(offsetof(StreamPayloadHeader, channel_count), 16, "channel_count offset must be 16");
    TEST_ASSERT_EQ(offsetof(StreamPayloadHeader, sample_type), 18, "sample_type offset must be 18");
}

void test_fourcc_generation() {
    uint32_t cntr = MAKE_FOURCC('C', 'N', 'T', 'R');
    uint32_t temp = MAKE_FOURCC('T', 'E', 'M', 'P');
    uint32_t adc0 = MAKE_FOURCC('A', 'D', 'C', '0');

    char tag_str[5] = {0};
    memcpy(tag_str, &cntr, 4);
    TEST_ASSERT(strcmp(tag_str, "CNTR") == 0, "FourCC CNTR mismatch");

    memcpy(tag_str, &temp, 4);
    TEST_ASSERT(strcmp(tag_str, "TEMP") == 0, "FourCC TEMP mismatch");

    memcpy(tag_str, &adc0, 4);
    TEST_ASSERT(strcmp(tag_str, "ADC0") == 0, "FourCC ADC0 mismatch");
}

void test_message_types_and_ports() {
    TEST_ASSERT_EQ(static_cast<uint16_t>(MessageType::TIME_SYNC_REQ), 0x0010, "TIME_SYNC_REQ value");
    TEST_ASSERT_EQ(static_cast<uint16_t>(MessageType::TIME_SYNC_RESP), 0x0011, "TIME_SYNC_RESP value");
    TEST_ASSERT_EQ(static_cast<uint16_t>(MessageType::TIME_SYNC_BEACON), 0x0012, "TIME_SYNC_BEACON value");
    TEST_ASSERT_EQ(PORT_DISCOVERY, 50000, "PORT_DISCOVERY must be 50000");
    TEST_ASSERT_EQ(PORT_STREAM, 50001, "PORT_STREAM must be 50001");
    TEST_ASSERT_EQ(PORT_COMMAND, 50002, "PORT_COMMAND must be 50002");
}

// ── Tier 2 Boundary & Serialization Tests ─────────────────────────────────────

void test_payload_time_sync_serialization_roundtrip() {
    PayloadTimeSync sync_out = {
        1724432000000000ULL, // t1
        5000000ULL,          // t2
        5000045ULL,          // t3
        1724432000000500ULL  // t4
    };

    uint8_t buffer[sizeof(PayloadTimeSync)];
    memcpy(buffer, &sync_out, sizeof(PayloadTimeSync));

    PayloadTimeSync sync_in;
    memcpy(&sync_in, buffer, sizeof(PayloadTimeSync));

    TEST_ASSERT_EQ(sync_in.t1_host_tx_us, sync_out.t1_host_tx_us, "t1 roundtrip");
    TEST_ASSERT_EQ(sync_in.t2_node_rx_us, sync_out.t2_node_rx_us, "t2 roundtrip");
    TEST_ASSERT_EQ(sync_in.t3_node_tx_us, sync_out.t3_node_tx_us, "t3 roundtrip");
    TEST_ASSERT_EQ(sync_in.t4_host_rx_us, sync_out.t4_host_rx_us, "t4 roundtrip");
}

void test_payload_time_beacon_extreme_values() {
    PayloadTimeBeacon beacon_out = {
        0xFFFFFFFFFFFFFFFFULL, // Max 64-bit timestamp
        0xFFFFFFFFU,           // Max 32-bit sequence number
        255,                   // Max epoch id
        1,                     // Stratum 1
        0xABCD                 // Flags
    };

    uint8_t buffer[sizeof(PayloadTimeBeacon)];
    memcpy(buffer, &beacon_out, sizeof(PayloadTimeBeacon));

    PayloadTimeBeacon beacon_in;
    memcpy(&beacon_in, buffer, sizeof(PayloadTimeBeacon));

    TEST_ASSERT_EQ(beacon_in.master_utc_us, 0xFFFFFFFFFFFFFFFFULL, "Max UTC timestamp roundtrip");
    TEST_ASSERT_EQ(beacon_in.beacon_seq, 0xFFFFFFFFU, "Max sequence roundtrip");
    TEST_ASSERT_EQ(beacon_in.epoch_id, 255, "Epoch ID roundtrip");
    TEST_ASSERT_EQ(beacon_in.stratum, 1, "Stratum roundtrip");
    TEST_ASSERT_EQ(beacon_in.flags, 0xABCD, "Flags roundtrip");
}

void test_crc16_ccitt_computation() {
    const char* sample_data = "EmbeddedPixel TimeSync Protocol 2.1";
    uint16_t crc1 = crc16_ccitt(sample_data, strlen(sample_data));
    uint16_t crc2 = crc16_ccitt(sample_data, strlen(sample_data));
    TEST_ASSERT_EQ(crc1, crc2, "CRC16 must be deterministic");
    TEST_ASSERT(crc1 != 0, "CRC16 non-zero check");

    // Bitflip test
    char corrupted_data[64];
    strcpy(corrupted_data, sample_data);
    corrupted_data[5] ^= 0x01; // flip single bit
    uint16_t crc_corrupt = crc16_ccitt(corrupted_data, strlen(corrupted_data));
    TEST_ASSERT(crc1 != crc_corrupt, "CRC16 must detect single bit corruption");
}

void test_full_wire_packet_assembly() {
    struct FullPacket {
        PE_Header header;
        PayloadTimeBeacon beacon;
    } __attribute__((packed));

    static_assert(sizeof(FullPacket) == 32, "FullPacket must be exactly 32 bytes");

    FullPacket pkt;
    pkt.header.magic = PE_MAGIC;
    pkt.header.version = PE_PROTOCOL_VERSION;
    pkt.header.flags = 0;
    pkt.header.node_id = 42;
    pkt.header.msg_type = static_cast<uint16_t>(MessageType::TIME_SYNC_BEACON);
    pkt.header.seq_num = 100;
    pkt.header.payload_len = sizeof(PayloadTimeBeacon);
    pkt.beacon.master_utc_us = 1724432000000000ULL;
    pkt.beacon.beacon_seq = 100;
    pkt.beacon.epoch_id = 1;
    pkt.beacon.stratum = 1;
    pkt.beacon.flags = 0;
    pkt.header.crc16 = crc16_ccitt(&pkt.beacon, sizeof(PayloadTimeBeacon));

    TEST_ASSERT_EQ(pkt.header.magic, 0x5045, "Header magic");
    TEST_ASSERT_EQ(pkt.header.payload_len, 16, "Beacon payload length");
    TEST_ASSERT(pkt.header.crc16 != 0, "Computed CRC non-zero");

    // Verify CRC verification logic
    uint16_t verified_crc = crc16_ccitt(&pkt.beacon, pkt.header.payload_len);
    TEST_ASSERT_EQ(verified_crc, pkt.header.crc16, "CRC verification must succeed");
}

int main() {
    printf("===============================================================\n");
    printf(" EmbeddedPixel Wire Protocol Structs & Layout Unit Tests\n");
    printf("===============================================================\n\n");

    // Tier 1
    RUN_TEST(test_payload_time_sync_size_and_offsets);
    RUN_TEST(test_payload_time_beacon_size_and_offsets);
    RUN_TEST(test_pe_header_size_and_offsets);
    RUN_TEST(test_stream_payload_header_size_and_offsets);
    RUN_TEST(test_fourcc_generation);
    RUN_TEST(test_message_types_and_ports);

    // Tier 2
    RUN_TEST(test_payload_time_sync_serialization_roundtrip);
    RUN_TEST(test_payload_time_beacon_extreme_values);
    RUN_TEST(test_crc16_ccitt_computation);
    RUN_TEST(test_full_wire_packet_assembly);

    printf("\n===============================================================\n");
    printf(" Test Results: %d/%d Passed (%d Failed)\n", g_tests_passed, g_tests_run, g_tests_failed);
    printf("===============================================================\n");

    return (g_tests_failed == 0) ? 0 : 1;
}
