// AULA SC580 part of the production AULA poller with a simulated HID transport. Replies captured from an
// SC580 (firmware 1.6.01) on its 089D:062F receiver and 088D:062E cable.
#include "../src/drivers/drivers.h"

#include <cstdio>

void PollAula(PollContext& ctx);

namespace {
// First bytes of a 32-byte payload (zero padded) and its last byte, the checksum.
std::vector<BYTE> Reply(std::initializer_list<BYTE> head, BYTE checksum = 0) {
    std::vector<BYTE> r(head);
    r.resize(32, 0);
    r[31] = checksum;
    return r;
}
// Receiver, mouse awake on battery: 90%.
const std::vector<BYTE> kAwake = Reply({ 0x30, 0x00, 0x01, 0x0B, 0x4D, 0x31, 0x33, 0x37, 0x01, 0x16, 0x11, 0xAA,
                                         0x00, 0x5A, 0x01 }, 0x15);
// Receiver, mouse dozing or switched off.
const std::vector<BYTE> kAsleep = Reply({ 0x30, 0x02, 0x01 });
// Cable while charging: always level 100, whatever the real charge.
const std::vector<BYTE> kCharging = Reply({ 0x30, 0x00, 0x01, 0x0B, 0x4D, 0x31, 0x33, 0x37, 0x01, 0x16, 0x11, 0xAA,
                                            0x01, 0x64, 0x01 }, 0x20);
// Pushed by the receiver when the link comes back.
const std::vector<BYTE> kPush = Reply({ 0xD1, 0x01, 0x5A, 0x00, 0x01, 0x11, 0x01 });

std::vector<BYTE> WithChecksum(std::vector<BYTE> r) {
    unsigned s = 0;
    for (int i = 4; i < 31; i++) s += r[i];
    r[31] = (BYTE)s;
    return r;
}

int failures = 0, opens = 0, writes = 0;
bool openOk = true, writeOk = true, ioError = false;
std::vector<std::vector<BYTE>> replies;
std::vector<std::wstring> opened;
const char* current = "";

void Check(bool result, const char* expression, int line) {
    if (!result) {
        std::printf("FAIL %s:%d %s\n", current, line, expression);
        ++failures;
    }
}
#define CHECK(x) Check((x), #x, __LINE__)

HidColl Collection(bool cable) {
    HidColl c;
    c.vid = cable ? 0x088D : 0x089D;
    c.pid = cable ? 0x062E : 0x062F;
    c.iface = 2;
    c.path = cable ? L"cable" : L"receiver";
    c.caps.UsagePage = 0xFF02;
    c.caps.Usage = 0x0000;
    c.caps.InputReportByteLength = c.caps.OutputReportByteLength = 33;
    return c;
}

PollContext Context(std::initializer_list<bool> cables) {
    opens = writes = 0;
    openOk = writeOk = true;
    ioError = false;
    replies.clear();
    opened.clear();
    PollContext ctx;
    for (bool cable : cables) ctx.colls.push_back(Collection(cable));
    return ctx;
}

const DeviceStatus* Only(const PollContext& ctx) { return ctx.out.size() == 1 ? &ctx.out[0] : nullptr; }
}  // namespace

bool HidDev::Open(const HidColl& c) {
    c_ = c;
    ++opens;
    opened.push_back(c.path);
    return openOk;
}
void HidDev::Close() {}
void HidDev::Flush() {}
bool HidDev::Write(const std::vector<BYTE>& data, int) {
    // 30 00 01 00 behind report id 0, zero data, zero checksum: the read-only device-info request.
    std::vector<BYTE> expected(33, 0);
    expected[1] = 0x30;
    expected[3] = 0x01;
    CHECK(data == expected);
    ++writes;
    return writeOk;
}
int HidDev::Read(std::vector<BYTE>& buf, int timeoutMs) {
    if (ioError) return -1;  // fails at once, like an unplugged device
    if (replies.empty()) {
        Sleep((DWORD)timeoutMs);  // a real timeout
        return -1;
    }
    buf = replies.front();
    replies.erase(replies.begin());
    return (int)buf.size();
}
std::wstring HidProduct(const HidColl&) { return L"Wireless-Receiver"; }
std::wstring Hex4(USHORT v) {
    wchar_t b[8];
    swprintf(b, 8, L"%04X", v);
    return b;
}

int main() {
    current = "captured receiver reply, awake on battery";
    auto ctx = Context({ false });
    replies = { kAwake };
    PollAula(ctx);
    const DeviceStatus* s = Only(ctx);
    CHECK(s != nullptr);
    if (s) {
        CHECK(s->percent == 90 && !s->charging && !s->asleep && s->connected);
        CHECK(s->name == L"AULA SC580" && s->kind == DeviceKind::Mouse && s->via == L"2.4 GHz receiver");
        CHECK(s->key == L"aula:sc580");
    }
    CHECK(ctx.IsClaimed(0x089D, 0x062F) && writes == 1);

    current = "receiver says offline: listed as asleep without a level, so the tray keeps the last one";
    ctx = Context({ false });
    replies = { kAsleep };
    PollAula(ctx);
    s = Only(ctx);
    CHECK(s != nullptr);
    if (s) CHECK(s->asleep && s->connected && s->percent == -1 && !s->charging);
    CHECK(ctx.IsClaimed(0x089D, 0x062F));

    current = "charging on the cable: the fixed 100 is not shown";
    ctx = Context({ true });
    replies = { kCharging };
    PollAula(ctx);
    s = Only(ctx);
    CHECK(s != nullptr);
    if (s) CHECK(s->charging && s->percent == -1 && !s->asleep && s->via == L"USB cable");

    current = "charge state 2 (full) is a real 100";
    ctx = Context({ true });
    auto full = kCharging;
    full[12] = 0x02;
    replies = { WithChecksum(full) };
    PollAula(ctx);
    s = Only(ctx);
    CHECK(s != nullptr);
    if (s) CHECK(s->percent == 100 && !s->charging);

    current = "cable and receiver present: one entry from the cable, receiver claimed and left alone";
    ctx = Context({ false, true });
    replies = { kCharging };
    PollAula(ctx);
    CHECK(ctx.out.size() == 1 && opens == 1 && opened[0] == L"cable");
    CHECK(ctx.IsClaimed(0x088D, 0x062E) && ctx.IsClaimed(0x089D, 0x062F));

    current = "pushed notification skipped, stale reply resent, busy retried";
    ctx = Context({ false });
    replies = { kPush, Reply({ 0x13, 0x00, 0x01 }), kAwake };
    PollAula(ctx);
    CHECK(Only(ctx) && Only(ctx)->percent == 90 && writes == 2);
    ctx = Context({ false });
    replies = { Reply({ 0x30, 0x01, 0x01 }), kAwake };
    PollAula(ctx);
    CHECK(Only(ctx) && Only(ctx)->percent == 90 && writes == 2);

    current = "bad checksum and level above 100 rejected";
    ctx = Context({ false });
    auto damaged = kAwake;
    damaged[31] ^= 0xFF;
    replies = { damaged };
    PollAula(ctx);
    CHECK(ctx.out.empty() && !ctx.IsClaimed(0x089D, 0x062F));
    ctx = Context({ false });
    auto over = kAwake;
    over[13] = 101;
    replies = { WithChecksum(over) };
    PollAula(ctx);
    CHECK(ctx.out.empty());

    current = "strict collection matching, nothing opened otherwise";
    for (int field = 0; field < 5; ++field) {
        ctx = Context({ false });
        auto& c = ctx.colls[0];
        switch (field) {
        case 0: c.vid = 0x088E; break;
        case 1: c.pid = 0x0630; break;
        case 2: c.caps.UsagePage = 0xFF01; break;
        case 3: c.caps.Usage = 0x0002; break;
        case 4: c.caps.InputReportByteLength = 20; break;
        }
        replies = { kAwake };
        PollAula(ctx);
        CHECK(ctx.out.empty() && opens == 0 && writes == 0);
    }

    current = "silent device, I/O error and failed write: bounded, not claimed";
    ctx = Context({ false });
    ULONGLONG start = GetTickCount64();
    PollAula(ctx);
    ULONGLONG took = GetTickCount64() - start;
    CHECK(ctx.out.empty() && !ctx.IsClaimed(0x089D, 0x062F) && took < 1300 && writes >= 2);
    ctx = Context({ false });
    ioError = true;
    start = GetTickCount64();
    PollAula(ctx);
    CHECK(ctx.out.empty() && GetTickCount64() - start < 200 && writes == 1);
    ctx = Context({ false });
    writeOk = false;
    PollAula(ctx);
    CHECK(ctx.out.empty() && writes == 1);

    if (failures) {
        std::printf("AULA SC580: %d failed checks\n", failures);
        return 1;
    }
    std::puts("AULA SC580 driver tests passed (captured replies, asleep, charging, full, dedup, retries, matching, deadline).");
}
