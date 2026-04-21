#include "ccsds/space_packet.hpp"
#include "ccsds/crc16.hpp"

namespace ccsds {

namespace {

std::uint16_t read_be16(std::span<const std::uint8_t> bytes, std::size_t offset) {
    return static_cast<std::uint16_t>(
        (static_cast<std::uint16_t>(bytes[offset]) << 8U) |
        static_cast<std::uint16_t>(bytes[offset + 1])
    );
}

void write_be16(std::vector<std::uint8_t>& out, std::uint16_t value) {
    out.push_back(static_cast<std::uint8_t>((value >> 8U) & 0xFFU));
    out.push_back(static_cast<std::uint8_t>(value & 0xFFU));
}

}  // namespace

void SpacePacketCodec::validate_for_encode(const SpacePacket& packet) {
    const auto& h = packet.primary_header;

    if (h.version > 0b111U) {
        throw PacketError("version must fit in 3 bits");
    }

    if (h.apid > MAX_APID) {
        throw PacketError("APID must fit in 11 bits");
    }

    if (h.sequence_count > MAX_SEQUENCE_COUNT) {
        throw PacketError("sequence_count must fit in 14 bits");
    }

    if (packet.packet_data_field.empty()) {
        throw PacketError("packet data field must contain at least 1 octet");
    }

    if (packet.packet_data_field.size() > MAX_PACKET_DATA_FIELD_SIZE) {
        throw PacketError("packet data field exceeds CCSDS maximum of 65536 octets");
    }

    const std::uint16_t expected_length =
        static_cast<std::uint16_t>(packet.packet_data_field.size() - 1U);

    if (h.packet_data_length != expected_length) {
        throw PacketError("packet_data_length does not match packet_data_field size minus one");
    }
}

EncodedPacket SpacePacketCodec::encode(const SpacePacket& packet, const EncodeOptions& options) {
    validate_for_encode(packet);

    const auto& h = packet.primary_header;

    std::uint16_t packet_id = 0;
    packet_id |= static_cast<std::uint16_t>((h.version & 0b111U) << 13U);
    packet_id |= static_cast<std::uint16_t>((static_cast<std::uint8_t>(h.packet_type) & 0b1U) << 12U);
    packet_id |= static_cast<std::uint16_t>((h.secondary_header_flag ? 1U : 0U) << 11U);
    packet_id |= static_cast<std::uint16_t>(h.apid & 0x07FFU);

    std::uint16_t sequence_control = 0;
    sequence_control |= static_cast<std::uint16_t>((static_cast<std::uint8_t>(h.sequence_flags) & 0b11U) << 14U);
    sequence_control |= static_cast<std::uint16_t>(h.sequence_count & 0x3FFFU);

    std::vector<std::uint8_t> out;
    out.reserve(PRIMARY_HEADER_SIZE + packet.packet_data_field.size() +
                (options.append_crc16_trailer ? CRC_TRAILER_SIZE : 0));

    write_be16(out, packet_id);
    write_be16(out, sequence_control);
    write_be16(out, h.packet_data_length);

    out.insert(out.end(), packet.packet_data_field.begin(), packet.packet_data_field.end());

    std::optional<std::uint16_t> crc;

    if (options.append_crc16_trailer) {
        crc = Crc16CcittFalse::compute(out);
        write_be16(out, *crc);
    }

    return EncodedPacket{
        .bytes = std::move(out),
        .crc16_ccitt_false = crc
    };
}

SpacePacket SpacePacketCodec::decode(std::span<const std::uint8_t> bytes, const DecodeOptions& options) {
    if (bytes.size() < PRIMARY_HEADER_SIZE) {
        throw PacketError("buffer smaller than CCSDS primary header");
    }

    std::span<const std::uint8_t> packet_bytes = bytes;

    if (options.expect_crc16_trailer) {
        if (bytes.size() < PRIMARY_HEADER_SIZE + CRC_TRAILER_SIZE + 1) {
            throw PacketError("buffer too small for header, data field, and CRC trailer");
        }

        const std::size_t without_crc_size = bytes.size() - CRC_TRAILER_SIZE;
        const std::uint16_t expected_crc = read_be16(bytes, without_crc_size);
        const std::uint16_t actual_crc = Crc16CcittFalse::compute(bytes.first(without_crc_size));

        if (expected_crc != actual_crc) {
            throw PacketError("CRC validation failed");
        }

        packet_bytes = bytes.first(without_crc_size);
    }

    const std::uint16_t packet_id = read_be16(packet_bytes, 0);
    const std::uint16_t sequence_control = read_be16(packet_bytes, 2);
    const std::uint16_t packet_data_length = read_be16(packet_bytes, 4);

    PrimaryHeader header;
    header.version = static_cast<std::uint8_t>((packet_id >> 13U) & 0b111U);
    header.packet_type = static_cast<PacketType>((packet_id >> 12U) & 0b1U);
    header.secondary_header_flag = ((packet_id >> 11U) & 0b1U) != 0;
    header.apid = static_cast<std::uint16_t>(packet_id & 0x07FFU);
    header.sequence_flags = static_cast<SequenceFlags>((sequence_control >> 14U) & 0b11U);
    header.sequence_count = static_cast<std::uint16_t>(sequence_control & 0x3FFFU);
    header.packet_data_length = packet_data_length;

    const std::size_t declared_data_field_size =
        static_cast<std::size_t>(packet_data_length) + 1U;

    const std::size_t total_declared_packet_size =
        PRIMARY_HEADER_SIZE + declared_data_field_size;

    if (packet_bytes.size() != total_declared_packet_size) {
        throw PacketError("buffer size does not match packet_data_length in header");
    }

    SpacePacket packet;
    packet.primary_header = header;
    packet.packet_data_field.assign(
        packet_bytes.begin() + static_cast<std::ptrdiff_t>(PRIMARY_HEADER_SIZE),
        packet_bytes.end()
    );

    return packet;
}

}  // namespace ccsds