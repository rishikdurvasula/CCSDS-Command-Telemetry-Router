#include "ccsds/lossy_channel.hpp"
#include "ccsds/space_packet.hpp"

#include <arpa/inet.h>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <map>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <unistd.h>
#include <vector>

using namespace ccsds;

namespace {

constexpr std::uint16_t TM_APID = 1;
constexpr std::uint16_t TC_APID = 100;
constexpr std::uint16_t ACK_APID = 101;

struct InFlightCommand {
    std::vector<std::uint8_t> encoded_packet;
    std::chrono::steady_clock::time_point next_deadline;
    int retry_count = 0;
    bool acked = false;
};

void print_help() {
    std::cout
        << "\nCommands:\n"
        << "  drop <0.0-1.0>\n"
        << "  corrupt <0.0-1.0>\n"
        << "  reorder <0.0-1.0>\n"
        << "  show\n"
        << "  help\n"
        << "  quit\n\n";
}

double clamp_probability(double value) {
    if (value < 0.0) return 0.0;
    if (value > 1.0) return 1.0;
    return value;
}

bool send_packet_bytes(
    int sock,
    const sockaddr_in& addr,
    LossyChannel& channel,
    const std::vector<std::uint8_t>& packet_bytes,
    std::uint16_t seq,
    const std::string& label
) {
    auto processed = channel.process_outgoing(packet_bytes);

    if (!processed.empty()) {
        const ssize_t sent = sendto(
            sock,
            processed.data(),
            processed.size(),
            0,
            reinterpret_cast<const sockaddr*>(&addr),
            sizeof(addr)
        );

        if (sent >= 0) {
            std::cout << "[sender] sent " << label << " seq=" << seq << "\n";
            return true;
        } else {
            std::cout << "[sender] sendto failed for " << label << " seq=" << seq << "\n";
            return false;
        }
    } else {
        std::cout << "[sender] dropped " << label << " seq=" << seq << "\n";
        return false;
    }
}

std::vector<std::uint8_t> make_ack_payload_ref(std::uint16_t seq, std::uint8_t status) {
    return {
        static_cast<std::uint8_t>((seq >> 8U) & 0xFFU),
        static_cast<std::uint8_t>(seq & 0xFFU),
        status
    };
}

std::uint16_t read_be16(const std::vector<std::uint8_t>& bytes, std::size_t offset) {
    return static_cast<std::uint16_t>(
        (static_cast<std::uint16_t>(bytes[offset]) << 8U) |
        static_cast<std::uint16_t>(bytes[offset + 1])
    );
}

}  // namespace

int main() {
    int tx_sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (tx_sock < 0) {
        std::cerr << "Failed to create transmit UDP socket\n";
        return 1;
    }

    sockaddr_in tx_addr{};
    tx_addr.sin_family = AF_INET;
    tx_addr.sin_port = htons(9000);
    inet_pton(AF_INET, "127.0.0.1", &tx_addr.sin_addr);

    int control_sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (control_sock < 0) {
        std::cerr << "Failed to create control UDP socket\n";
        close(tx_sock);
        return 1;
    }

    sockaddr_in control_addr{};
    control_addr.sin_family = AF_INET;
    control_addr.sin_port = htons(9100);
    control_addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(control_sock, reinterpret_cast<sockaddr*>(&control_addr), sizeof(control_addr)) < 0) {
        std::cerr << "Failed to bind control UDP socket on port 9100\n";
        close(control_sock);
        close(tx_sock);
        return 1;
    }

    int ack_sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (ack_sock < 0) {
        std::cerr << "Failed to create ACK UDP socket\n";
        close(control_sock);
        close(tx_sock);
        return 1;
    }

    sockaddr_in ack_addr{};
    ack_addr.sin_family = AF_INET;
    ack_addr.sin_port = htons(9001);
    ack_addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(ack_sock, reinterpret_cast<sockaddr*>(&ack_addr), sizeof(ack_addr)) < 0) {
        std::cerr << "Failed to bind ACK UDP socket on port 9001\n";
        close(ack_sock);
        close(control_sock);
        close(tx_sock);
        return 1;
    }

    timeval tv{};
    tv.tv_sec = 0;
    tv.tv_usec = 100000;
    setsockopt(control_sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(ack_sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    LossyChannel channel;

    ChaosConfig config;
    config.drop_probability = 0.10;
    config.corrupt_probability = 0.05;
    config.reorder_probability = 0.10;
    channel.update_config(config);

    std::atomic<bool> running = true;
    std::mutex inflight_mutex;
    std::map<std::uint16_t, InFlightCommand> inflight;

    std::thread control_thread([&]() {
        print_help();

        std::string line;
        while (running.load() && std::getline(std::cin, line)) {
            std::istringstream iss(line);
            std::string command;
            iss >> command;

            if (command == "drop") {
                double value;
                if (iss >> value) {
                    config.drop_probability = clamp_probability(value);
                    channel.update_config(config);
                    std::cout << "[control] drop_probability=" << config.drop_probability << "\n";
                } else {
                    std::cout << "[control] invalid drop value\n";
                }
            } else if (command == "corrupt") {
                double value;
                if (iss >> value) {
                    config.corrupt_probability = clamp_probability(value);
                    channel.update_config(config);
                    std::cout << "[control] corrupt_probability=" << config.corrupt_probability << "\n";
                } else {
                    std::cout << "[control] invalid corrupt value\n";
                }
            } else if (command == "reorder") {
                double value;
                if (iss >> value) {
                    config.reorder_probability = clamp_probability(value);
                    channel.update_config(config);
                    std::cout << "[control] reorder_probability=" << config.reorder_probability << "\n";
                } else {
                    std::cout << "[control] invalid reorder value\n";
                }
            } else if (command == "show") {
                std::cout
                    << "[control] current config: "
                    << "drop=" << config.drop_probability
                    << ", corrupt=" << config.corrupt_probability
                    << ", reorder=" << config.reorder_probability
                    << "\n";
            } else if (command == "help") {
                print_help();
            } else if (command == "quit") {
                running.store(false);
                break;
            } else if (!command.empty()) {
                std::cout << "[control] unknown command\n";
            }
        }

        running.store(false);
    });

    std::uint16_t telemetry_seq = 0;
    std::uint16_t command_seq = 0;

    while (running.load()) {
        // 1) Poll for injected commands from API
        std::uint8_t cmd_buffer[2048];
        sockaddr_in from_addr{};
        socklen_t from_len = sizeof(from_addr);

        const ssize_t cmd_len = recvfrom(
            control_sock,
            cmd_buffer,
            sizeof(cmd_buffer),
            0,
            reinterpret_cast<sockaddr*>(&from_addr),
            &from_len
        );

        if (cmd_len > 0) {
            SpacePacket cmd_pkt;
            cmd_pkt.primary_header.version = 0;
            cmd_pkt.primary_header.packet_type = PacketType::Telecommand;
            cmd_pkt.primary_header.secondary_header_flag = false;
            cmd_pkt.primary_header.apid = TC_APID;
            cmd_pkt.primary_header.sequence_flags = SequenceFlags::Unsegmented;
            cmd_pkt.primary_header.sequence_count = command_seq++;
            cmd_pkt.packet_data_field.assign(cmd_buffer, cmd_buffer + cmd_len);
            cmd_pkt.primary_header.packet_data_length =
                static_cast<std::uint16_t>(cmd_pkt.packet_data_field.size() - 1U);

            auto encoded = SpacePacketCodec::encode(
                cmd_pkt,
                EncodeOptions{.append_crc16_trailer = true}
            );

            std::cout << "[sender] injected command received from API, bytes=" << cmd_len << "\n";

            send_packet_bytes(
                tx_sock,
                tx_addr,
                channel,
                encoded.bytes,
                cmd_pkt.primary_header.sequence_count,
                "TC"
            );

            std::lock_guard<std::mutex> lock(inflight_mutex);
            inflight[cmd_pkt.primary_header.sequence_count] = InFlightCommand{
                .encoded_packet = encoded.bytes,
                .next_deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(500),
                .retry_count = 0,
                .acked = false
            };
        }

        // 2) Poll for ACKs
        std::uint8_t ack_buffer[2048];
        sockaddr_in ack_from{};
        socklen_t ack_from_len = sizeof(ack_from);

        const ssize_t ack_len = recvfrom(
            ack_sock,
            ack_buffer,
            sizeof(ack_buffer),
            0,
            reinterpret_cast<sockaddr*>(&ack_from),
            &ack_from_len
        );

        if (ack_len > 0) {
            try {
                auto ack_pkt = SpacePacketCodec::decode(
                    std::span(ack_buffer, static_cast<std::size_t>(ack_len)),
                    DecodeOptions{.expect_crc16_trailer = true}
                );

                if (ack_pkt.primary_header.packet_type == PacketType::Telemetry &&
                    ack_pkt.primary_header.apid == ACK_APID &&
                    ack_pkt.packet_data_field.size() >= 3) {

                    const std::uint16_t acked_seq = read_be16(ack_pkt.packet_data_field, 0);
                    const std::uint8_t status = ack_pkt.packet_data_field[2];

                    std::lock_guard<std::mutex> lock(inflight_mutex);
                    auto it = inflight.find(acked_seq);
                    if (it != inflight.end()) {
                        it->second.acked = true;
                        std::cout << "[sender] ACK received for TC seq=" << acked_seq
                                  << " status=" << static_cast<int>(status)
                                  << " retries=" << it->second.retry_count << "\n";
                    } else {
                        std::cout << "[sender] ACK for unknown TC seq=" << acked_seq << "\n";
                    }
                }
            } catch (const std::exception& e) {
                std::cout << "[sender] ACK packet error: " << e.what() << "\n";
            }
        }

        // 3) Retransmit expired in-flight commands
        {
            std::lock_guard<std::mutex> lock(inflight_mutex);
            const auto now = std::chrono::steady_clock::now();

            for (auto it = inflight.begin(); it != inflight.end();) {
                if (it->second.acked) {
                    it = inflight.erase(it);
                    continue;
                }

                if (now >= it->second.next_deadline) {
                    if (it->second.retry_count >= 5) {
                        std::cout << "[sender] TC seq=" << it->first << " failed after max retries\n";
                        it = inflight.erase(it);
                        continue;
                    }

                    send_packet_bytes(
                        tx_sock,
                        tx_addr,
                        channel,
                        it->second.encoded_packet,
                        it->first,
                        "TC-RETX"
                    );

                    it->second.retry_count += 1;
                    const int backoff_ms = 500 * (1 << it->second.retry_count);
                    it->second.next_deadline = now + std::chrono::milliseconds(backoff_ms);

                    std::cout << "[sender] retransmit scheduled TC seq=" << it->first
                              << " retry=" << it->second.retry_count
                              << " next_backoff_ms=" << backoff_ms << "\n";
                }

                ++it;
            }
        }

        // 4) Multi-channel telemetry generation with prioritization

struct Channel {
    std::uint16_t apid;
    int priority;
};

// Higher number = more packets sent per loop
std::vector<Channel> channels = {
    {1, 3},  // GNC (high priority)
    {2, 3},  // Propulsion (high priority)
    {3, 2},  // Power (medium)
    {4, 1}   // Thermal (low)
};

for (const auto& ch : channels) {
    for (int i = 0; i < ch.priority; i++) {

        SpacePacket pkt;
        pkt.primary_header.version = 0;
        pkt.primary_header.packet_type = PacketType::Telemetry;
        pkt.primary_header.secondary_header_flag = false;
        pkt.primary_header.apid = ch.apid;
        pkt.primary_header.sequence_flags = SequenceFlags::Unsegmented;
        pkt.primary_header.sequence_count = telemetry_seq++;

        pkt.packet_data_field = {
            static_cast<std::uint8_t>(ch.apid),
            static_cast<std::uint8_t>(telemetry_seq & 0xFF)
        };

        pkt.primary_header.packet_data_length =
            static_cast<std::uint16_t>(pkt.packet_data_field.size() - 1U);

        auto encoded = SpacePacketCodec::encode(
            pkt,
            EncodeOptions{.append_crc16_trailer = true}
        );

        send_packet_bytes(
            tx_sock,
            tx_addr,
            channel,
            encoded.bytes,
            pkt.primary_header.sequence_count,
            "TM"
        );
    }
}

// Slightly faster loop to simulate higher throughput
std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    if (control_thread.joinable()) {
        control_thread.join();
    }

    close(ack_sock);
    close(control_sock);
    close(tx_sock);
    return 0;
}