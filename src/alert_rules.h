#pragma once
// Pure, portable policy for per-device low-battery notifications.
// No HID access, Windows dependencies, timers or disk/network writes.
#include "devices.h"

#include <map>
#include <string>

namespace alerts {
constexpr int kDefaultThreshold = 15;
constexpr int kUseDefault = -2;   // no per-device override persisted
constexpr int kDisabled = -1;     // notifications disabled for this device
constexpr int kThresholds[] = {5, 10, 15, 20, 25, 30, 35, 40};
constexpr int kThresholdCount = sizeof(kThresholds) / sizeof(kThresholds[0]);

inline bool ValidOverride(int value) {
    if (value == kDisabled) return true;
    for (int threshold : kThresholds)
        if (value == threshold) return true;
    return false;
}

inline int OverrideFor(const std::map<std::wstring, int>& overrides, const std::wstring& key) {
    const auto it = overrides.find(key);
    return it == overrides.end() ? kUseDefault : it->second;
}

inline int EffectiveThreshold(const std::map<std::wstring, int>& overrides, const std::wstring& key) {
    int value = OverrideFor(overrides, key);
    return value == kUseDefault ? kDefaultThreshold : value;
}

// This is invoked only for a fresh poll result. Unknown, sleeping and disconnected
// readings don't reset the latch: a transient missing report must not spam alerts.
inline bool ShouldNotify(const DeviceStatus& s, int threshold, bool& warned) {
    if (threshold == kDisabled) { warned = false; return false; }
    if (!s.connected || s.asleep || s.percent < 0 || s.percent > 100) return false;
    if (s.charging) { warned = false; return false; }
    if (s.percent > threshold + 10) { warned = false; return false; }
    if (s.percent <= threshold && !warned) {
        warned = true;
        return true;
    }
    return false;
}

// Tiered low-battery alerts (PR #3946): the device's threshold, then 10% (when the threshold is above it)
// and 5%, each once per discharge. Same rules as ShouldNotify: only fresh readings count, charging or a
// level 10 points above the threshold re-arms all three.
enum class LowAlert { None, Low, Critical, Emergency };
struct LowLatch { bool low = false, critical = false, emergency = false; };
constexpr int kCriticalLevel = 10, kEmergencyLevel = 5;

inline LowAlert EvaluateLow(const DeviceStatus& s, int threshold, LowLatch& l, bool tiers = true) {
    if (threshold == kDisabled) { l = {}; return LowAlert::None; }
    if (!s.connected || s.asleep || s.percent < 0 || s.percent > 100) return LowAlert::None;
    if (s.charging || s.percent > threshold + 10) { l = {}; return LowAlert::None; }
    if (tiers && s.percent <= kEmergencyLevel && !l.emergency) {
        l.emergency = l.critical = l.low = true;
        return LowAlert::Emergency;
    }
    if (tiers && threshold > kCriticalLevel && s.percent <= kCriticalLevel && !l.critical) {
        l.critical = l.low = true;
        return LowAlert::Critical;
    }
    if (s.percent <= threshold && !l.low) {
        l.low = true;
        return LowAlert::Low;
    }
    return LowAlert::None;
}

// "Fully charged" (PR #3946): once per charge, only when the level is seen reaching full while charging,
// so a device already full on its charger when the app starts says nothing. Re-armed when unplugged.
inline bool ShouldNotifyCharged(const DeviceStatus& s, int previousPercent, bool& warned) {
    if (!s.connected || s.asleep || s.percent < 0 || s.percent > 100) return false;
    if (!s.charging && !s.full) { warned = false; return false; }
    bool full = s.full || s.percent >= 100;
    if (!warned && full && previousPercent >= 0 && previousPercent < 100) {
        warned = true;
        return true;
    }
    if (full) warned = true;  // already full when first seen: wait for the next charge
    return false;
}

// "Unplug to save the battery": charging past `limit` (e.g. 80%) wears a lithium battery faster, so the
// app can say when the level crosses it. Only when it is seen crossing (previous reading below the limit),
// so starting the app on a device already at 90% says nothing; once per charge, re-armed when unplugged
// or after the level drops 5 points below the limit. A jump straight to 100% is left to "fully charged".
constexpr int kChargeLimits[] = {80, 85, 90};
constexpr int kChargeLimitCount = sizeof(kChargeLimits) / sizeof(kChargeLimits[0]);

inline bool ShouldNotifyChargeLimit(const DeviceStatus& s, int limit, int previousPercent, bool& warned) {
    if (limit <= 0) { warned = false; return false; }
    if (!s.connected || s.asleep || s.percent < 0 || s.percent > 100) return false;
    if (!s.charging || s.percent < limit - 5) { warned = false; return false; }
    if (!warned && s.percent >= limit && s.percent < 100 && previousPercent >= 0 && previousPercent < limit) {
        warned = true;
        return true;
    }
    return false;
}
}  // namespace alerts
