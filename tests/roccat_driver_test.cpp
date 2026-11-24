// Production ROCCAT Syn Max Air poller with a simulated HID transport. The frames are what the receiver
// (1E7D:3A3F) sent in a USBPcap capture of ROCCAT Swarm: the status reply on battery and on the dock, and the
// unsolicited event reports the headset pushes on the same collection (mic arm, wheels).
#include "../src/drivers/drivers.h"

#include <cstdio>
#include <deque>

int RoccatSynPercentFromMillivolts(int mv);
bool RoccatSynDecode(const std::vector<BYTE>& r, DeviceStatus& s);

namespace {
std::vector<BYTE> Report(std::initializer_list<BYTE> head) {
    std::vector<BYTE> r(head);
    r.resize(64, 0);
    return r;
}
// 8A 83 06 02 LL 00 V1 V1 V2 V2 CC: level 5, 3920 mV, discharging (captured).
const std::vector<BYTE> kOnBattery = Report({ 0x8A, 0x83, 0x06, 0x02, 0x05, 0x00, 0x50, 0x0F, 0x50, 0x0F, 0x00 });
// On the dock a few seconds after docking: 3940 mV, charging flag set (captured).
const std::vector<BYTE> kOnDock = Report({ 0x8A, 0x83, 0x06, 0x02, 0x05, 0x00, 0x64, 0x0F, 0x64, 0x0F, 0x01 });
// Unsolicited event reports the headset pushes (mic arm flipped, wheel turned); tag E0 / E2, never 8A.
const std::vector<BYTE> kMicEvent = Report({ 0xE0, 0x06, 0x03, 0x00, 0x01, 0x72, 0x02, 0x9D, 0xE0, 0x2E });
const std::vector<BYTE> kWheelEvent = Report({ 0xE2, 0x06, 0x03, 0x00, 0x01, 0x00, 0x81, 0xBF, 0x20, 0x3C });
// Swarm's own status poll answer (tag A1), which a stale queue could still hold: not ours.
const std::vector<BYTE> kForeignReply = Report({ 0xA1, 0x84, 0x06, 0x02 });
const std::vector<BYTE> kRequest = { 0x00, 0x8A, 0x07, 0x06, 0x01 };

constexpr USHORT kVid = 0x1E7D, kPid = 0x3A3F;

int failures = 0, opens = 0, writes = 0, reads = 0, flushes = 0;
bool openOk = true, writeOk = true;
std::vector<BYTE> lastWrite;
std::deque<std::vector<BYTE>> pending;  // input reports in order; empty = timeout
const char* current = "";

void Check(bool result, const char* expression, int line) {
    if (!result) { std::printf("FAIL %s:%d %s\n", current, line, expression); ++failures; }
}
#define CHECK(x) Check((x), #x, __LINE__)

HidColl Vendor() {
    HidColl c;
    c.vid = kVid;
    c.pid = kPid;
    c.iface = 0;
    c.path = L"vendor";
    c.caps.UsagePage = 0xFF00;
    c.caps.Usage = 0x0001;
    c.caps.InputReportByteLength = c.caps.OutputReportByteLength = 65;  // 64 bytes + report ID 0 on Windows
    return c;
}
// The receiver's consumer-control collection (volume keys): never opened.
HidColl Consumer() {
    HidColl c = Vendor();
    c.iface = 5;
    c.path = L"consumer";
    c.caps.UsagePage = 0x000C;
    c.caps.InputReportByteLength = 2;
    c.caps.OutputReportByteLength = 0;
    return c;
}

PollContext Receiver(std::deque<std::vector<BYTE>> script) {
    opens = writes = reads = flushes = 0;
    openOk = writeOk = true;
    lastWrite.clear();
    pending = script;
    PollContext ctx;
    ctx.colls = { Consumer(), Vendor() };
    return ctx;
}

DeviceStatus PollOnce(std::deque<std::vector<BYTE>> script, ULONGLONG* elapsed = nullptr) {
    auto ctx = Receiver(script);
    auto start = GetTickCount64();
    PollRoccat(ctx);
    if (elapsed) *elapsed = GetTickCount64() - start;
    CHECK(ctx.out.size() == 1 && ctx.IsClaimed(kVid, kPid));
    return ctx.out.empty() ? DeviceStatus{} : ctx.out[0];
}
}  // namespace

bool HidDev::Open(const HidColl& c) {
    ++opens;
    c_ = c;
    CHECK(c.page() == 0xFF00);  // never the consumer-control collection
    return openOk;
}
void HidDev::Close() {}
void HidDev::Flush() { ++flushes; }
bool HidDev::Write(const std::vector<BYTE>& data, int) {
    ++writes;
    lastWrite = data;
    return writeOk;
}
int HidDev::Read(std::vector<BYTE>& out, int timeoutMs) {
    ++reads;
    CHECK(timeoutMs > 0 && timeoutMs <= 1500);
    if (pending.empty()) return -1;  // timeout
    out = pending.front();
    pending.pop_front();
    return (int)out.size();
}
std::wstring HidProduct(const HidColl&) { return L"SYN Max Air"; }
std::wstring Hex4(USHORT v) {
    wchar_t b[8];
    swprintf(b, 8, L"%04X", v);
    return b;
}

int main() {
    current = "voltage curve end points and measured points";
    CHECK(RoccatSynPercentFromMillivolts(4200) == 100);
    CHECK(RoccatSynPercentFromMillivolts(4080) == 100);
    CHECK(RoccatSynPercentFromMillivolts(3920) == 78);
    CHECK(RoccatSynPercentFromMillivolts(3740) == 34);
    CHECK(RoccatSynPercentFromMillivolts(3660) == 11);
    CHECK(RoccatSynPercentFromMillivolts(3400) == 0);
    CHECK(RoccatSynPercentFromMillivolts(3910) > RoccatSynPercentFromMillivolts(3890));  // monotonic between points

    current = "on battery: level, identity, the exact read-only request, one open and one write";
    {
        auto ctx = Receiver({ kOnBattery });
        PollRoccat(ctx);
        CHECK(ctx.out.size() == 1 && ctx.IsClaimed(kVid, kPid));
        CHECK(opens == 1 && writes == 1 && reads == 1 && flushes == 1);
        CHECK(lastWrite == kRequest);
        if (ctx.out.size() == 1) {
            const auto& s = ctx.out[0];
            CHECK(s.percent == 78 && s.millivolts == 3920 && !s.charging && !s.full && s.connected && !s.asleep);
            CHECK(s.key == L"roccat:3A3F");
            CHECK(s.name == L"ROCCAT Syn Max Air");
            CHECK(s.kind == DeviceKind::Headset);
            CHECK(s.via == L"2.4 GHz receiver");
            CHECK(s.detail == L"3920 mV, level 5/5");
            CHECK(s.graceSec >= 60);
        }
        auto before = opens;
        PollRoccat(ctx);  // already claimed: not asked twice
        CHECK(opens == before && ctx.out.size() == 1);
    }
    current = "event reports before the reply are skipped, the reply is still found";
    {
        auto s = PollOnce({ kMicEvent, kWheelEvent, kForeignReply, kOnBattery });
        CHECK(s.percent == 78 && s.connected && reads == 4);
    }
    current = "on the dock: charging, no voltage-derived percent, coarse level text";
    {
        auto s = PollOnce({ kOnDock });
        CHECK(s.charging && !s.full && s.percent == -1 && s.millivolts == 3940 && s.connected);
        CHECK(s.levelText == L"Charging");
    }
    current = "full on the dock";
    {
        DeviceStatus s;
        CHECK(RoccatSynDecode(Report({ 0x8A, 0x83, 0x06, 0x02, 0x05, 0x00, 0x60, 0x10, 0x60, 0x10, 0x01 }), s));
        CHECK(s.full && s.charging && s.levelText == L"Full" && s.millivolts == 4192);
    }
    current = "headset off: receiver answers nothing, entry hidden, poll ends within the deadline";
    {
        ULONGLONG ms = 0;
        auto s = PollOnce({}, &ms);
        CHECK(!s.connected && s.percent == -1 && !s.asleep);
        CHECK(ms < 1500);
        CHECK(writes == 1 && reads == 1);
    }
    current = "only events, no reply: hidden as well";
    {
        auto s = PollOnce({ kMicEvent, kWheelEvent });
        CHECK(!s.connected && reads == 3);
    }
    current = "collection held by Swarm: open fails, entry hidden, nothing written";
    {
        auto ctx = Receiver({ kOnBattery });
        openOk = false;
        PollRoccat(ctx);
        CHECK(ctx.out.size() == 1 && !ctx.out[0].connected && writes == 0);
    }
    current = "write fails: hidden";
    {
        auto ctx = Receiver({ kOnBattery });
        writeOk = false;
        PollRoccat(ctx);
        CHECK(ctx.out.size() == 1 && !ctx.out[0].connected && reads == 0);
    }
    current = "malformed replies are not decoded";
    {
        DeviceStatus s;
        CHECK(!RoccatSynDecode(Report({ 0x8A, 0x83 }), s));
        CHECK(!RoccatSynDecode(Report({ 0x8A, 0x06, 0x00 }), s));            // receiver "unknown" answer
        CHECK(!RoccatSynDecode(Report({ 0xA1, 0x83, 0x06, 0x02, 0x05 }), s));  // wrong tag
    }
    current = "another vendor's receiver is ignored";
    {
        PollContext ctx;
        HidColl c = Vendor();
        c.vid = 0x1038;
        ctx.colls = { c };
        opens = 0;
        PollRoccat(ctx);
        CHECK(ctx.out.empty() && opens == 0);
    }
    if (failures == 0) std::printf("roccat_driver_test: all passed\n");
    return failures == 0 ? 0 : 1;
}
