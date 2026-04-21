#include "ccsds/crc16.hpp"

namespace ccsds {

std::uint16_t Crc16CcittFalse::compute(std::span<const std::uint8_t> data) noexcept {
    std::uint16_t crc = 0xFFFF;

    for (std::uint8_t byte : data) {
        crc ^= static_cast<std::uint16_t>(byte) << 8U;

        for (int i = 0; i < 8; ++i) {
            if ((crc & 0x8000U) != 0U) {
                crc = static_cast<std::uint16_t>((crc << 1U) ^ 0x1021U);
            } else {
                crc = static_cast<std::uint16_t>(crc << 1U);
            }
        }
    }

    return crc;
}

}  // namespace ccsds