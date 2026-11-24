#include "../src/runtime_estimate.h"
#include <cassert>
#include <cstdio>
#include <cmath>
using namespace battery_history;

int main() {
    constexpr int64_t now=1795000000;
    const std::wstring key=L"demo:mouse";
    auto sample=[&](int hoursAgo,int percent,bool charging=false){
        return Sample{key,now-static_cast<int64_t>(hoursAgo)*3600,percent,charging};
    };
    {
        const auto e=EstimateRuntime({sample(8,80),sample(6,77),sample(4,74),sample(2,71),sample(0,68)},now);
        assert(e.state==RuntimeState::Ready);
        assert(e.sampleCount==5 && std::abs(e.percentPerHour-1.5)<0.01);
        assert(std::abs(e.hoursRemaining-68.0/1.5)<0.01);
    }
    assert(EstimateRuntime({sample(2,65),sample(1,64)},now).state==RuntimeState::Insufficient);
    assert(EstimateRuntime({sample(8,68),sample(6,68),sample(4,67),sample(0,67)},now).state==RuntimeState::Flat);
    assert(EstimateRuntime({sample(8,70),sample(6,68),sample(4,65),sample(0,64,true)},now).state==RuntimeState::Charging);
    assert(EstimateRuntime({sample(16,80),sample(14,77),sample(12,74),sample(10,71)},now).state==RuntimeState::Stale);
    // A charging phase must not pollute the current discharge estimate.
    {
        auto e=EstimateRuntime({sample(18,26),sample(16,80,true),sample(12,90,true),
                                sample(9,91),sample(6,87),sample(3,84),sample(0,80)},now);
        assert(e.state==RuntimeState::Ready);
        assert(e.sampleCount==4);
    }
    // A long offline gap invalidates an extrapolation from an earlier session.
    assert(EstimateRuntime({sample(30,90),sample(27,87),sample(24,84),sample(0,80)},now).state==RuntimeState::Insufficient);
    // A sudden unexplained jump resets the discharge cycle.
    assert(EstimateRuntime({sample(15,48),sample(12,89),sample(9,87),sample(6,85),sample(3,83),sample(0,81)},now).sampleCount==5);
    // Same reported battery level should not make a green 'charging' line.
    assert(EstimateRuntime({sample(7,50),sample(6,50),sample(5,50),sample(0,50)},now).state==RuntimeState::Insufficient);
    // The optional taskbar readout must remain explicitly approximate and fail closed.
    const auto ready = EstimateRuntime({sample(8,80),sample(6,77),sample(4,74),sample(2,71),sample(0,68)},now);
    assert(CompactRuntimeText(ready) == L"~45h");
    RuntimeEstimate forecast;
    forecast.state = RuntimeState::Ready;
    forecast.hoursRemaining = 0.4;
    assert(CompactRuntimeText(forecast) == L"<1h");
    forecast.hoursRemaining = 0.0;
    assert(CompactRuntimeText(forecast) == L"~0h");
    forecast.hoursRemaining = -2.0;
    assert(CompactRuntimeText(forecast).empty());
    forecast.hoursRemaining = 18;
    for (const auto state : {RuntimeState::Charging, RuntimeState::Stale, RuntimeState::Flat,
                             RuntimeState::Insufficient}) {
        forecast.state = state;
        assert(CompactRuntimeText(forecast).empty());
    }
    forecast.state = RuntimeState::Ready;
    forecast.hoursRemaining = 139;
    assert(CompactRuntimeText(forecast) == L"~6d");
    puts("Battery runtime estimation tests passed");
}
