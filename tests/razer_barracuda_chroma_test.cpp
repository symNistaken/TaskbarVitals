// Razer Barracuda X Chroma: exercise the production headset poller with simulated HID.
#include "../src/drivers/drivers.h"

#include <cassert>
#include <cstdio>
#include <deque>

namespace {
std::deque<std::vector<BYTE>> reports;
int battery = 72;
int charging = 1;
int writes = 0;
int opens = 0;
bool openOk = true;
bool offline = false;
bool badChecksum = false;
bool badStatus = false;
bool wrongTarget = false;
bool wrongAck = false;
bool shortReport = false;
bool staleReply = false;
bool noChargingReply = false;

std::vector<BYTE> Reply(BYTE property, BYTE value) {
    std::vector<BYTE> r(64, 0);
    r[0] = 0x02; r[1] = 0x02; r[2] = 0x60;
    r[6] = 0x05; r[8] = 0x80; r[9] = 0x80;
    r[10] = property; r[11] = 0x01; r[12] = 0x01; r[13] = value;
    BYTE checksum = 0;
    for (int i = 1; i < 62; ++i) checksum ^= r[i];
    r[62] = checksum;
    return r;
}

void Rechecksum(std::vector<BYTE>& r) {
    BYTE checksum = 0;
    for (int i = 1; i < 62; ++i) checksum ^= r[i];
    r[62] = checksum;
}

HidColl Collection(USHORT pid = 0x0574, USHORT page = 0xFF14,
                   USHORT usage = 0x0001, int iface = 3, USHORT vid = 0x1532) {
    HidColl c;
    c.vid = vid; c.pid = pid; c.iface = iface;
    c.caps.UsagePage = page; c.caps.Usage = usage;
    c.caps.InputReportByteLength = 64;
    c.caps.OutputReportByteLength = 64;
    c.caps.FeatureReportByteLength = 9;
    return c;
}

void Reset() {
    reports.clear();
    battery = 72; charging = 1;
    writes = opens = 0;
    openOk = true; offline = badChecksum = badStatus = wrongTarget = false;
    wrongAck = shortReport = staleReply = noChargingReply = false;
}

PollContext Poll(const HidColl& c = Collection()) {
    PollContext ctx;
    ctx.colls = { c };
    PollRazerHeadsets(ctx);
    return ctx;
}

void ExpectStatus(int percent, bool isCharging) {
    auto ctx = Poll();
    assert(ctx.out.size() == 1 && ctx.IsClaimed(0x1532, 0x0574));
    const auto& s = ctx.out.front();
    assert(s.name == L"Razer Barracuda X Chroma");
    assert(s.key == L"barracuda:0574");
    assert(s.kind == DeviceKind::Headset);
    assert(s.via == L"2.4 GHz receiver");
    assert(s.connected && s.percent == percent && s.charging == isCharging);
    assert(writes == 2 && opens == 1);
}

void ExpectNothing() {
    auto ctx = Poll();
    assert(ctx.out.empty() && !ctx.IsClaimed(0x1532, 0x0574));
}
} // namespace

bool HidDev::Open(const HidColl& c) {
    ++opens;
    c_ = c;
    return openOk;
}
bool HidDev::OpenFeatureOnly(const HidColl& c) {
    return Open(c);
}
void HidDev::Close() {}
void HidDev::Flush() { reports.clear(); }
bool HidDev::Write(const std::vector<BYTE>& b, int timeoutMs) {
    assert(timeoutMs > 0 && timeoutMs <= 500);
    assert(c_.vid == 0x1532 && c_.pid == 0x0574);
    assert(c_.iface == 3 && c_.page() == 0xFF14 && c_.usage() == 1);
    assert(b.size() == 64);
    assert(b[0] == 0x02 && b[1] == 0 && b[2] == 0x60);
    assert(b[6] == 0x04 && b[9] == 0x80);
    assert(b[10] == 0x21 || b[10] == 0x2A);
    BYTE checksum = 0;
    for (int i = 0; i < 62; ++i) checksum ^= b[i];
    assert(b[62] == checksum);
    ++writes;
    if (offline || (b[10] == 0x2A && noChargingReply)) return true;
    if (staleReply && b[10] == 0x21) reports.push_back(Reply(0x2A, 1));
    auto r = Reply(b[10], static_cast<BYTE>(b[10] == 0x21 ? battery : charging));
    if (b[10] == 0x21) {
        if (badChecksum) r[62] ^= 1;
        if (badStatus) { r[1] = 0x05; Rechecksum(r); }
        if (wrongTarget) { r[9] = 0x00; Rechecksum(r); }
        if (wrongAck) { r[11] = 0x00; Rechecksum(r); }
        if (shortReport) r.resize(63);
    }
    reports.push_back(r);
    return true;
}
int HidDev::Read(std::vector<BYTE>& r, int timeoutMs) {
    assert(timeoutMs > 0 && timeoutMs <= 650);
    if (reports.empty()) return -1;
    r = reports.front();
    reports.pop_front();
    return static_cast<int>(r.size());
}
bool HidDev::SetFeature(const std::vector<BYTE>&) { return false; }
bool HidDev::GetFeature(BYTE, std::vector<BYTE>&) { return false; }
bool HidDev::GetInputReport(BYTE, std::vector<BYTE>&) { return false; }
std::wstring Hex4(USHORT v) {
    wchar_t b[8];
    swprintf(b, 8, L"%04X", v);
    return b;
}
int Clamp100(int v) { return v < 0 ? 0 : v > 100 ? 100 : v; }
int MapTo100(int v, int lo, int hi) { return Clamp100((v - lo) * 100 / (hi - lo)); }

int main() {
    Reset(); ExpectStatus(72, true);
    Reset(); battery = 0; charging = 0; ExpectStatus(0, false);
    Reset(); battery = 100; ExpectStatus(100, true);
    Reset(); staleReply = true; ExpectStatus(72, true); // unrelated report skipped
    Reset(); noChargingReply = true; ExpectStatus(72, false);
    Reset(); charging = 2; ExpectStatus(72, false); // invalid charging cannot mean true

    Reset(); offline = true; ExpectNothing();
    Reset(); battery = 101; ExpectNothing();
    Reset(); badChecksum = true; ExpectNothing();
    Reset(); badStatus = true; ExpectNothing();
    Reset(); wrongTarget = true; ExpectNothing();
    Reset(); wrongAck = true; ExpectNothing();
    Reset(); shortReport = true; ExpectNothing();
    Reset(); openOk = false; ExpectNothing(); assert(writes == 0);

    for (const auto& invalid : {
        Collection(0x0575), Collection(0x0574, 0xFF13),
        Collection(0x0574, 0xFF14, 2), Collection(0x0574, 0xFF14, 1, 2),
        Collection(0x0574, 0xFF14, 1, 3, 0x1533)
    }) {
        Reset();
        auto ctx = Poll(invalid);
        assert(ctx.out.empty() && ctx.claimed.empty());
        assert(opens == 0 && writes == 0);
    }
    std::puts("Razer Barracuda X Chroma tests passed (battery, charging, fixed sequence, "
              "CRC, reply correlation, status, sleep, malformed replies and HID filtering).");
}
