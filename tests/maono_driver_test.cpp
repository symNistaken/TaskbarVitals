// Production MAONO poller with a simulated HID transport (no hardware required).
#include "../src/drivers/drivers.h"

#include <algorithm>
#include <cstdio>

namespace {
int failures = 0, opens = 0, writes = 0, reads = 0, flushes = 0;
bool openOk = true, writeOk = true, repeatReply = false;
std::vector<std::vector<BYTE>> replies;
const char* current = "";

void Check(bool ok, const char* expression, int line) {
    if (!ok) { std::printf("FAIL %s:%d %s\n", current, line, expression); ++failures; }
}
#define CHECK(x) Check((x), #x, __LINE__)

HidColl Collection() {
    HidColl c;
    c.vid = 0x352F; c.pid = 0x040E; c.iface = 3;
    c.path = L"maono-receiver";
    c.caps.UsagePage = 0x000C; c.caps.Usage = 0x0001;
    c.caps.InputReportByteLength = c.caps.OutputReportByteLength = 64;
    return c;
}
PollContext Context() {
    opens = writes = reads = flushes = 0;
    openOk = writeOk = true; repeatReply = false;
    replies.clear();
    PollContext ctx;
    ctx.colls = { Collection() };
    return ctx;
}
void FixChecksum(std::vector<BYTE>& r) {
    size_t end = r[1] - 2;
    unsigned sum = 0;
    for (size_t i = 0; i < end; ++i) sum += r[i];
    USHORT checksum = static_cast<USHORT>(0u - sum);
    r[end] = static_cast<BYTE>(checksum);
    r[end + 1] = static_cast<BYTE>(checksum >> 8);
}
std::vector<BYTE> Battery(unsigned percent, USHORT reg = 0x0042) {
    std::vector<BYTE> r(64, 0);
    r[0] = 0xC4; r[1] = 11; r[4] = 0x04;
    r[5] = static_cast<BYTE>(reg); r[6] = static_cast<BYTE>(reg >> 8);
    r[7] = static_cast<BYTE>(percent); r[8] = static_cast<BYTE>(percent >> 8);
    FixChecksum(r);
    return r;
}
// Model the generic HID fallback's admission rule. The offline receiver can expose a
// standard battery field of zero, but a vendor-owned device must never reach that read.
void GenericFallback(PollContext& ctx) {
    if (ctx.IsClaimed(0x352F, 0x040E)) return;
    DeviceStatus s;
    s.key = L"generic:maono-receiver";
    s.percent = 0;
    ctx.Add(s, 0x352F, 0x040E);
}
void HiddenAndClaimed(PollContext& ctx) {
    PollMaono(ctx);
    CHECK(ctx.out.empty());
    CHECK(ctx.IsClaimed(0x352F, 0x040E));
    GenericFallback(ctx);
    CHECK(ctx.out.empty());
}
}  // namespace

bool HidDev::Open(const HidColl& c) { ++opens; c_ = c; return openOk; }
void HidDev::Close() {}
void HidDev::Flush() { ++flushes; }
bool HidDev::Write(const std::vector<BYTE>& data, int timeoutMs) {
    ++writes;
    std::vector<BYTE> expected(64, 0);
    const BYTE frame[] = {0xC4, 0x09, 0, 0, 0x04, 0x42, 0, 0xED, 0xFE};
    std::copy(std::begin(frame), std::end(frame), expected.begin());
    CHECK(data == expected);  // only the battery GET; no settings-changing SET
    CHECK(timeoutMs > 0 && timeoutMs <= 250);
    return writeOk;
}
int HidDev::Read(std::vector<BYTE>& out, int timeoutMs) {
    ++reads;
    CHECK(timeoutMs > 0 && timeoutMs <= 1000);
    if (replies.empty()) return -1;
    out = replies.front();
    if (!repeatReply) replies.erase(replies.begin());
    return static_cast<int>(out.size());
}

int main() {
    current = "connected, real zero, low and full battery";
    for (unsigned percent : {0u, 1u, 15u, 64u, 100u}) {
        auto ctx = Context(); replies = { Battery(percent) };
        PollMaono(ctx);
        CHECK(ctx.out.size() == 1 && ctx.IsClaimed(0x352F, 0x040E));
        CHECK(opens == 1 && flushes == 1 && writes == 1 && reads == 1);
        if (ctx.out.size() == 1) {
            const auto& s = ctx.out[0];
            CHECK(s.percent == static_cast<int>(percent) && s.connected && !s.asleep);
            CHECK(s.kind == DeviceKind::Microphone);
            CHECK(s.name == L"MAONO PD200W" && s.via == L"2.4 GHz receiver");
            CHECK(s.key == L"maono:pd200w" && s.graceSec == 0 && !s.charging);
        }
        PollMaono(ctx);
        GenericFallback(ctx);
        CHECK(ctx.out.size() == 1 && opens == 1 && writes == 1);
    }

    current = "mic off, receiver present: no generic zero battery entry";
    auto ctx = Context();
    HiddenAndClaimed(ctx);
    CHECK(writes == 1 && reads == 1);

    current = "open and write failures still reserve receiver";
    ctx = Context(); openOk = false;
    HiddenAndClaimed(ctx);
    CHECK(writes == 0 && reads == 0);
    ctx = Context(); writeOk = false;
    HiddenAndClaimed(ctx);
    CHECK(writes == 1 && reads == 0);

    current = "connected, disconnected, reconnected with same identity";
    ctx = Context(); replies = { Battery(64) }; PollMaono(ctx);
    const auto connected = ctx.out;
    ctx = Context(); HiddenAndClaimed(ctx);
    ctx = Context(); replies = { Battery(58) }; PollMaono(ctx);
    CHECK(ctx.out.size() == 1 && connected.size() == 1);
    if (ctx.out.size() == 1 && connected.size() == 1)
        CHECK(ctx.out[0].key == connected[0].key && ctx.out[0].percent == 58);

    current = "unplugged receiver is absent and not claimed";
    ctx = Context(); ctx.colls.clear(); PollMaono(ctx);
    CHECK(ctx.out.empty() && ctx.claimed.empty() && opens == 0);

    current = "strict identity and collection matching";
    for (int field = 0; field < 8; ++field) {
        ctx = Context();
        auto& c = ctx.colls[0];
        switch (field) {
            case 0: c.vid = 0x3530; break;
            case 1: c.pid = 0x0411; break;
            case 2: c.iface = 2; break;
            case 3: c.caps.UsagePage = 0xFF00; break;
            case 4: c.caps.Usage = 2; break;
            case 5: c.caps.InputReportByteLength = 63; break;
            case 6: c.caps.OutputReportByteLength = 63; break;
            case 7: c.caps.FeatureReportByteLength = 64; break;
        }
        PollMaono(ctx);
        CHECK(ctx.out.empty() && ctx.claimed.empty() && opens == 0);
    }
    ctx = Context(); ctx.Claim(0x352F, 0x040E); PollMaono(ctx);
    CHECK(ctx.out.empty() && opens == 0);

    current = "malformed and unrelated replies never revive offline receiver";
    std::vector<std::vector<BYTE>> invalid;
    invalid.push_back(std::vector<BYTE>(10));
    for (int field = 0; field < 9; ++field) {
        auto r = Battery(64);
        switch (field) {
            case 0: r[0] = 0xC5; break;
            case 1: r[1] = 65; break;
            case 2: r[1] = 10; break;
            case 3: r[2] = 1; FixChecksum(r); break;
            case 4: r[3] = 1; FixChecksum(r); break;
            case 5: r[4] = 3; FixChecksum(r); break;
            case 6: r[9] ^= 1; break;
            case 7: r = Battery(101); break;
            case 8: r = Battery(64, 0x0044); break;
        }
        invalid.push_back(r);
    }
    ctx = Context(); replies = invalid; HiddenAndClaimed(ctx);
    ctx = Context(); replies = invalid; replies.push_back(Battery(42)); PollMaono(ctx);
    CHECK(ctx.out.size() == 1);
    if (ctx.out.size() == 1) CHECK(ctx.out[0].percent == 42);

    current = "battery register may follow other register pairs";
    auto multi = Battery(0, 0x0044);
    multi[1] = 15; multi[9] = 0x42; multi[10] = 0; multi[11] = 37; multi[12] = 0;
    FixChecksum(multi);
    ctx = Context(); replies = { multi }; PollMaono(ctx);
    CHECK(ctx.out.size() == 1);
    if (ctx.out.size() == 1) CHECK(ctx.out[0].percent == 37);

    current = "continuous unrelated reports cannot extend query indefinitely";
    ctx = Context(); replies = { Battery(64, 0x0044) }; repeatReply = true;
    auto start = GetTickCount64();
    HiddenAndClaimed(ctx);
    CHECK(reads <= 128 && GetTickCount64() - start < 1500);

    if (failures) { std::printf("MAONO: %d failed checks\n", failures); return 1; }
    std::puts("MAONO tests passed (offline fallback, reconnect, microphone kind, real zero, strict matching, malformed, GET-only).");
}
