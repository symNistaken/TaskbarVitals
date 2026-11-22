#include "../src/drivers/drivers.h"
#include <cassert>
#include <cstdio>
#include <functional>

namespace {
using Frame = std::function<std::vector<BYTE>(BYTE)>;
std::vector<Frame> pending;
std::vector<std::vector<BYTE>> writes;
BYTE sequence = 0;
bool openOk = true, writeOk = true;
int opens = 0;
USHORT Crc(const std::vector<BYTE>& r, size_t count) {
    USHORT crc = 0xFFFF;
    for (size_t i = 0; i < count; ++i) {
        crc ^= (USHORT)r[i] << 8;
        for (int b = 0; b < 8; ++b) crc = (USHORT)((crc << 1) ^ ((crc & 0x8000) ? 0x1021 : 0));
    }
    return crc;
}
void Seal(std::vector<BYTE>& r) {
    size_t end = 9 + r[7]; USHORT crc = Crc(r, end);
    r[end] = (BYTE)(crc >> 8); r[end + 1] = (BYTE)crc;
}
Frame Reply(BYTE value) {
    return [value](BYTE seq) {
        std::vector<BYTE> r(64, 0);
        r[0] = 0xFE; r[1] = seq; r[2] = 0x10; r[4] = 2; r[7] = 3;
        r[9] = 1; r[10] = 1; r[11] = value; Seal(r); return r;
    };
}
HidColl Collection() {
    HidColl c; c.vid = 0x3654; c.pid = 0x6901; c.iface = 3;
    c.caps.UsagePage = 0xFF02; c.caps.Usage = 2;
    c.caps.InputReportByteLength = c.caps.OutputReportByteLength = 65; return c;
}
PollContext Poll(std::vector<Frame> frames, HidColl c = Collection()) {
    pending = std::move(frames); writes.clear(); opens = 0;
    PollContext ctx; ctx.colls = { c }; PollDarkrim(ctx); return ctx;
}
}
bool HidDev::Open(const HidColl&) { ++opens; return openOk; }
void HidDev::Close() {}
void HidDev::Flush() {}
bool HidDev::Write(const std::vector<BYTE>& r, int timeout) {
    assert(timeout == 200); writes.push_back(r);
    assert(r.size() == 14 && r[0] == 0 && r[1] == 0xFE);
    assert(r[2] >= 1 && r[2] <= 15); sequence = r[2];
    assert(std::vector<BYTE>(r.begin() + 3, r.begin() + 12) ==
           std::vector<BYTE>({ 0x10, 0, 1, 0, 1, 2, 0, 1, 0 }));
    std::vector<BYTE> frame(r.begin() + 1, r.end());
    assert(Crc(frame, 11) == (USHORT)((r[12] << 8) | r[13]));
    return writeOk;
}
int HidDev::Read(std::vector<BYTE>& r, int timeout) {
    assert(timeout > 0 && timeout <= 800);
    if (pending.empty()) return -1;
    auto next = pending.front(); pending.erase(pending.begin()); r = next(sequence); return (int)r.size();
}
int main() {
    // Captured reply FE 03 10 00 02 00 00 03 00 01 01 28 96 0D.
    auto captured = Reply(40)(3);
    assert(captured[12] == 0x96 && captured[13] == 0x0D);
    for (int i = 0; i < 32; ++i) {
        auto ctx = Poll({ Reply(40) });
        assert(ctx.out.size() == 1 && ctx.out[0].percent == 40 && !ctx.out[0].charging);
        assert(ctx.out[0].kind == DeviceKind::Headset && ctx.IsClaimed(0x3654, 0x6901));
    }
    for (BYTE value : { 0, 100, 0x80, 0xE4 }) {
        auto ctx = Poll({ Reply(value) });
        assert(ctx.out.size() == 1 && ctx.out[0].percent == (value & 0x7F));
        assert(ctx.out[0].charging == ((value & 0x80) != 0));
    }
    assert(Poll({ Reply(101) }).out.empty());
    // Reject wrong magic, sequence, command, response type, fragments, lengths,
    // non-battery TLVs and CRC errors; continue to a valid matching response.
    for (int index : { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 12, 13 }) {
        Frame bad = [index](BYTE seq) {
            auto r = Reply(40)(seq); r[index] ^= (index == 6 ? 2 : 1);
            if (index < 7 || index == 9 || index == 10) Seal(r);
            return r;
        };
        assert(Poll({ bad }).out.empty());
        assert(Poll({ bad, Reply(40) }).out.size() == 1);
    }
    assert(Poll({ [](BYTE) { return std::vector<BYTE>(8, 0); } }).out.empty());
    assert(Poll({}).out.empty());
    for (int field = 0; field < 7; ++field) {
        auto c = Collection();
        if (field == 0) ++c.vid;
        if (field == 1) ++c.pid;
        if (field == 2) ++c.iface;
        if (field == 3) ++c.caps.UsagePage;
        if (field == 4) ++c.caps.Usage;
        if (field == 5) --c.caps.InputReportByteLength;
        if (field == 6) --c.caps.OutputReportByteLength;
        assert(Poll({ Reply(40) }, c).out.empty() && opens == 0);
    }
    openOk = false; assert(Poll({ Reply(40) }).out.empty() && writes.empty()); openOk = true;
    writeOk = false; assert(Poll({ Reply(40) }).out.empty()); writeOk = true;
    PollContext claimed; claimed.colls = { Collection() }; claimed.Claim(0x3654, 0x6901);
    opens = 0; PollDarkrim(claimed); assert(opens == 0);
    std::puts("darkrim_driver_test: ok");
}
