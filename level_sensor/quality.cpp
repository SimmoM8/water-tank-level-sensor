#include "quality.h"
#include <limits.h>

static constexpr uint32_t kWindowInactive = UINT32_MAX;

void quality_init(QualityRuntime &rt)
{
    rt.hasLast = false;
    rt.lastRaw = 0;
    rt.spikeCount = 0;
    rt.spikeWindowStart = kWindowInactive;
    rt.stuckStartMs = kWindowInactive;
    rt.calBelowCount = 0;
    rt.calAboveCount = 0;
    rt.calWindowStart = kWindowInactive;
    rt.zeroCount = 0;
    rt.zeroWindowStart = kWindowInactive;
}

static bool windowActive(uint32_t start)
{
    return start != kWindowInactive;
}

static bool windowExpired(uint32_t now, uint32_t start, uint32_t windowMs)
{
    return windowActive(start) && (uint32_t)(now - start) > windowMs;
}

static void startWindowIfExpired(uint32_t now, uint32_t windowMs, uint32_t &start, uint8_t &count)
{
    if (!windowActive(start) || windowExpired(now, start, windowMs))
    {
        start = now;
        count = 0;
    }
}

static void startWindowIfExpired(uint32_t now, uint32_t windowMs, uint32_t &start, uint8_t &countA, uint8_t &countB)
{
    if (!windowActive(start) || windowExpired(now, start, windowMs))
    {
        start = now;
        countA = 0;
        countB = 0;
    }
}

static void expireWindow(uint32_t now, uint32_t windowMs, uint32_t &start, uint8_t &count)
{
    if (windowExpired(now, start, windowMs))
    {
        start = kWindowInactive;
        count = 0;
    }
}

static void expireWindow(uint32_t now, uint32_t windowMs, uint32_t &start, uint8_t &countA, uint8_t &countB)
{
    if (windowExpired(now, start, windowMs))
    {
        start = kWindowInactive;
        countA = 0;
        countB = 0;
    }
}

QualityResult quality_evaluate(uint32_t raw,
                               const AppliedConfig &cfg,
                               const QualityConfig &qc,
                               QualityRuntime &rt,
                               uint32_t nowMs)
{
    // Policy:
    // - On disconnect: reset all windows/counters and treat next sample as first.
    // - Invalid samples (out_of_bounds) do not update history.
    // - Windows are active even when start == 0; kWindowInactive indicates "not started".
    QualityResult res{true, ProbeQualityReason::OK};
    bool updateLast = true;

    // Disconnected if raw below threshold
    if (raw < qc.disconnectedBelowRaw)
    {
        res.connected = false;
        res.reason = ProbeQualityReason::DISCONNECTED_LOW_RAW;
        // Reset counters so we don't carry over stale state across reconnects.
        quality_init(rt);
        updateLast = false;
        goto finalize;
    }

    // Out of bounds
    if (raw < qc.rawMin || raw > qc.rawMax)
    {
        res.reason = ProbeQualityReason::OUT_OF_BOUNDS;
        updateLast = false;
        goto finalize;
    }

    // Expire windows even when no events are observed.
    expireWindow(nowMs, qc.zeroWindowMs, rt.zeroWindowStart, rt.zeroCount);
    expireWindow(nowMs, qc.spikeWindowMs, rt.spikeWindowStart, rt.spikeCount);
    expireWindow(nowMs, qc.calRecommendWindowMs, rt.calWindowStart, rt.calBelowCount, rt.calAboveCount);

    // Zero hits tracking
    if (raw == 0)
    {
        startWindowIfExpired(nowMs, qc.zeroWindowMs, rt.zeroWindowStart, rt.zeroCount);
        rt.zeroCount++;
        if (rt.zeroCount >= qc.zeroHitCount)
        {
            res.reason = ProbeQualityReason::ZERO_HITS;
            goto finalize;
        }
    }

    // Spike tracking
    if (rt.hasLast)
    {
        uint32_t delta = (raw > rt.lastRaw) ? (raw - rt.lastRaw) : (rt.lastRaw - raw);
        // Spike: windowed count of large deltas.
        if (delta >= qc.spikeDelta)
        {
            startWindowIfExpired(nowMs, qc.spikeWindowMs, rt.spikeWindowStart, rt.spikeCount);
            rt.spikeCount++;
            if (rt.spikeCount >= qc.spikeCountThreshold)
            {
                res.reason = ProbeQualityReason::UNRELIABLE_SPIKES;
                goto finalize;
            }
        }
        else if (delta >= qc.rapidFluctuationDelta)
        {
            // Rapid fluctuation: any single delta above threshold flags unreliable.
            res.reason = ProbeQualityReason::UNRELIABLE_RAPID;
            goto finalize;
        }
        else
        {
            // stuck detection
            if (delta <= qc.stuckDelta)
            {
                if (!windowActive(rt.stuckStartMs))
                {
                    rt.stuckStartMs = nowMs;
                }
                else if ((uint32_t)(nowMs - rt.stuckStartMs) >= qc.stuckMs)
                {
                    res.reason = ProbeQualityReason::UNRELIABLE_STUCK;
                    goto finalize;
                }
            }
            else
            {
                rt.stuckStartMs = kWindowInactive;
            }
        }
    }
    else
    {
        rt.stuckStartMs = kWindowInactive;
    }

    // Calibration recommended: raw repeatedly beyond stored cal bounds + margin.
    const int32_t dry = cfg.calDry;
    const int32_t wet = cfg.calWet;
    const int32_t calMin = (dry < wet) ? dry : wet;
    const int32_t calMax = (dry > wet) ? dry : wet;
    const int32_t calRange = calMax - calMin;
    const bool hasCal = (dry > 0) && (wet > 0) && (calRange >= (int32_t)qc.calRecommendMargin);
    if (hasCal)
    {
        startWindowIfExpired(nowMs, qc.calRecommendWindowMs, rt.calWindowStart, rt.calBelowCount, rt.calAboveCount);

        const int32_t rawSigned = (raw > (uint32_t)INT32_MAX) ? INT32_MAX : (int32_t)raw;
        if (rawSigned < (calMin - (int32_t)qc.calRecommendMargin))
        {
            rt.calBelowCount++;
        }
        else if (rawSigned > (calMax + (int32_t)qc.calRecommendMargin))
        {
            rt.calAboveCount++;
        }

        if ((rt.calBelowCount >= qc.calRecommendCount) || (rt.calAboveCount >= qc.calRecommendCount))
        {
            res.reason = ProbeQualityReason::CALIBRATION_RECOMMENDED;
            goto finalize;
        }
    }

finalize:
    if (updateLast)
    {
        rt.hasLast = true;
        rt.lastRaw = raw;
    }
    return res;
}

#if defined(QUALITY_SELF_TEST)
// Minimal self-test harness (compile-time gated).
// Scenarios:
// 1) disconnected: raw < disconnectedBelowRaw -> DISCONNECTED_LOW_RAW
// 2) spike burst: repeated large deltas within spikeWindow -> UNRELIABLE_SPIKES
// 3) stuck: delta <= stuckDelta for >= stuckMs -> UNRELIABLE_STUCK
// 4) zero hits: raw==0 count within zeroWindow -> ZERO_HITS
// 5) cal recommend: repeated below/above bounds -> CALIBRATION_RECOMMENDED
static void quality_selfTest()
{
    QualityConfig qc{};
    qc.disconnectedBelowRaw = 10;
    qc.rawMin = 0;
    qc.rawMax = 1000000;
    qc.rapidFluctuationDelta = 100;
    qc.spikeDelta = 500;
    qc.spikeCountThreshold = 3;
    qc.spikeWindowMs = 1000;
    qc.stuckDelta = 2;
    qc.stuckMs = 1000;
    qc.calRecommendMargin = 10;
    qc.calRecommendCount = 2;
    qc.calRecommendWindowMs = 1000;
    qc.zeroHitCount = 2;
    qc.zeroWindowMs = 1000;

    AppliedConfig cfg{};
    cfg.calDry = 200;
    cfg.calWet = 800;

    QualityRuntime rt{};
    quality_init(rt);

    (void)quality_evaluate(5, cfg, qc, rt, 0); // disconnected
    quality_init(rt);

    (void)quality_evaluate(100, cfg, qc, rt, 0);
    (void)quality_evaluate(700, cfg, qc, rt, 100); // spike 1
    (void)quality_evaluate(100, cfg, qc, rt, 200); // spike 2
    (void)quality_evaluate(700, cfg, qc, rt, 300); // spike 3 -> unreliable_spikes

    quality_init(rt);
    (void)quality_evaluate(500, cfg, qc, rt, 0);
    (void)quality_evaluate(501, cfg, qc, rt, 1000); // stuck -> unreliable_stuck

    quality_init(rt);
    (void)quality_evaluate(0, cfg, qc, rt, 0);
    (void)quality_evaluate(0, cfg, qc, rt, 100); // zero_hits

    quality_init(rt);
    (void)quality_evaluate(50, cfg, qc, rt, 0);
    (void)quality_evaluate(50, cfg, qc, rt, 100); // below -> cal recommend
}
#endif
