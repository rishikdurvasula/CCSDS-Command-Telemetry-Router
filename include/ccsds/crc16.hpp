#pragma once

#include <cstdint>
#include <span>

namespace ccsds {

// CRC-16/CCITT-FALSE
// poly=0x1021, init=0xFFFF, refin=false, refout=false, xorout=0x0000
class Crc16CcittFalse {
public:
    static std::uint16_t compute(std::span<const std::uint8_t> data) noexcept;
};

}  // namespace ccsds