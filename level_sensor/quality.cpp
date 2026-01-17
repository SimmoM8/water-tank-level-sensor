#include "quality.h"
#include <Arduino.h>

void quality_init(QualityRuntime &rt)
{
    rt.hasLast = false;
    rt.lastRaw = 0;
    rt.spikeCount = 0;
    rt.spikeWindowStart = 0;
    rt.stuckStartMs = 0;
    rt.calBelowCount = 0;
    rt.calAboveCount = 0;
    rt.calWindowStart = 0;
    rt.zeroCount = 0;
    rt.zeroWindowStart = 0;
}

static bool inWindow(uint32_t now, uint32_t start, uint32_t windowMs)
{
    return start != 0 && (now - start) <= windowMs;
}

QualityResult quality_evaluate(uint32_t raw,
                               const AppliedConfig &cfg,
                               const QualityConfig &qc,
                               QualityRuntime &rt,
                               uint32_t nowMs)
{
    QualityResult res{true, ProbeQualityReason::OK};

    // Disconnected if raw below threshold
    if (raw < qc.disconnectedBelowRaw)
    {
        res.connected = false;
        res.reason = ProbeQualityReason::DISCONNECTED_LOW_RAW;
        // reset counters so we don't carry over stale state
        quality_init(rt);
        return res;
    }

    // Out of bounds
    if (raw < qc.rawMin || raw > qc.rawMax)
    {
        res.reason = ProbeQualityReason::OUT_OF_BOUNDS;
        return res;
    }

    // Zero hits tracking
    if (raw == 0)
    {
        if (!inWindow(nowMs, rt.zeroWindowStart, qc.zeroWindowMs))
        {
            rt.zeroWindowStart = nowMs;
            rt.zeroCount = 0;
        }
        rt.zeroCount++;
        if (rt.zeroCount >= qc.zeroHitCount)
        {
            res.reason = ProbeQualityReason::ZERO_HITS;
            return res;
        }
    }

    // Spike tracking
    if (!rt.spikeWindowStart)
    {
        rt.spikeWindowStart = nowMs;
    }

    if (rt.hasLast)
    {
        uint32_t delta = (raw > rt.lastRaw) ? (raw - rt.lastRaw) : (rt.lastRaw - raw);
        if (delta >= qc.spikeDelta)
        {
            if (!inWindow(nowMs, rt.spikeWindowStart, qc.spikeWindowMs))
            {
                rt.spikeWindowStart = nowMs;
                rt.spikeCount = 0;
            }
            rt.spikeCount++;
            if (rt.spikeCount >= qc.spikeCountThreshold)
            {
                res.reason = ProbeQualityReason::UNRELIABLE_SPIKES;
                return res;
            }
        }
        else if (delta >= qc.rapidFluctuationDelta)
        {
            res.reason = ProbeQualityReason::UNRELIABLE_RAPID;
            return res;
        }
        else
        {
            // stuck detection
            if (!rt.stuckStartMs)
            {
                rt.stuckStartMs = nowMs;
            }
            else if ((delta <= qc.stuckDelta) && ((nowMs - rt.stuckStartMs) >= qc.stuckMs))
            {
                res.reason = ProbeQualityReason::UNRELIABLE_STUCK;
                return res;
            }
            else if (delta > qc.stuckDelta)
            {
                rt.stuckStartMs = nowMs;
            }
        }
    }
    else
    {
        rt.stuckStartMs = nowMs;
    }

    // Calibration recommended: raw repeatedly beyond stored cal bounds
    const bool hasDryWet = cfg.calDry > 0 && cfg.calWet > 0 && (uint32_t)abs(cfg.calWet - cfg.calDry) >= qc.calRecommendMargin;
    if (hasDryWet)
    {
        const int32_t dry = cfg.calDry;
        const int32_t wet = cfg.calWet;
        if (!inWindow(nowMs, rt.calWindowStart, qc.calRecommendWindowMs))
        {
            rt.calWindowStart = nowMs;
            rt.calBelowCount = 0;
            rt.calAboveCount = 0;
        }

        if ((int32_t)raw + (int32_t)qc.calRecommendMargin < dry)
        {
            rt.calBelowCount++;
        }
        else if ((int32_t)raw > (int32_t)wet + (int32_t)qc.calRecommendMargin)
        {
            rt.calAboveCount++;
        }

        if ((rt.calBelowCount >= qc.calRecommendCount) || (rt.calAboveCount >= qc.calRecommendCount))
        {
            res.reason = ProbeQualityReason::CALIBRATION_RECOMMENDED;
            return res;
        }
    }

    rt.hasLast = true;
    rt.lastRaw = raw;
    return res;
}
