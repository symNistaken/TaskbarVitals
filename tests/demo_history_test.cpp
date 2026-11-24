#include "../src/demo_history.h"
#include "../src/runtime_estimate.h"

#include <cassert>
#include <cstdio>
#include <string>
#include <vector>

using namespace battery_history;

int main() {
    constexpr int64_t now = 1795000000;

    struct Expected {
        const wchar_t* key;
        int percent;
        RuntimeState state;
        const wchar_t* badge;
    };
    const Expected cases[] = {
        {L"razer:demo",        78, RuntimeState::Ready,        L"~6d"},
        {L"demo:logi",         64, RuntimeState::Ready,        L"~8d"},
        {L"steelseries:demo",  55, RuntimeState::Ready,        L"~18h"},
        {L"hsc:1b1c:0a6b",     81, RuntimeState::Ready,        L"~3d"},
        {L"rog:demo",          90, RuntimeState::Ready,        L"~15d"},
        {L"zowie:demo",        66, RuntimeState::Ready,        L"~3d"},
        {L"bt:demo-ps",        38, RuntimeState::Ready,        L"~10h"},
        {L"xbox:demo",         72, RuntimeState::Ready,        L"~9h"},
        {L"coolermaster:demo", 12, RuntimeState::Ready,        L"~8h"},
        {L"hyperx:demo",       47, RuntimeState::Ready,        L"~47h"},
        {L"bt:demo-buds",      59, RuntimeState::Insufficient, L""},
        {L"airpods:demo",      85, RuntimeState::Flat,         L""},
        {L"jabra:demo",        23, RuntimeState::Stale,        L""},
        {L"jbl:demo",          70, RuntimeState::Ready,        L"~5d"},
        {L"steam:demo",        50, RuntimeState::Charging,     L""},
        {L"bt:demo-plain",    100, RuntimeState::Charging,     L""},
    };

    for (const auto& expected : cases) {
        auto samples = DemoSamples(expected.key, now);
        assert(!samples.empty());
        assert(samples.back().percent == expected.percent);
        assert(samples.front().timestamp >= now - kRetentionSeconds);
        for (const auto& s : samples) {
            assert(s.key == expected.key);
            assert(Valid(s));
        }
        const auto forecast = EstimateRuntime(samples, now);
        assert(forecast.state == expected.state);
        assert(CompactRuntimeText(forecast) == expected.badge);
    }

    assert(DemoSamples(L"unknown:device", now).empty());
    assert(DemoSamples(L"demo:offline-receiver", now).empty());
    assert(EstimateRuntime(DemoSamples(L"razer:demo", now), now).hoursRemaining !=
           EstimateRuntime(DemoSamples(L"xbox:demo", now), now).hoursRemaining);

    puts("Per-device demo battery history tests passed");
}
