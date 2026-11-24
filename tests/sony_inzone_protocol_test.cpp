// Sony INZONE HCI packets, checked against bytes captured from a first-generation INZONE H9 on its
// serial dongle (054C:0E53) and against the HID battery request of the H9 II / H5 / Buds.
#include "../src/drivers/sony_inzone_protocol.h"

#include <cassert>
#include <cstdio>
#include <vector>

using Bytes = std::vector<std::uint8_t>;

int main() {
    // Battery GET, tid 2: what the H9 dongle was sent on COM3 (and answered).
    assert(SonyHci::BatteryGet(2) == (Bytes{ 0x01, 0x00, 0xFC, 0x08, 0x96, 0xC3, 0x41, 0x04, 0x01, 0x02, 0x00, 0xA1 }));
    assert(SonyHci::BatteryGet(3).back() == 0xA2);
    // tid high byte goes into the packet and the checksum.
    Bytes high = SonyHci::BatteryGet(0x0102);
    assert(high[9] == 0x02 && high[10] == 0x01 && high[11] == 0xA2);

    // The H9's reply: RET, tid 2, not charging, 50 %.
    const Bytes reply = { 0x04, 0xFF, 0x0B, 0x00, 0x96, 0xC3, 0x14, 0x04, 0x10, 0x02, 0x00, 0x00, 0x32, 0xB5 };
    SonyHci::Event e;
    assert(SonyHci::ParseEvent(reply.data(), reply.size(), e));
    assert(e.id == 0x04 && e.type == 0x10 && e.tid == 2);
    assert(e.payload == (Bytes{ 0x00, 0x32 }));

    // Bad checksum, or addressed to someone other than the PC: rejected.
    Bytes bad = reply;
    bad.back() ^= 1;
    assert(!SonyHci::ParseEvent(bad.data(), bad.size(), e));
    Bytes notPc = reply;
    notPc[6] = 0x24;
    notPc.back() = (std::uint8_t)(notPc.back() + 0x10);
    assert(!SonyHci::ParseEvent(notPc.data(), notPc.size(), e));

    // Serial stream: noise, a command packet from the dongle, the reply split in two reads.
    Bytes buf = { 0x00, 0x7F, 0x01, 0x00, 0xFC, 0x02, 0xAA, 0xBB };
    buf.insert(buf.end(), reply.begin(), reply.begin() + 5);
    assert(!SonyHci::NextSerialEvent(buf, e));
    assert(buf.size() == 5);  // the partial event is kept, everything before it dropped
    buf.insert(buf.end(), reply.begin() + 5, reply.end());
    assert(SonyHci::NextSerialEvent(buf, e));
    assert(e.tid == 2 && e.payload == (Bytes{ 0x00, 0x32 }));
    assert(buf.empty());

    // Two replies back to back (tid 2 and 3): one per call; a corrupt one in front is skipped.
    const Bytes reply3 = { 0x04, 0xFF, 0x0B, 0x00, 0x96, 0xC3, 0x14, 0x04, 0x10, 0x03, 0x00, 0x00, 0x32, 0xB6 };
    buf = bad;
    buf.insert(buf.end(), reply.begin(), reply.end());
    buf.insert(buf.end(), reply3.begin(), reply3.end());
    assert(SonyHci::NextSerialEvent(buf, e) && e.tid == 2);
    assert(SonyHci::NextSerialEvent(buf, e) && e.tid == 3);
    assert(!SonyHci::NextSerialEvent(buf, e));

    // HID transport: the same packet behind [02][length] in a 64-byte report, byte for byte what the
    // driver sent before the shared header existed (H9 II / H5 / Buds).
    Bytes hid = { 0x02, 12, 0x01, 0x00, 0xFC, 8, 0x96, 0xC3, 0x41, 0x04, 0x01, 0x05, 0x00 };
    unsigned sum = 0;
    for (int i = 6; i <= 12; i++) sum += hid[i];
    hid.push_back((std::uint8_t)sum);
    Bytes packet = SonyHci::BatteryGet(5);
    assert(Bytes(hid.begin() + 2, hid.end()) == packet);

    std::puts("sony_inzone_protocol_test: OK");
    return 0;
}
