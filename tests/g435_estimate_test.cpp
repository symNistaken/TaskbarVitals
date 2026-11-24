// Logitech G435 Jieli receiver estimate (src/drivers/g435_estimate.h, PR #632).
#include "../src/drivers/g435_estimate.h"

#include <cassert>
#include <cstdio>

int main() {
    g435::State st;
    assert(g435::Estimate(st) == -1);  // never calibrated
    assert(g435::RecordReading(st, 80) && g435::Estimate(st) == 80);  // one reading: the reference
    assert(!g435::RecordReading(st, 80));
    assert(!g435::RecordReading(st, 101) && !g435::RecordReading(st, -1));

    // Use is counted only while in use, and long gaps count as at most kMaxStepSec.
    g435::Accumulate(st, true, 1000);  // first call only starts the clock
    assert(st.usageSec == 0);
    g435::Accumulate(st, true, 1060);
    assert(st.usageSec == 60);
    g435::Accumulate(st, false, 2000);
    assert(st.usageSec == 60);
    g435::Accumulate(st, true, 9000);
    assert(st.usageSec == 60 + g435::kMaxStepSec);

    // Too little use between readings: no rate learned.
    assert(g435::RecordReading(st, 78) && st.rateMilli == 0 && st.usageSec == 0);

    // Two hours of use, 78% -> 58%: 10 %/h.
    st.usageSec = 2 * 3600;
    assert(g435::RecordReading(st, 58) && st.rateMilli == 10000);
    st.usageSec = 3600;  // one more hour of use
    assert(g435::Estimate(st) == 48);
    st.usageSec = 100 * 3600;
    assert(g435::Estimate(st) == 1);  // never shown as empty

    // A higher reading (charged) moves the reference and keeps the rate.
    assert(g435::RecordReading(st, 100) && st.rateMilli == 10000 && g435::Estimate(st) == 100);
    // The next rate is smoothed with the old one: 20 %/h measured -> 0.7 * 10 + 0.3 * 20 = 13 %/h.
    st.usageSec = 3600;
    assert(g435::RecordReading(st, 80) && st.rateMilli == 13000);
    // Rates outside 1..50 %/h are clamped.
    st.usageSec = 3600;
    assert(g435::RecordReading(st, 0) && st.rateMilli == (13000 * 7 + 50000 * 3) / 10);
    puts("G435 estimate tests passed");
}
