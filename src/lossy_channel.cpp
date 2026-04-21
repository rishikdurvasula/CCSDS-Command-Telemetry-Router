#include "ccsds/lossy_channel.hpp"

namespace ccsds {

LossyChannel::LossyChannel()
    : rng_(std::random_device{}()), dist_(0.0, 1.0) {}

void LossyChannel::update_config(const ChaosConfig& config) {
    std::lock_guard<std::mutex> lock(mutex_);
    config_ = config;
}

std::vector<uint8_t> LossyChannel::process_outgoing(const std::vector<uint8_t>& packet) {
    std::lock_guard<std::mutex> lock(mutex_);

    double r = dist_(rng_);

    // 1. Drop
    if (r < config_.drop_probability) {
        return {};
    }

    std::vector<uint8_t> out = packet;

    // 2. Corrupt
    if (dist_(rng_) < config_.corrupt_probability && !out.empty()) {
        size_t idx = static_cast<size_t>(dist_(rng_) * out.size());
        out[idx] ^= 0xFF;
    }

    // 3. Reorder (very simple: swap with buffer)
    if (dist_(rng_) < config_.reorder_probability) {
        if (!reorder_buffer_.empty()) {
            std::vector<uint8_t> temp = reorder_buffer_;
            reorder_buffer_ = out;
            return temp;
        } else {
            reorder_buffer_ = out;
            return {};
        }
    }

    return out;
}

}  // namespace ccsds