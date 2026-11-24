#include "../src/drivers/nuphy_protocol.h"

#include <cassert>

namespace {

nuphy::BatterySample ConsumeSample(nuphy::BatteryFrame& frame, int usage, std::uint64_t nowMs,
                                   std::uint8_t reportId = 3) {
    const std::uint8_t bytes[] = {reportId, std::uint8_t(usage), std::uint8_t(usage >> 8)};
    return frame.Consume(bytes, sizeof(bytes), nowMs);
}

int Consume(nuphy::BatteryFrame& frame, int usage, std::uint64_t nowMs, std::uint8_t reportId = 3) {
    return ConsumeSample(frame, usage, nowMs, reportId).percent;
}

}  // namespace

int main() {
    nuphy::BatteryFrame frame;
    for (int percent = 0; percent <= 100; ++percent) {
        assert(Consume(frame, 0x07, 0) == -1);
        assert(Consume(frame, 0x08 + (percent >> 4), 50) == -1);
        assert(Consume(frame, 0x10 + (percent & 0xf), 100) == -1);
        assert(Consume(frame, 0, 150) == percent);
    }

    // An out-of-range level and a missing start marker cannot publish a sample.
    assert(Consume(frame, 0x07, 0) == -1);
    assert(Consume(frame, 0x0e, 50) == -1);
    assert(Consume(frame, 0x15, 100) == -1);
    assert(Consume(frame, 0, 150) == -1);
    assert(Consume(frame, 0x0c, 200) == -1);
    assert(Consume(frame, 0x19, 250) == -1);
    assert(Consume(frame, 0, 300) == -1);

    // A media key aborts a frame. A later start marker resynchronizes the decoder.
    assert(Consume(frame, 0x07, 1000) == -1);
    assert(Consume(frame, 0x0c, 1050) == -1);
    assert(Consume(frame, 0xe9, 1075) == -1);
    assert(Consume(frame, 0x19, 1100) == -1);
    assert(Consume(frame, 0, 1150) == -1);
    assert(Consume(frame, 0x07, 1200) == -1);
    assert(Consume(frame, 0x07, 1250) == -1);
    assert(Consume(frame, 0x0c, 1300) == -1);
    assert(Consume(frame, 0x19, 1325, 4) == -1);
    assert(Consume(frame, 0x19, 1350) == -1);
    assert(Consume(frame, 0, 1400) == 73);

    // Incomplete and expired sequences cannot carry over into the next burst.
    assert(Consume(frame, 0x07, 2000) == -1);
    assert(Consume(frame, 0x0c, 2050) == -1);
    assert(Consume(frame, 0, 2100) == -1);
    assert(Consume(frame, 0x07, 2500) == -1);
    assert(Consume(frame, 0x0c, 2550) == -1);
    assert(Consume(frame, 0x19, 2600) == -1);
    assert(Consume(frame, 0, 3001) == -1);

    for (int percent = 0; percent <= 100; ++percent) {
        assert(Consume(frame, 0x0f, 3500) == -1);
        assert(Consume(frame, 0x07, 3550) == -1);
        assert(Consume(frame, 0x08 + (percent >> 4), 3600) == -1);
        assert(Consume(frame, 0x10 + (percent & 0xf), 3650) == -1);
        const auto sample = ConsumeSample(frame, 0, 3700);
        assert(sample.percent == percent && sample.charging);
    }

    // A normal media key or an expired prefix clears charging for the next valid frame.
    assert(Consume(frame, 0x0f, 4000) == -1);
    assert(Consume(frame, 0xe9, 4025) == -1);
    assert(Consume(frame, 0x07, 4050) == -1);
    assert(Consume(frame, 0x0c, 4100) == -1);
    assert(Consume(frame, 0x19, 4150) == -1);
    const auto interrupted = ConsumeSample(frame, 0, 4200);
    assert(interrupted.percent == 73 && !interrupted.charging);

    assert(Consume(frame, 0x0f, 4500) == -1);
    assert(Consume(frame, 0x07, 5001) == -1);
    assert(Consume(frame, 0x0c, 5050) == -1);
    assert(Consume(frame, 0x19, 5100) == -1);
    const auto expired = ConsumeSample(frame, 0, 5150);
    assert(expired.percent == 73 && !expired.charging);
}
