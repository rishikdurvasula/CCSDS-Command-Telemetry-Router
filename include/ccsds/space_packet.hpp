#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace ccsds {

enum class PacketType : std::uint8_t {
    Telemetry = 0,
    Telecommand = 1,
};

enum class SequenceFlags : std::uint8_t {
    ContinuationSegment = 0b00,
    FirstSegment        = 0b01,
    LastSegment         = 0b10,
    Unsegmented         = 0b11,
};

struct PrimaryHeader {
    std::uint8_t version = 0;
    PacketType packet_type = PacketType::Telemetry;
    bool secondary_header_flag = false;
    std::uint16_t apid = 0;
    SequenceFlags sequence_flags = SequenceFlags::Unsegmented;
    std::uint16_t sequence_count = 0;
    std::uint16_t packet_data_length = 0;
};

struct SpacePacket {
    PrimaryHeader primary_header;
    std::vector<std::uint8_t> packet_data_field;
};

struct EncodeOptions {
    bool append_crc16_trailer = false;
};

struct DecodeOptions {
    bool expect_crc16_trailer = false;
};

struct EncodedPacket {
    std::vector<std::uint8_t> bytes;
    std::optional<std::uint16_t> crc16_ccitt_false;
};

class PacketError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

class SpacePacketCodec {
public:
    static constexpr std::size_t PRIMARY_HEADER_SIZE = 6;
    static constexpr std::size_t CRC_TRAILER_SIZE = 2;
    static constexpr std::uint16_t MAX_APID = 0x07FF;
    static constexpr std::uint16_t MAX_SEQUENCE_COUNT = 0x3FFF;
    static constexpr std::size_t MAX_PACKET_DATA_FIELD_SIZE = 65536;

    [[nodiscard]] static EncodedPacket encode(
        const SpacePacket& packet,
        const EncodeOptions& options = {}
    );

    [[nodiscard]] static SpacePacket decode(
        std::span<const std::uint8_t> bytes,
        const DecodeOptions& options = {}
    );

private:
    static void validate_for_encode(const SpacePacket& packet);
};

}  // namespace ccsds