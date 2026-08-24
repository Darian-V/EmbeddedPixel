#pragma once

#include <stddef.h>
#include <stdint.h>

#include "proto/ProtocolTypes.h"
#include "ITempSensor.h"

namespace net {

/**
 * @brief Abstract interface for a per-board telemetry data producer/channel.
 */
class ITelemetryChannel {
public:
    virtual ~ITelemetryChannel() = default;

    /**
     * @brief 4-character FourCC stream identifier (e.g. proto::STREAM_TAG_COUNTER).
     */
    virtual uint32_t get_tag() const = 0;

    /**
     * @brief Human-readable name for logging and diagnostics.
     */
    virtual const char* get_name() const = 0;

    /**
     * @brief Sampling frequency in Hertz (e.g. 10 Hz).
     */
    virtual uint16_t get_sample_rate_hz() const = 0;

    /**
     * @brief Set sampling frequency in Hertz.
     */
    virtual void set_sample_rate_hz(uint16_t rate_hz) = 0;

    /**
     * @brief Number of samples to batch per transmitted UDP packet.
     */
    virtual uint16_t get_batch_count() const = 0;

    /**
     * @brief Set batch count per packet.
     */
    virtual void set_batch_count(uint16_t batch) = 0;

    /**
     * @brief Number of channels / elements per sample (e.g. 1).
     */
    virtual uint16_t get_channel_count() const = 0;

    /**
     * @brief Data type of each channel sample.
     */
    virtual proto::SampleType get_sample_type() const = 0;

    /**
     * @brief Number of bytes per individual sample (channel_count * sizeof(sample_type)).
     */
    virtual size_t get_bytes_per_sample() const = 0;

    /**
     * @brief Check if this channel stream is actively enabled.
     */
    virtual bool is_enabled() const = 0;

    /**
     * @brief Enable or disable transmission of this channel stream.
     */
    virtual void set_enabled(bool enabled) = 0;

    /**
     * @brief Populate destination buffer with up to max_samples.
     * @return Number of samples actually generated/copied into buffer.
     */
    virtual size_t produce_samples(void* buffer, size_t max_samples) = 0;
};

/**
 * @brief Standard 10Hz Monotonic Counter telemetry channel ('CNTR').
 */
class CounterChannel : public ITelemetryChannel {
public:
    explicit CounterChannel(uint16_t sampleRateHz = 10, uint16_t batchCount = 1)
        : sample_rate_hz_(sampleRateHz),
          batch_count_(batchCount),
          counter_(0),
          enabled_(false) {}

    uint32_t get_tag() const override {
        return proto::STREAM_TAG_COUNTER;
    }

    const char* get_name() const override {
        return "Counter";
    }

    uint16_t get_sample_rate_hz() const override {
        return sample_rate_hz_;
    }

    void set_sample_rate_hz(uint16_t rate_hz) override {
        if (rate_hz > 0) {
            sample_rate_hz_ = rate_hz;
            batch_count_ = 1; // 1 packet per tick (e.g. 10Hz = 10 packets/sec)
        }
    }

    uint16_t get_batch_count() const override {
        return batch_count_;
    }

    void set_batch_count(uint16_t batch) override {
        batch_count_ = (batch > 0 && batch <= 100) ? batch : 1;
    }

    uint16_t get_channel_count() const override {
        return 1;
    }

    proto::SampleType get_sample_type() const override {
        return proto::SampleType::UINT32;
    }

    size_t get_bytes_per_sample() const override {
        return sizeof(uint32_t);
    }

    bool is_enabled() const override {
        return enabled_;
    }

    void set_enabled(bool enabled) override {
        enabled_ = enabled;
    }

    size_t produce_samples(void* buffer, size_t max_samples) override {
        if (max_samples < 1 || buffer == nullptr) {
            return 0;
        }
        auto* data = static_cast<uint32_t*>(buffer);
        for (size_t i = 0; i < max_samples; ++i) {
            data[i] = counter_++;
        }
        return max_samples;
    }

    void reset() {
        counter_ = 0;
    }

private:
    uint16_t sample_rate_hz_;
    uint16_t batch_count_;
    uint32_t counter_;
    bool     enabled_;
};

/**
 * @brief On-chip DTS temperature telemetry channel ('TEMP').
 */
class TemperatureChannel : public ITelemetryChannel {
public:
    explicit TemperatureChannel(hal::ITempSensor& sensor, uint16_t sampleRateHz = 1, uint16_t batchCount = 1)
        : sensor_(sensor),
          sample_rate_hz_(sampleRateHz),
          batch_count_(batchCount),
          enabled_(false) {
        sensor_.init();
    }

    uint32_t get_tag() const override {
        return proto::STREAM_TAG_TEMP;
    }

    const char* get_name() const override {
        return "Temperature";
    }

    uint16_t get_sample_rate_hz() const override {
        return sample_rate_hz_;
    }

    void set_sample_rate_hz(uint16_t rate_hz) override {
        if (rate_hz > 0) {
            sample_rate_hz_ = rate_hz;
            batch_count_ = 1;
        }
    }

    uint16_t get_batch_count() const override {
        return batch_count_;
    }

    void set_batch_count(uint16_t batch) override {
        batch_count_ = (batch > 0 && batch <= 100) ? batch : 1;
    }

    uint16_t get_channel_count() const override {
        return 1;
    }

    proto::SampleType get_sample_type() const override {
        return proto::SampleType::INT32;
    }

    size_t get_bytes_per_sample() const override {
        return sizeof(int32_t);
    }

    bool is_enabled() const override {
        return enabled_;
    }

    void set_enabled(bool enabled) override {
        enabled_ = enabled;
    }

    size_t produce_samples(void* buffer, size_t max_samples) override {
        if (max_samples < 1 || buffer == nullptr) {
            return 0;
        }
        auto* data = static_cast<int32_t*>(buffer);
        int32_t temp_c = 0;
        if (!sensor_.get_temperature(temp_c)) {
            temp_c = -999;
        }
        for (size_t i = 0; i < max_samples; ++i) {
            data[i] = temp_c;
        }
        return max_samples;
    }

private:
    hal::ITempSensor& sensor_;
    uint16_t          sample_rate_hz_;
    uint16_t          batch_count_;
    bool              enabled_;
};

/**
 * @brief High-Speed Multi-Channel Stress Test telemetry channel ('STR6', 'STR1', 'RAW0', etc.)
 * Conforms to EMBEDDED_STRESS_TEST_GUIDE.md
 */
template <size_t NumChannels = 64>
class StressTestChannel : public ITelemetryChannel {
public:
    explicit StressTestChannel(uint32_t streamTag = proto::STREAM_TAG_STR6,
                               uint16_t sampleRateHz = 5000,
                               uint16_t batchCount = 11)
        : tag_(streamTag),
          sample_rate_hz_(sampleRateHz),
          batch_count_(batchCount),
          counter_(0),
          enabled_(false) {}

    uint32_t get_tag() const override {
        return tag_;
    }

    const char* get_name() const override {
        return "StressStream";
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
        if (batch > 0) {
            batch_count_ = batch;
        }
    }

    uint16_t get_channel_count() const override {
        return static_cast<uint16_t>(NumChannels);
    }

    proto::SampleType get_sample_type() const override {
        return proto::SampleType::UINT16;
    }

    size_t get_bytes_per_sample() const override {
        return NumChannels * sizeof(uint16_t);
    }

    bool is_enabled() const override {
        return enabled_;
    }

    void set_enabled(bool enabled) override {
        enabled_ = enabled;
    }

    size_t produce_samples(void* buffer, size_t max_samples) override {
        if (max_samples < 1 || buffer == nullptr) {
            return 0;
        }
        auto* data = static_cast<uint16_t*>(buffer);
        for (size_t s = 0; s < max_samples; ++s) {
            counter_++;
            for (size_t ch = 0; ch < NumChannels; ++ch) {
                data[s * NumChannels + ch] = static_cast<uint16_t>(counter_ + (ch * 100));
            }
        }
        return max_samples;
    }

    void reset() {
        counter_ = 0;
    }

private:
    uint32_t tag_;
    uint16_t sample_rate_hz_;
    uint16_t batch_count_;
    uint16_t counter_;
    bool     enabled_;
};

} // namespace net

