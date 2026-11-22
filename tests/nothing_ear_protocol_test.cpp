#include "../src/drivers/nothing_ear_protocol.h"

#include <cassert>
#include <cstdio>
#include <vector>

using Bytes = std::vector<std::uint8_t>;

static Bytes Frame(std::uint16_t cmd, const Bytes& payload, std::uint8_t seq = 1) {
    Bytes f = {
        0x55, 0x60, 0x01,
        (std::uint8_t)(cmd & 0xFF), (std::uint8_t)(cmd >> 8),
        (std::uint8_t)payload.size(), 0x00, seq
    };
    f.insert(f.end(), payload.begin(), payload.end());
    std::uint16_t crc = NothingEar::Crc16Modbus(f.data(), f.size());
    f.push_back((std::uint8_t)(crc & 0xFF));
    f.push_back((std::uint8_t)(crc >> 8));
    return f;
}

int main() {
    auto q = NothingEar::BuildBatteryQuery(1);
    assert(q.size() == 10);
    assert(q[0] == 0x55 && q[1] == 0x60 && q[2] == 0x01);
    assert(q[3] == 0x07 && q[4] == 0xC0);

    // 3 parts: left 85% (not charging), right 100% (charging), case 60% (not charging)
    Bytes reply = Frame(NothingEar::kCmdBattery, { 0x03, 0x02, 85, 0x03, (std::uint8_t)(0x80 | 100), 0x04, 60 });
    NothingEar::Status st;
    assert(NothingEar::FindStatus(reply.data(), reply.size(), st));
    assert(st.left == 85 && !st.leftCharging);
    assert(st.right == 100 && st.rightCharging);
    assert(st.caseLevel == 60 && !st.caseCharging);

    // Push frame preceded by noise bytes
    Bytes stream = { 0x00, 0x11, 0x55 };
    Bytes push = Frame(NothingEar::kCmdBatteryPush, { 0x02, 0x02, 42, 0x04, (std::uint8_t)(0x80 | 90) }, 2);
    stream.insert(stream.end(), push.begin(), push.end());
    st = {};
    assert(NothingEar::FindStatus(stream.data(), stream.size(), st));
    assert(st.left == 42 && !st.leftCharging);
    assert(st.right == -1);
    assert(st.caseLevel == 90 && st.caseCharging);

    // Bad CRC must be rejected
    Bytes bad = reply;
    bad.back() ^= 0xFF;
    st = {};
    assert(!NothingEar::FindStatus(bad.data(), bad.size(), st));

    std::printf("nothing_ear_protocol_test: all assertions passed\n");
    return 0;
}
