#pragma once

#include <cstdint>
#include <random>
#include <vector>
#include <mutex>

namespace ccsds {

struct ChaosConfig {
    double drop_probability = 0.0;      // 0.0 → 1.0
    double corrupt_probability = 0.0;   // 0.0 → 1.0
    double reorder_probability = 0.0;   // 0.0 → 1.0
};

class LossyChannel {
public:
    LossyChannel();

    void update_config(const ChaosConfig& config);

    // Returns empty vector if packet is dropped
    std::vector<uint8_t> process_outgoing(const std::vector<uint8_t>& packet);

private:
    ChaosConfig config_;
    std::mt19937 rng_;
    std::uniform_real_distribution<double> dist_;

    std::mutex mutex_;

    std::vector<uint8_t> reorder_buffer_;
};

}  // namespace ccsds