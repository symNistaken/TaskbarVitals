#include "../src/reading_freshness.h"

#include <cassert>
#include <cstdio>

using namespace reading_freshness;

int main() {
    const std::uint64_t min = 60ULL * 1000, poll = 60 * 1000;
    assert(Classify(0, 1000, poll) == State::Unknown);
    assert(Classify(1, 1 + 2 * min, poll) == State::Fresh);
    assert(Classify(1, 1 + 3 * min, poll) == State::Delayed);
    assert(Classify(1, 1 + 10 * min, poll) == State::Stale);
    // A 30-minute poll interval: one interval later the reading is still fresh, not stale.
    assert(Classify(1, 1 + 30 * min, 30 * min) == State::Fresh);
    assert(Classify(1, 1 + 60 * min, 30 * min) == State::Delayed);
    assert(Classify(1, 1 + 90 * min, 30 * min) == State::Stale);
    assert(AgeMs(100, 50) == 0 && AgeMinutes(1, 1 + 5 * min) == 5);
    assert(Confirmed(50, true, false) && !Confirmed(50, true, true) && !Confirmed(-1, true, false) && !Confirmed(50, false, false));
    std::puts("reading_freshness_test: ok");
}
