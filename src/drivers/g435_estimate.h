#pragma once
// Battery estimate for the Logitech G435 on its Jieli LIGHTSPEED receiver (4C4A:4155), which has no
// readable battery level (PR #632). The headset's real level is only known over Bluetooth (Windows keeps
// the Hands-Free battery level of the paired G435). That reading is the reference; while the headset is
// in use over the receiver, the estimate drops by a discharge rate learned from successive Bluetooth
// readings. Plain C++ so it can be tested without Windows.
#include <algorithm>
#include <cstdint>

namespace g435 {

constexpr std::uint64_t kMaxStepSec = 300;         // a longer gap between polls is not counted as use
constexpr std::uint64_t kMinLearnSec = 30 * 60;    // use needed between two readings to learn a rate
constexpr int kMinRateMilli = 1000, kMaxRateMilli = 50000;  // 1 .. 50 % per hour, in thousandths

struct State {
    int lastReal = -1;              // last Bluetooth reading, 0..100 (-1: never calibrated)
    std::uint64_t usageSec = 0;     // use over the receiver since that reading
    std::uint64_t lastTick = 0;     // seconds; when usage was last accumulated
    int rateMilli = 0;              // learned drain, thousandths of a percent per hour (0: not yet)
};

// A Bluetooth reading. Returns true when the state changed.
inline bool RecordReading(State& st, int pct) {
    if (pct < 0 || pct > 100 || pct == st.lastReal) return false;
    if (st.lastReal >= 0 && pct < st.lastReal && st.usageSec >= kMinLearnSec) {
        int rate = (int)((st.lastReal - pct) * 1000.0 * 3600.0 / (double)st.usageSec);
        rate = std::clamp(rate, kMinRateMilli, kMaxRateMilli);
        st.rateMilli = st.rateMilli ? (st.rateMilli * 7 + rate * 3) / 10 : rate;  // smooth over readings
    }
    st.lastReal = pct;
    st.usageSec = 0;
    return true;
}

// Called on every poll; counts the time since the last call as use only when `inUse`.
inline bool Accumulate(State& st, bool inUse, std::uint64_t nowSec) {
    bool changed = false;
    if (inUse && st.lastTick && nowSec > st.lastTick) {
        st.usageSec += std::min<std::uint64_t>(nowSec - st.lastTick, kMaxStepSec);
        changed = true;
    }
    if (st.lastTick != nowSec) {
        st.lastTick = nowSec;
        changed = true;
    }
    return changed;
}

// The estimate to show: -1 before the first Bluetooth reading, else 1..100.
inline int Estimate(const State& st) {
    if (st.lastReal < 0) return -1;
    if (!st.rateMilli) return st.lastReal;  // one reading: shown as the reference
    int drop = (int)((double)st.usageSec / 3600.0 * st.rateMilli / 1000.0);
    return std::clamp(st.lastReal - drop, 1, 100);
}

}  // namespace g435
