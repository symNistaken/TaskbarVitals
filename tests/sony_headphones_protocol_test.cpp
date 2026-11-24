#include "../src/drivers/sony_headphones_protocol.h"

#include <cassert>
#include <cstdio>
#include <vector>

using Bytes = std::vector<std::uint8_t>;

int main() {
    // Battery request for headphones (Gadgetbridge's encoding of 0C 00 / payload 10 00).
    Bytes req = SonyMdr::Encode(SonyMdr::kTypeCommand, 0, { 0x10, 0x00 });
    assert((req == Bytes{ 0x3E, 0x0C, 0x00, 0x00, 0x00, 0x00, 0x02, 0x10, 0x00, 0x1E, 0x3C }));

    // An ACK with seq 1: 01 01 00000000, checksum 02.
    Bytes ack = SonyMdr::Encode(SonyMdr::kTypeAck, 1, {});
    assert((ack == Bytes{ 0x3E, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x02, 0x3C }));

    // Bytes 3C / 3D / 3E inside the frame are escaped and come back unchanged.
    Bytes tricky = SonyMdr::Encode(SonyMdr::kTypeCommand, 1, { 0x11, 0x00, 0x3C, 0x01 });
    for (std::size_t i = 1; i + 1 < tricky.size(); i++) assert(tricky[i] != 0x3C && tricky[i] != 0x3E);
    Bytes buf = { 0x00, 0x42 };  // leading noise is skipped
    buf.insert(buf.end(), ack.begin(), ack.end());
    buf.insert(buf.end(), tricky.begin(), tricky.end());
    SonyMdr::Message m;
    assert(SonyMdr::TakeMessage(buf, m) && m.type == SonyMdr::kTypeAck && m.seq == 1 && m.payload.empty());
    assert(SonyMdr::TakeMessage(buf, m) && m.type == SonyMdr::kTypeCommand && m.seq == 1);
    SonyMdr::Battery b;
    assert(SonyMdr::ParseBattery(m.payload, b) && b.level == 60 && b.charging && !b.dual);
    assert(buf.empty() && !SonyMdr::TakeMessage(buf, m));

    // A frame split across reads is only taken once complete.
    Bytes half(req.begin(), req.begin() + 5), rest(req.begin() + 5, req.end());
    assert(!SonyMdr::TakeMessage(half, m));
    half.insert(half.end(), rest.begin(), rest.end());
    assert(SonyMdr::TakeMessage(half, m) && m.payload == (Bytes{ 0x10, 0x00 }));

    // A bad checksum is dropped.
    Bytes corrupt = req;
    corrupt[corrupt.size() - 2] ^= 1;
    assert(!SonyMdr::TakeMessage(corrupt, m));

    // Earbuds: the lower earbud wins; a 0 level means that earbud is not connected.
    SonyMdr::Battery e;
    assert(SonyMdr::ParseBattery({ 0x11, 0x01, 80, 0, 70, 1 }, e) && e.dual && e.level == 70 && e.charging);
    SonyMdr::Battery one;
    assert(SonyMdr::ParseBattery({ 0x11, 0x01, 0, 0, 55, 0 }, one) && one.left == -1 && one.level == 55);
    assert(SonyMdr::ParseBattery({ 0x11, 0x0A, 40, 0 }, e) && e.caseLevel == 40 && e.level == 70);
    assert(!SonyMdr::ParseBattery({ 0x01, 0x00, 0x00, 0x00 }, e));

    std::puts("sony_headphones_protocol_test: ok");
    return 0;
}
