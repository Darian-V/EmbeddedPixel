#pragma once

#include <stddef.h>
#include <stdint.h>
#include <cstring>

#include "ITelemetryChannel.h"
#include "proto/ProtocolTypes.h"

namespace net {

// FourCC tag for Motor Regulation stream: 'MREG'
constexpr uint32_t STREAM_TAG_MREG = proto::MAKE_FOURCC('M', 'R', 'E', 'G');

#pragma pack(push, 1)
/**
 * @brief High-rate binary telemetry sample for motor regulation diagnostics.
 */
struct MotorTelemetrySample {
    int32_t  current_position;    ///< Measured/encoder position (pulses)
    int32_t  current_velocity;    ///< Measured/commanded velocity (pulses/sec)
    int32_t  target_position;     ///< Target setpoint (pulses)
    uint16_t status_flags;        ///< Packed motor status flags
    int16_t  temperature_c_x10;   ///< Motor temperature (0.1 deg C)
    uint16_t encoder_battery_mv;  ///< Encoder backup battery voltage (mV)
};
#pragma pack(pop)

static_assert(sizeof(MotorTelemetrySample) == 18, "MotorTelemetrySample must be exactly 18 bytes");

/**
 * @brief Telemetry channel streaming real-time motor state over UDP port 50001.
 */
class MotorTelemetryChannel : public ITelemetryChannel {
public:
    explicit MotorTelemetryChannel(uint16_t sampleRateHz = 50, uint16_t batchCount = 1)
        : sample_rate_hz_(sampleRateHz),
          batch_count_(batchCount),
          enabled_(false),
          latest_sample_{} {}

    uint32_t get_tag() const override {
        return STREAM_TAG_MREG;
    }

    const char* get_name() const override {
        return "MotorReg";
    }

    uint16_t get_sample_rate_hz() const override {
        return sample_rate_hz_;
    }

    void set_sample_rate_hz(uint16_t rate_hz) override {
        if (rate_hz > 0) {
            sample_rate_hz_ = rate_hz;
        }
    }

    uint16_t get_batch_count() const override {
        return batch_count_;
    }

    void set_batch_count(uint16_t batch) override {
        if (batch > 0 && batch <= 100) {
            batch_count_ = batch;
        }
    }

    uint16_t get_channel_count() const override {
        // sizeof(MotorTelemetrySample) / sizeof(uint16_t) = 9 elements of 16-bit words
        return static_cast<uint16_t>(sizeof(MotorTelemetrySample) / sizeof(uint16_t));
    }

    proto::SampleType get_sample_type() const override {
        return proto::SampleType::UINT16;
    }

    size_t get_bytes_per_sample() const override {
        return sizeof(MotorTelemetrySample);
    }

    bool is_enabled() const override {
        return enabled_;
    }

    void set_enabled(bool enabled) override {
        enabled_ = enabled;
    }

    /**
     * @brief Update the latest motor state snapshot from application loop.
     */
    void update_snapshot(const MotorTelemetrySample& sample) {
        latest_sample_ = sample;
    }

    size_t produce_samples(void* buffer, size_t max_samples) override {
        if (max_samples < 1 || buffer == nullptr) {
            return 0;
        }
        auto* dest = static_cast<MotorTelemetrySample*>(buffer);
        for (size_t i = 0; i < max_samples; ++i) {
            dest[i] = latest_sample_;
        }
        return max_samples;
    }

private:
    uint16_t             sample_rate_hz_;
    uint16_t             batch_count_;
    bool                 enabled_;
    MotorTelemetrySample latest_sample_;
};

} // namespace net
