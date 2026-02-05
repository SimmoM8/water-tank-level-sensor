#pragma once
#include <stdint.h>
#include "device_state.h"
#include "applied_config.h"

// Configuration for quality evaluation.
struct QualityConfig
{
    uint32_t disconnectedBelowRaw;
    uint32_t rawMin;
    uint32_t rawMax;
    uint32_t rapidFluctuationDelta;
    uint32_t spikeDelta;
    uint8_t spikeCountThreshold;
    uint32_t spikeWindowMs;
    uint32_t stuckDelta;
    uint32_t stuckMs;
    uint32_t calRecommendMargin;
    uint8_t calRecommendCount;
    uint32_t calRecommendWindowMs;
    uint8_t zeroHitCount;
    uint32_t zeroWindowMs;
};

struct QualityRuntime
{
    bool hasLast;
    uint32_t lastRaw;
    uint8_t spikeCount;
    uint32_t spikeWindowStart; // UINT32_MAX means inactive
    uint32_t stuckStartMs;     // UINT32_MAX means inactive
    uint8_t calBelowCount;
    uint8_t calAboveCount;
    uint32_t calWindowStart;   // UINT32_MAX means inactive
    uint8_t zeroCount;
    uint32_t zeroWindowStart;  // UINT32_MAX means inactive
};

struct QualityResult
{
    bool connected;
    ProbeQualityReason reason;
};

// Initialize runtime counters and window state.
void quality_init(QualityRuntime &rt);

// Evaluate quality given a raw sample, calibration, and config thresholds.
QualityResult quality_evaluate(uint32_t raw,
                               const AppliedConfig &cfg,
                               const QualityConfig &qc,
                               QualityRuntime &rt,
                               uint32_t nowMs);
