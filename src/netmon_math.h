#pragma once
// Pure, Windows-independent interface filtering, counter delta/wrap handling, and speed formatting
// for live network throughput monitoring.
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cwchar>
#include <string>

namespace netmon {

constexpr std::uint32_t kIfTypeSoftwareLoopback = 24;
constexpr std::uint32_t kIfTypeTunnel = 131;
constexpr int kIfOperStatusUp = 1;

struct RawTotals {
    bool valid = false;
    std::uint64_t inOctets = 0;
    std::uint64_t outOctets = 0;
};

struct RateResult {
    bool valid = false;
    double rxBytesPerSec = 0.0;
    double txBytesPerSec = 0.0;
};

// Keep only active interfaces carrying user traffic: exclude invalid type 0, software loopback (24),
// tunnel interfaces (131), interfaces that are not up (operStatus != 1), and NDIS filter overlays
// (filterInterface == true) which would otherwise double-count packets on top of the underlying NIC.
inline bool IncludeInterface(std::uint32_t ifType, int operStatus, bool filterInterface) {
    if (ifType == 0 || ifType == kIfTypeSoftwareLoopback || ifType == kIfTypeTunnel) return false;
    if (operStatus != kIfOperStatusUp || filterInterface) return false;
    return true;
}

// Computes the byte delta between two monotonic counter samples.
// - Normal advance (cur >= prev): returns cur - prev.
// - 32-bit counter wraparound (cur < prev && prev <= 0xFFFFFFFFULL): wraps at 2^32.
// - >32-bit counter drop (cur < prev && prev > 0xFFFFFFFFULL, e.g. adapter disconnect/reset):
//   returns 0 and sets *didReset = true so the caller can re-baseline without a fake spike.
inline std::uint64_t CounterDelta(std::uint64_t prev, std::uint64_t cur, bool* didReset = nullptr) {
    if (cur >= prev) {
        if (didReset) *didReset = false;
        return cur - prev;
    }
    if (prev <= 0xFFFFFFFFULL) {
        if (didReset) *didReset = false;
        return (0x100000000ULL - prev) + cur;
    }
    if (didReset) *didReset = true;
    return 0;
}

// Computes RX/TX byte rates over `elapsedMs` milliseconds.
// Returns valid = false if either sample is invalid, elapsedMs == 0, or a >32-bit counter reset
// occurred (re-baselining prevents both fake petabyte spikes and false 32-bit wraps on the sibling
// direction when an interface disappears).
inline RateResult ComputeRates(const RawTotals& prev, const RawTotals& cur, std::uint64_t elapsedMs) {
    if (!prev.valid || !cur.valid || elapsedMs == 0) return {};
    bool rxReset = false, txReset = false;
    const std::uint64_t rxDelta = CounterDelta(prev.inOctets, cur.inOctets, &rxReset);
    const std::uint64_t txDelta = CounterDelta(prev.outOctets, cur.outOctets, &txReset);
    if (rxReset || txReset) return {};
    const double seconds = static_cast<double>(elapsedMs) / 1000.0;
    return { true, static_cast<double>(rxDelta) / seconds, static_cast<double>(txDelta) / seconds };
}

namespace detail {

inline std::wstring FormatRateValue(double bytesPerSec, const wchar_t* const (&units)[4]) {
    if (!std::isfinite(bytesPerSec) || bytesPerSec <= 0.0) bytesPerSec = 0.0;
    wchar_t buf[32]{};
    if (bytesPerSec < 1023.5) {
        std::swprintf(buf, 32, L"%lld", static_cast<long long>(std::llround(bytesPerSec)));
        return std::wstring(buf) + units[0];
    }
    double val = bytesPerSec / 1024.0;
    int tier = 1;
    while (tier < 3 && val >= 1023.5) {
        val /= 1024.0;
        ++tier;
    }
    if (val < 9.95) {
        std::swprintf(buf, 32, L"%.1f", val);
    } else {
        std::swprintf(buf, 32, L"%lld", static_cast<long long>(std::llround(val)));
    }
    return std::wstring(buf) + units[tier];
}

}  // namespace detail

inline std::wstring FormatCompactRate(double bytesPerSec) {
    static constexpr const wchar_t* kUnits[4] = { L"B", L"K", L"M", L"G" };
    return detail::FormatRateValue(bytesPerSec, kUnits);
}

inline std::wstring FormatTooltipRate(double bytesPerSec) {
    static constexpr const wchar_t* kUnits[4] = { L" B/s", L" KB/s", L" MB/s", L" GB/s" };
    return detail::FormatRateValue(bytesPerSec, kUnits);
}

inline std::wstring FormatStripSpeed(double rxBps, double txBps) {
    return L"\u2193" + FormatCompactRate(rxBps) + L" \u2191" + FormatCompactRate(txBps);
}

inline std::wstring FormatTooltipSpeed(double rxBps, double txBps) {
    return L"\u2193 " + FormatTooltipRate(rxBps) + L", \u2191 " + FormatTooltipRate(txBps);
}

}  // namespace netmon
