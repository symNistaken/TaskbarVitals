// Production Redragon (Evision) poller with a simulated HID transport; the request and reply bytes are the ones
// captured from the vendor software talking to a K1NG M999 on its 320F:226F receiver (battery shown: 97 %).
#include "../src/drivers/drivers.h"

#include <cstdio>

void PollRedragonEvision(PollContext& ctx);

namespace {
std::vector<BYTE> Report(std::initializer_list<BYTE> head) {
    std::vector<BYTE> r(head);
    r.resize(64, 0);
    return r;
}
// Captured: request 04 1C 00 1A 02, reply 04 1C 00 1A 02 00 00 00 61 00 (0x61 = 97 %).
const std::vector<BYTE> kCaptured = Report({ 0x04, 0x1C, 0x00, 0x1A, 0x02, 0x00, 0x00, 0x00, 0x61, 0x00 });
// Captured keep-alive that is not a battery reply: 04 AA 00 AA ... FF
const std::vector<BYTE> kPing = Report({ 0x04, 0xAA, 0x00, 0xAA, 0x00, 0x00, 0x00, 0x00, 0xFF, 0x00 });

int failures = 0, opens = 0, flushes = 0, writes = 0, reads = 0;
bool openOk = true, writeOk = true;
std::vector<BYTE> lastWrite;
std::vector<std::vector<BYTE>> replies;
const char* current = "";

void Check(bool result, const char* expression, int line) {
    if (!result) { std::printf("FAIL %s:%d %s\n", current, line, expression); ++failures; }
}
#define CHECK(x) Check((x), #x, __LINE__)

HidColl Collection(USHORT vid, USHORT pid, USHORT page, USHORT usage, const wchar_t* path) {
    HidColl c;
    c.vid = vid;
    c.pid = pid;
    c.caps.UsagePage = page;
    c.caps.Usage = usage;
    c.caps.InputReportByteLength = c.caps.OutputReportByteLength = 65;
    c.path = path;
    return c;
}

PollContext Context() {
    opens = flushes = writes = reads = 0;
    openOk = writeOk = true;
    lastWrite.clear();
    replies.clear();
    PollContext ctx;
    ctx.colls.push_back(Collection(0x320F, 0x226F, 0x0001, 0x0002, L"mouse"));
    ctx.colls.push_back(Collection(0x320F, 0x226F, 0xFF00, 0x0000, L"other-vendor"));
    ctx.colls.push_back(Collection(0x320F, 0x226F, 0xFF1C, 0x0092, L"vendor"));
    return ctx;
}

}  // namespace

bool HidDev::Open(const HidColl& c) {
    ++opens;
    c_ = c;
    CHECK(c.path == L"vendor");  // only the FF1C:0092 collection, never the mouse or the other vendor ones
    return openOk;
}
void HidDev::Close() {}
void HidDev::Flush() { ++flushes; }
bool HidDev::Write(const std::vector<BYTE>& data, int) {
    ++writes;
    lastWrite = data;
    return writeOk;
}
int HidDev::Read(std::vector<BYTE>& buf, int timeoutMs) {
    ++reads;
    CHECK(timeoutMs > 0 && timeoutMs <= 500);
    if (replies.empty()) return -1;
    buf = replies.front();
    replies.erase(replies.begin());
    return (int)buf.size();
}
std::wstring Hex4(USHORT v) {
    wchar_t b[8];
    swprintf(b, 8, L"%04X", v);
    return b;
}

int main() {
    current = "request matches the vendor software and a captured reply gives 97%";
    {
        auto ctx = Context();
        // The first request of a process uses sequence 1 (the capture used 0x1C), so the echoed sequence is rewritten.
        replies = { kCaptured };
        replies[0][1] = 0x01;
        replies[0][2] = 0x00;
        PollRedragonEvision(ctx);
        CHECK(ctx.out.size() == 1 && flushes == 1 && opens == 1 && writes == 1);
        CHECK(lastWrite.size() == 32 && lastWrite[0] == 0x04 && lastWrite[3] == 0x1A && lastWrite[4] == 0x02);
        CHECK(ctx.IsClaimed(0x320F, 0x226F));
        if (ctx.out.size() == 1) {
            const auto& s = ctx.out[0];
            CHECK(s.percent == 97 && !s.charging && s.connected && !s.asleep);
            CHECK(s.key == L"redragon_evision:226F");
            CHECK(s.kind == DeviceKind::Mouse);
            CHECK(s.via == L"2.4 GHz receiver");
            CHECK(s.vid == 0x320F);
        }
        auto before = opens;
        PollRedragonEvision(ctx);  // already claimed: no second query
        CHECK(opens == before && ctx.out.size() == 1);
    }

    current = "keep-alive and mismatched replies are skipped";
    {
        auto ctx = Context();
        auto good = kCaptured;
        good[1] = 0x02;  // second request of the process
        good[2] = 0x00;
        auto wrongSeq = good;
        wrongSeq[1] = 0x55;
        auto wrongCmd = good;
        wrongCmd[3] = 0x1B;
        auto nonZero = good;
        nonZero[5] = 1;
        replies = { kPing, wrongSeq, wrongCmd, nonZero, good };
        PollRedragonEvision(ctx);
        CHECK(ctx.out.size() == 1 && reads == 5);
        if (ctx.out.size() == 1) CHECK(ctx.out[0].percent == 97);
    }

    current = "charging flag follows the ATK driver";
    {
        auto ctx = Context();
        auto r = kCaptured;
        r[1] = 0x03;
        r[2] = 0x00;
        r[8] = 55;
        r[9] = 1;
        replies = { r };
        PollRedragonEvision(ctx);
        CHECK(ctx.out.size() == 1);
        if (ctx.out.size() == 1) CHECK(ctx.out[0].percent == 55 && ctx.out[0].charging);
    }

    current = "out-of-range percent is rejected as asleep";
    {
        auto ctx = Context();
        auto r = kCaptured;
        r[1] = 0x04;
        r[2] = 0x00;
        r[8] = 0xFF;
        replies = { r };
        PollRedragonEvision(ctx);
        CHECK(ctx.out.size() == 1);
        if (ctx.out.size() == 1) CHECK(ctx.out[0].asleep && ctx.out[0].percent == -1 && ctx.out[0].graceSec > 0);
    }

    current = "no reply: asleep, still claimed, bounded by the 1 s deadline";
    {
        auto ctx = Context();
        PollRedragonEvision(ctx);
        CHECK(ctx.out.size() == 1 && ctx.IsClaimed(0x320F, 0x226F));
        if (ctx.out.size() == 1) CHECK(ctx.out[0].asleep && ctx.out[0].percent == -1 && ctx.out[0].graceSec > 0);
    }

    current = "open or write failure";
    {
        auto ctx = Context();
        openOk = false;
        PollRedragonEvision(ctx);
        CHECK(ctx.out.size() == 1 && ctx.out[0].asleep);
        ctx = Context();
        writeOk = false;
        PollRedragonEvision(ctx);
        CHECK(ctx.out.size() == 1 && ctx.out[0].asleep && reads == 0);
    }

    current = "strict matching";
    {
        PollContext ctx;
        ctx.colls.push_back(Collection(0x320F, 0x5088, 0xFF1C, 0x0092, L"vendor"));  // ATK V75X receiver, not ours
        ctx.colls.push_back(Collection(0x320F, 0x226F, 0xFF00, 0x0000, L"other-vendor"));
        opens = 0;
        PollRedragonEvision(ctx);
        CHECK(ctx.out.empty() && opens == 0);
        PollContext empty;
        PollRedragonEvision(empty);
        CHECK(empty.out.empty());
    }

    if (failures) {
        std::printf("%d failure(s)\n", failures);
        return 1;
    }
    std::printf("Redragon Evision driver tests passed (captured request/reply, filtering, charging, asleep, strict matching).\n");
    return 0;
}
