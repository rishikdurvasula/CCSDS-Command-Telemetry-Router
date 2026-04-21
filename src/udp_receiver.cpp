#include "ccsds/space_packet.hpp"

#include <arpa/inet.h>
#include <cstdint>
#include <iostream>
#include <string>
#include <unistd.h>
#include <vector>

using namespace ccsds;

namespace {

constexpr std::uint16_t TM_APID = 1;
constexpr std::uint16_t TC_APID = 100;
constexpr std::uint16_t ACK_APID = 101;

std::string packet_type_to_string(PacketType t) {
    return (t == PacketType::Telemetry) ? "TM" : "TC";
}

std::vector<std::uint8_t> make_ack_payload(std::uint16_t seq, std::uint8_t status) {
    return {
        static_cast<std::uint8_t>((seq >> 8U) & 0xFFU),
        static_cast<std::uint8_t>(seq & 0xFFU),
        status
    };
}

}  // namespace

int main() {
    int rx_sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (rx_sock < 0) {
        std::cerr << "Failed to create UDP receive socket\n";
        return 1;
    }

    sockaddr_in rx_addr{};
    rx_addr.sin_family = AF_INET;
    rx_addr.sin_port = htons(9000);
    rx_addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(rx_sock, reinterpret_cast<sockaddr*>(&rx_addr), sizeof(rx_addr)) < 0) {
        std::cerr << "Failed to bind UDP receiver on port 9000\n";
        close(rx_sock);
        return 1;
    }

    int ack_sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (ack_sock < 0) {
        std::cerr << "Failed to create ACK transmit socket\n";
        close(rx_sock);
        return 1;
    }

    sockaddr_in ack_addr{};
    ack_addr.sin_family = AF_INET;
    ack_addr.sin_port = htons(9001);
    inet_pton(AF_INET, "127.0.0.1", &ack_addr.sin_addr);

    std::uint8_t buffer[4096];

    std::uint16_t last_tm_seq = 0;
    bool first_tm = true;
    std::uint16_t ack_seq = 0;

    while (true) {
        const ssize_t len = recv(rx_sock, buffer, sizeof(buffer), 0);
        if (len <= 0) {
            continue;
        }

        try {
            auto pkt = SpacePacketCodec::decode(
                std::span(buffer, static_cast<std::size_t>(len)),
                DecodeOptions{.expect_crc16_trailer = true}
            );
            // forward raw packet to Python ingestor
            int forward_sock = socket(AF_INET, SOCK_DGRAM, 0);
            sockaddr_in forward_addr{};
            forward_addr.sin_family = AF_INET;
            forward_addr.sin_port = htons(9200);
            inet_pton(AF_INET, "127.0.0.1", &forward_addr.sin_addr);

            sendto(forward_sock, buffer, len, 0,
                reinterpret_cast<sockaddr*>(&forward_addr),
                sizeof(forward_addr));

            close(forward_sock);

            const auto type = pkt.primary_header.packet_type;
            const auto seq = pkt.primary_header.sequence_count;
            const auto apid = pkt.primary_header.apid;

            std::cout
                << "[receiver] type=" << packet_type_to_string(type)
                << " apid=" << apid
                << " seq=" << seq
                << " bytes=" << pkt.packet_data_field.size()
                << "\n";

            if (type == PacketType::Telemetry && apid == TM_APID) {
                if (!first_tm) {
                    const std::uint16_t expected =
                        static_cast<std::uint16_t>((last_tm_seq + 1U) & 0x3FFFU);
                    if (seq != expected) {
                        std::cout
                            << "[receiver] GAP detected for TM: expected "
                            << expected << " got " << seq << "\n";
                    }
                }

                first_tm = false;
                last_tm_seq = seq;
            } else if (type == PacketType::Telecommand && apid == TC_APID) {
                std::cout << "[receiver] TC payload: ";
                for (std::uint8_t b : pkt.packet_data_field) {
                    if (b >= 32 && b <= 126) {
                        std::cout << static_cast<char>(b);
                    } else {
                        std::cout << ".";
                    }
                }
                std::cout << "\n";

                SpacePacket ack_pkt;
                ack_pkt.primary_header.version = 0;
                ack_pkt.primary_header.packet_type = PacketType::Telemetry;
                ack_pkt.primary_header.secondary_header_flag = false;
                ack_pkt.primary_header.apid = ACK_APID;
                ack_pkt.primary_header.sequence_flags = SequenceFlags::Unsegmented;
                ack_pkt.primary_header.sequence_count = ack_seq++;
                ack_pkt.packet_data_field = make_ack_payload(seq, 1);
                ack_pkt.primary_header.packet_data_length =
                    static_cast<std::uint16_t>(ack_pkt.packet_data_field.size() - 1U);

                auto encoded_ack = SpacePacketCodec::encode(
                    ack_pkt,
                    EncodeOptions{.append_crc16_trailer = true}
                );

                const ssize_t sent = sendto(
                    ack_sock,
                    encoded_ack.bytes.data(),
                    encoded_ack.bytes.size(),
                    0,
                    reinterpret_cast<sockaddr*>(&ack_addr),
                    sizeof(ack_addr)
                );

                if (sent >= 0) {
                    std::cout << "[receiver] ACK sent for TC seq=" << seq << "\n";
                } else {
                    std::cout << "[receiver] ACK send failed for TC seq=" << seq << "\n";
                }
            }

        } catch (const std::exception& e) {
            std::cout << "[receiver] Packet error: " << e.what() << "\n";
        }
    }

    close(ack_sock);
    close(rx_sock);
    return 0;
}