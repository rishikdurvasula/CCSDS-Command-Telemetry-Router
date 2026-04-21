#include "ccsds/crc16.hpp"
#include "ccsds/space_packet.hpp"

#include <cstdint>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

using ccsds::Crc16CcittFalse;
using ccsds::DecodeOptions;
using ccsds::EncodeOptions;
using ccsds::PacketType;
using ccsds::SequenceFlags;
using ccsds::SpacePacket;
using ccsds::SpacePacketCodec;

namespace {

void expect_true(bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

template <typename Fn>
void expect_throws(Fn&& fn, const std::string& expected_substring) {
    try {
        fn();
    } catch (const std::exception& ex) {
        const std::string msg = ex.what();
        if (msg.find(expected_substring) == std::string::npos) {
            throw std::runtime_error("exception thrown, but message mismatch: " + msg);
        }
        return;
    }
    throw std::runtime_error("expected exception but none was thrown");
}

SpacePacket make_basic_tm_packet() {
    SpacePacket pkt;
    pkt.primary_header.version = 0;
    pkt.primary_header.packet_type = PacketType::Telemetry;
    pkt.primary_header.secondary_header_flag = false;
    pkt.primary_header.apid = 42;
    pkt.primary_header.sequence_flags = SequenceFlags::Unsegmented;
    pkt.primary_header.sequence_count = 7;
    pkt.packet_data_field = {0x10, 0x20, 0x30, 0x40};
    pkt.primary_header.packet_data_length =
        static_cast<std::uint16_t>(pkt.packet_data_field.size() - 1U);
    return pkt;
}

void test_crc_known_vector() {
    const std::vector<std::uint8_t> bytes = {'1','2','3','4','5','6','7','8','9'};
    const auto crc = Crc16CcittFalse::compute(bytes);
    expect_true(crc == 0x29B1, "CRC known vector failed");
}

void test_round_trip_without_crc() {
    const SpacePacket pkt = make_basic_tm_packet();

    const auto encoded = SpacePacketCodec::encode(pkt);
    expect_true(!encoded.crc16_ccitt_false.has_value(), "CRC should not be present");

    const auto decoded = SpacePacketCodec::decode(encoded.bytes);

    expect_true(decoded.primary_header.version == 0, "version mismatch");
    expect_true(decoded.primary_header.packet_type == PacketType::Telemetry, "packet type mismatch");
    expect_true(decoded.primary_header.apid == 42, "apid mismatch");
    expect_true(decoded.primary_header.sequence_count == 7, "sequence count mismatch");
    expect_true(decoded.packet_data_field == std::vector<std::uint8_t>({0x10, 0x20, 0x30, 0x40}),
                "packet data field mismatch");
}

void test_round_trip_with_crc() {
    const SpacePacket pkt = make_basic_tm_packet();

    const auto encoded = SpacePacketCodec::encode(
        pkt,
        EncodeOptions{.append_crc16_trailer = true}
    );

    expect_true(encoded.crc16_ccitt_false.has_value(), "CRC should be present");
    expect_true(encoded.bytes.size() == 12, "encoded size with CRC mismatch");

    const auto decoded = SpacePacketCodec::decode(
        encoded.bytes,
        DecodeOptions{.expect_crc16_trailer = true}
    );

    expect_true(decoded.primary_header.apid == 42, "apid mismatch");
    expect_true(decoded.packet_data_field == std::vector<std::uint8_t>({0x10, 0x20, 0x30, 0x40}),
                "decoded data mismatch");
}

void test_crc_failure_on_corruption() {
    const SpacePacket pkt = make_basic_tm_packet();

    auto encoded = SpacePacketCodec::encode(
        pkt,
        EncodeOptions{.append_crc16_trailer = true}
    );

    encoded.bytes[6] ^= 0xFFU;

    expect_throws(
        [&]() {
            (void)SpacePacketCodec::decode(
                encoded.bytes,
                DecodeOptions{.expect_crc16_trailer = true}
            );
        },
        "CRC validation failed"
    );
}

void test_header_bit_packing() {
    SpacePacket pkt;
    pkt.primary_header.version = 0;
    pkt.primary_header.packet_type = PacketType::Telecommand;
    pkt.primary_header.secondary_header_flag = true;
    pkt.primary_header.apid = 0x07FF;
    pkt.primary_header.sequence_flags = SequenceFlags::LastSegment;
    pkt.primary_header.sequence_count = 0x3FFF;
    pkt.packet_data_field = {0x99};
    pkt.primary_header.packet_data_length = 0;

    const auto encoded = SpacePacketCodec::encode(pkt);

    expect_true(encoded.bytes.size() == 7, "encoded size mismatch");
    expect_true(encoded.bytes[0] == 0x1F, "packet id high byte mismatch");
    expect_true(encoded.bytes[1] == 0xFF, "packet id low byte mismatch");
    expect_true(encoded.bytes[2] == 0xBF, "sequence control high byte mismatch");
    expect_true(encoded.bytes[3] == 0xFF, "sequence control low byte mismatch");
    expect_true(encoded.bytes[4] == 0x00, "length high byte mismatch");
    expect_true(encoded.bytes[5] == 0x00, "length low byte mismatch");
    expect_true(encoded.bytes[6] == 0x99, "payload byte mismatch");
}

void test_invalid_packet_data_length_rejected() {
    SpacePacket pkt = make_basic_tm_packet();
    pkt.primary_header.packet_data_length = 0;

    expect_throws(
        [&]() {
            (void)SpacePacketCodec::encode(pkt);
        },
        "packet_data_length does not match"
    );
}

void test_too_small_buffer_rejected() {
    const std::vector<std::uint8_t> bad = {0x00, 0x01, 0x02};

    expect_throws(
        [&]() {
            (void)SpacePacketCodec::decode(bad);
        },
        "buffer smaller than CCSDS primary header"
    );
}

void test_declared_length_mismatch_rejected() {
    auto pkt = make_basic_tm_packet();
    auto encoded = SpacePacketCodec::encode(pkt);

    encoded.bytes.pop_back();

    expect_throws(
        [&]() {
            (void)SpacePacketCodec::decode(encoded.bytes);
        },
        "buffer size does not match packet_data_length"
    );
}

void test_max_packet_data_field_size() {
    SpacePacket pkt;
    pkt.primary_header.version = 0;
    pkt.primary_header.packet_type = PacketType::Telemetry;
    pkt.primary_header.secondary_header_flag = false;
    pkt.primary_header.apid = 1;
    pkt.primary_header.sequence_flags = SequenceFlags::Unsegmented;
    pkt.primary_header.sequence_count = 0;
    pkt.packet_data_field.assign(65536U, 0xABU);
    pkt.primary_header.packet_data_length = 65535U;

    const auto encoded = SpacePacketCodec::encode(pkt);
    const auto decoded = SpacePacketCodec::decode(encoded.bytes);

    expect_true(decoded.packet_data_field.size() == 65536U, "max data field size mismatch");
    expect_true(decoded.primary_header.packet_data_length == 65535U, "max packet_data_length mismatch");
}

}  // namespace

int main() {
    test_crc_known_vector();
    test_round_trip_without_crc();
    test_round_trip_with_crc();
    test_crc_failure_on_corruption();
    test_header_bit_packing();
    test_invalid_packet_data_length_rejected();
    test_too_small_buffer_rejected();
    test_declared_length_mismatch_rejected();
    test_max_packet_data_field_size();

    std::cout << "All CCSDS packet codec + CRC tests passed.\n";
    return 0;
}