// Production G735 poller with a simulated HID transport (no hardware required).
#include "../src/drivers/drivers.h"

#include <cstdio>

namespace {
int failures = 0, opens = 0, writes = 0, reads = 0;
bool repeatReply = false;
std::vector<std::vector<BYTE>> replies, sent;
const char* current = "";

void Check(bool ok, const char* expression, int line) {
    if (!ok) { std::printf("FAIL %s:%d %s\n", current, line, expression); ++failures; }
}
#define CHECK(x) Check((x), #x, __LINE__)

const BYTE kReqHeader[16] = { 0xF2, 0x01, 0x05, 0x1E, 0xFA, 0x82, 0x40, 0xCE, 0, 0, 0, 0x20, 0, 0, 0, 0 };
const BYTE kRepHeader[16] = { 0xF2, 0x00, 0x00, 0x0C, 0x1E, 0x82, 0x40, 0x00, 0x00, 0xF2, 0x00, 0x08, 0, 0, 0, 0 };

HidColl Collection() {
    HidColl c;
    c.vid = 0x046D; c.pid = 0x0AD8; c.iface = 0;
    c.path = L"g735-dongle";
    c.caps.UsagePage = 0xFF03; c.caps.Usage = 0x0001;
    c.caps.InputReportByteLength = c.caps.OutputReportByteLength = 271;
    return c;
}
PollContext Context() {
    opens = writes = reads = 0;
    repeatReply = false;
    replies.clear(); sent.clear();
    PollContext ctx;
    ctx.colls = { Collection() };
    return ctx;
}
std::vector<BYTE> Reply(std::initializer_list<BYTE> msg) {
    std::vector<BYTE> r(kRepHeader, kRepHeader + 16);
    r.insert(r.end(), msg);
    r.resize(271, 0);
    return r;
}
std::vector<BYTE> Request(std::initializer_list<BYTE> msg) {
    std::vector<BYTE> r(kReqHeader, kReqHeader + 16);
    r.insert(r.end(), msg);
    return r;
}
const DeviceStatus* Only(const PollContext& ctx) { return ctx.out.size() == 1 ? &ctx.out[0] : nullptr; }
}  // namespace

std::wstring Hex4(USHORT v) { wchar_t b[8]; swprintf(b, 8, L"%04X", v); return b; }  // used for the device key
bool HidDev::Open(const HidColl& c) { ++opens; c_ = c; return true; }
void HidDev::Close() {}
void HidDev::Flush() {}
bool HidDev::Write(const std::vector<BYTE>& data, int timeoutMs) {
    ++writes;
    sent.push_back(data);
    CHECK(timeoutMs > 0 && timeoutMs <= 500);
    return true;
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
    current = "battery level as captured from the real headset (41 %, discharging)";
    auto ctx = Context();
    replies = { Reply({ 0x00, 0x0B, 0x02, 0x00, 0x03 }), Reply({ 0x02, 0x0B, 0x29, 0x28, 0x00 }) };
    PollLogitechG735(ctx);
    CHECK(writes == 2 && ctx.IsClaimed(0x046D, 0x0AD8));
    // Only root getFeature(0x0104) and battery function 0 are sent.
    CHECK(sent.size() == 2 && sent[0] == Request({ 0x00, 0x0B, 0x01, 0x04 }) && sent[1] == Request({ 0x02, 0x0B }));
    if (auto s = Only(ctx)) {
        CHECK(s->percent == 41 && s->connected && !s->charging);
        CHECK(s->key == L"g735:0AD8" && s->name == L"Logitech G735" && s->kind == DeviceKind::Headset);
    } else CHECK(false);

    current = "charging states 1 and 2";
    for (BYTE state : { (BYTE)1, (BYTE)2 }) {
        ctx = Context();
        replies = { Reply({ 0x00, 0x0B, 0x02 }), Reply({ 0x02, 0x0B, 0x50, 0x50, state }) };
        PollLogitechG735(ctx);
        if (auto s = Only(ctx)) CHECK(s->percent == 80 && s->charging); else CHECK(false);
    }

    current = "replies to other software ids and other reports are skipped";
    ctx = Context();
    replies = { Reply({ 0x00, 0x0D, 0x07 }), std::vector<BYTE>(20, 0), Reply({ 0x00, 0x0B, 0x02 }),
                Reply({ 0x02, 0x0D, 0x05, 0x05, 0x00 }), Reply({ 0x02, 0x0B, 0x63, 0x62, 0x00 }) };
    PollLogitechG735(ctx);
    if (auto s = Only(ctx)) CHECK(s->percent == 99 && s->connected); else CHECK(false);

    current = "headset off: HID++ error from the dongle";
    ctx = Context();
    replies = { Reply({ 0xFF, 0x00, 0x0B, 0x05 }) };
    PollLogitechG735(ctx);
    CHECK(writes == 1);
    if (auto s = Only(ctx)) CHECK(!s->connected); else CHECK(false);

    current = "headset off: no answer";
    ctx = Context();
    PollLogitechG735(ctx);
    if (auto s = Only(ctx)) CHECK(!s->connected); else CHECK(false);

    current = "implausible level";
    ctx = Context();
    replies = { Reply({ 0x00, 0x0B, 0x02 }), Reply({ 0x02, 0x0B, 0xC8, 0x00, 0x00 }) };
    PollLogitechG735(ctx);
    if (auto s = Only(ctx)) CHECK(!s->connected); else CHECK(false);

    current = "unrelated report flood stays within one deadline";
    ctx = Context();
    replies = { Reply({ 0x00, 0x0D, 0x07 }) };
    repeatReply = true;
    auto start = GetTickCount64();
    PollLogitechG735(ctx);
    CHECK(GetTickCount64() - start < 1500);
    if (auto s = Only(ctx)) CHECK(!s->connected); else CHECK(false);

    current = "other collections, products and claimed devices are not touched";
    for (int field = 0; field < 5; ++field) {
        ctx = Context();
        auto& c = ctx.colls[0];
        switch (field) {
            case 0: c.pid = 0x0ADF; break;
            case 1: c.caps.UsagePage = 0xFF00; break;
            case 2: c.caps.Usage = 2; break;
            case 3: c.caps.InputReportByteLength = 64; break;
            case 4: c.caps.OutputReportByteLength = 64; break;
        }
        PollLogitechG735(ctx);
        CHECK(ctx.out.empty() && opens == 0);
    }
    ctx = Context(); ctx.Claim(0x046D, 0x0AD8); PollLogitechG735(ctx);
    CHECK(ctx.out.empty() && opens == 0);

    if (failures) { std::printf("G735: %d failed checks\n", failures); return 1; }
    std::puts("G735 tests passed (battery, charging, foreign replies, headset off, deadline, strict matching).");
}
