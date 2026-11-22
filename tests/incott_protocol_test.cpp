#include "../src/drivers/incott_protocol.h"

#include <cassert>
#include <cstdio>

using namespace incott_protocol;

static Bytes Pad(Bytes b, size_t n) {
    b.resize(n, 0);
    return b;
}

int main() {
    // Captured on a first-generation Incott G23 Pro (PAW3950) behind its 8K receiver 093A:522C, FF05:0001,
    // 33-byte input report: 73%, not charging, 1000 Hz, dpi tier 4 (hw 63), debounce 4, LOD hw 2.
    const Bytes push8k = Pad({ 0x09, 0x49, 0x40, 0x04, 0x00, 0x3F, 0x3F, 0x21 }, 33);
    // Same receiver, same minute: the 89 feature reply, unchanged for 43 hours and across a PC restart.
    const Bytes feature8k = { 0x09, 0x89, 0x3A, 0x00, 0x00, 0x3B, 0x00, 0x00, 0x5A };
    // Same receiver: device info 8F, first read and a later read with a residue byte shifted in.
    const Bytes info8k = { 0x09, 0x8F, 0x01, 0x02, 0x02, 0xF0, 0xF1, 0x00, 0x02 };
    const Bytes info8kShifted = { 0x09, 0x8F, 0x01, 0x02, 0x02, 0xF0, 0xF0, 0xF1, 0x02 };

    Layout layout = Layout::Unknown;
    Status push;
    assert(ParseStatusPush(push8k, layout, push));
    assert(push.percent == 73 && !push.charging && layout == Layout::Packed);

    Status feature;
    assert(ParseStatusFeature(feature8k, feature));
    assert(feature.percent == 58 && !feature.charging);  // what BatteryHub 1.1.0 and oexi showed

    assert(ParseReceiverKind(info8k) == Receiver::EightK);
    assert(ParseReceiverKind(info8kShifted) == Receiver::EightK);
    assert(ParseReceiverKind(feature8k) == Receiver::Unknown);

    // 8K receiver: the pushed status wins over the receiver's own 89 answer.
    Reading r = Choose(false, Receiver::EightK, push, feature);
    assert(r.show && r.percent == 73 && !r.charging && !r.asleep && r.connected);

    // 8K receiver and no push since the last poll: asleep, no percent, even though 89 still says 58.
    r = Choose(false, Receiver::EightK, Status{}, feature);
    assert(r.show && r.asleep && r.percent == -1 && r.connected);

    // G23 V2 behind an 8K receiver answers 89 with zeros (oexi tests/read-info.test.mjs): no data.
    Status none;
    assert(!ParseStatusFeature({ 0x09, 0x89, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x5A }, none));

    // Cable: the mouse answers 89 itself (G23 V2 capture in oexi tests: 97%), so it is still used.
    Status wired;
    assert(ParseStatusFeature({ 0x09, 0x89, 0x61, 0x02, 0x03, 0x02, 0x00, 0x00, 0x5A }, wired));
    r = Choose(true, Receiver::Other, Status{}, wired);
    assert(r.show && r.percent == 97 && r.connected && !r.asleep);

    // A receiver that is not an 8K one (1K receivers do not forward the push) keeps the 89 reading.
    r = Choose(false, Receiver::Other, Status{}, feature);
    assert(r.show && r.percent == 58);

    // A receiver that never answered device info gets no entry rather than an unverified 89 value.
    r = Choose(false, Receiver::Unknown, Status{}, Status{});
    assert(!r.show);

    // A valid push at 0% comes from a mouse that is on: shown as 0%, not as switched off.
    layout = Layout::Unknown;
    Status zero;
    assert(ParseStatusPush(Pad({ 0x09, 0x00, 0x40, 0x04, 0x00, 0x3F, 0x3F, 0x21 }, 33), layout, zero));
    r = Choose(false, Receiver::EightK, zero, Status{});
    assert(r.show && r.connected && r.percent == 0 && !r.asleep);
    // An all-zero push carries nothing (no rate, dpi or LOD): not a 0% reading.
    Status blank;
    assert(!ParseStatusPush(Pad({ 0x09 }, 33), layout, blank));
    // 0 in a receiver's 89 answer still means the mouse is off or out of range (unchanged).
    r = Choose(false, Receiver::Other, Status{}, Status{ 0, false });
    assert(r.show && !r.connected && r.percent == -1);

    // Same G23 Pro on a charger for about 2 minutes, dongle still in: the push sets the charging bit
    // (73% then 95%), and after unplugging reads 98% then 79%, while 89 kept answering 58% not charging.
    layout = Layout::Unknown;
    Status live;
    assert(ParseStatusPush(Pad({ 0x09, 0xC9, 0x40, 0x04, 0x00, 0x3F, 0x3F, 0x21 }, 33), layout, live));
    assert(live.percent == 73 && live.charging && layout == Layout::Packed);
    assert(ParseStatusPush(Pad({ 0x09, 0xDF, 0x40, 0x04, 0x00, 0x3F, 0x3F, 0x21 }, 33), layout, live));
    assert(live.percent == 95 && live.charging);
    r = Choose(false, Receiver::EightK, live, feature);
    assert(r.percent == 95 && r.charging && r.connected);
    assert(ParseStatusPush(Pad({ 0x09, 0x62, 0x40, 0x04, 0x00, 0x3F, 0x3F, 0x21 }, 33), layout, live));
    assert(live.percent == 98 && !live.charging);
    assert(ParseStatusPush(Pad({ 0x09, 0x4F, 0x40, 0x04, 0x00, 0x3F, 0x3F, 0x21 }, 33), layout, live));
    assert(live.percent == 79 && !live.charging);

    // Charging bit in the packed layout (G23 V2 frame from oexi tests/firmware-1403.test.mjs).
    layout = Layout::Unknown;
    Status chg;
    assert(ParseStatusPush({ 0x09, 0x80 | 97, 0x10, 0x00, 0x21, 0x2C, 0x01, 0x20, 0x00 }, layout, chg));
    assert(chg.percent == 97 && chg.charging && layout == Layout::Packed);
    r = Choose(false, Receiver::EightK, chg, Status{});
    assert(r.percent == 97 && r.charging && r.connected);

    // Older firmware behind a 1K receiver: 5-byte layout [charging][percent][tier][rate] (oexi issue #11).
    layout = Layout::Unknown;
    Status old;
    assert(ParseStatusPush({ 0x09, 0x00, 85, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00 }, layout, old));
    assert(old.percent == 85 && !old.charging);
    assert(ParseStatusPush({ 0x09, 0x01, 40, 0x03, 0x04, 0x00, 0x00, 0x00, 0x00 }, layout, old));
    assert(old.percent == 40 && old.charging);
    // Only the old layout fits (byte 2 > 0x56): settles it.
    assert(ParseStatusPush({ 0x09, 0x00, 100, 0x02, 0x03, 0x00, 0x00, 0x00, 0x00 }, layout, old));
    assert(old.percent == 100 && layout == Layout::Old);

    // Ambiguous frame: follows the layout settled by earlier frames of the same device.
    const Bytes ambiguous = { 0x09, 0x01, 0x21, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00 };
    layout = Layout::Packed;
    Status amb;
    assert(ParseStatusPush(ambiguous, layout, amb));
    assert(amb.percent == 1 && layout == Layout::Packed);
    layout = Layout::Unknown;
    assert(ParseStatusPush(ambiguous, layout, amb));
    assert(amb.percent == 0x21 && amb.charging && layout == Layout::Unknown);

    // Not a status: other report ids, short frames, values outside both layouts.
    layout = Layout::Unknown;
    Status bad;
    assert(!ParseStatusPush(Pad({ 0x08, 0x49, 0x40, 0x04 }, 33), layout, bad));
    assert(!ParseStatusPush({ 0x09, 0x49, 0x40 }, layout, bad));
    assert(!ParseStatusPush(Pad({ 0x09, 0xE5, 0xFF, 0xFF }, 33), layout, bad));
    assert(layout == Layout::Unknown);
    assert(!ParseStatusFeature({ 0x09, 0x89, 0xE5, 0x00 }, bad));  // 101% is not a percent
    assert(!ParseStatusFeature({ 0x09, 0x8F, 0x3A, 0x00 }, bad));  // reply to another command

    std::puts("incott_protocol_test: all passed");
    return 0;
}
