// Production Audeze Maxwell poller with a simulated HID transport. kCapturedXbox and the empty frame 07 00 80
// are what an Xbox dongle (3329:4B18) sent while this driver was tested on it: the empty frame on every read
// while the dongle was stuck, the captured one after it was replugged. kAnswer51 is a Maxwell dongle's answer
// from HeadsetControl issue #573.
#include "../src/drivers/drivers.h"

#include <cstdio>
#include <map>

namespace {
std::vector<BYTE> Report(std::initializer_list<BYTE> head) {
    std::vector<BYTE> r(head);
    r.resize(62, 0);
    return r;
}
const std::vector<BYTE> kEmpty = Report({ 0x07, 0x00, 0x80 });
// 16 new bytes: acknowledgement and result, 0x44 = 68%. The rest of the frame is left over from earlier replies,
// including an older result (0x45 = 69%). The next reads repeat the frame with length 0: nothing new.
const std::vector<BYTE> kCapturedXbox = Report({
    0x07, 0x10, 0x80, 0x05, 0x5B, 0x03, 0x00, 0xD6, 0x0C, 0x00, 0x05, 0x5D, 0x05, 0x00, 0xD6, 0x0C, 0x00,
    0x00, 0x44, 0x01, 0x4A, 0xAE, 0x2B, 0xD3, 0x44, 0xC7, 0x80, 0x01, 0x05, 0x5C, 0x03, 0x00, 0x80, 0x2C,
    0x03, 0x05, 0x5B, 0x03, 0x00, 0xD6, 0x0C, 0x00, 0x05, 0x5D, 0x05, 0x00, 0xD6, 0x0C, 0x00, 0x00, 0x45 });
// The same headset on its USB-C cable (3329:4B1E): byte 2 is 00 instead of the dongle's 80. 0x45 = 69%.
const std::vector<BYTE> kCapturedXboxCable = Report({
    0x07, 0x10, 0x00, 0x05, 0x5B, 0x03, 0x00, 0xD6, 0x0C, 0x00, 0x05, 0x5D, 0x05, 0x00, 0xD6, 0x0C, 0x00,
    0x00, 0x45, 0x0C, 0x00, 0x00, 0x45, 0x1C, 0x05, 0x5C, 0x03, 0x00, 0x80, 0x2C, 0x03, 0x05, 0x5C, 0x03,
    0x00, 0x80, 0x2C, 0x01, 0x05, 0x5C, 0x03, 0x00, 0x80, 0x2C, 0x03, 0x05, 0x5C, 0x03, 0x00, 0x80, 0x2C,
    0x1C, 0x05, 0x5C, 0x03, 0x00, 0x80, 0x2C, 0x03, 0x05, 0x5C, 0x03 });
// The headset switched off on its cable, with the dongle also plugged in: the cable still answers while it charges,
// after three 5C notifications. Length 0x25, 0x4D = 77%.
const std::vector<BYTE> kCapturedXboxCableOff = Report({
    0x07, 0x25, 0x00, 0x05, 0x5C, 0x03, 0x00, 0x80, 0x2C, 0x03, 0x05, 0x5C, 0x03, 0x00, 0x80, 0x2C, 0x01,
    0x05, 0x5C, 0x03, 0x00, 0x80, 0x2C, 0x00, 0x05, 0x5B, 0x03, 0x00, 0xD6, 0x0C, 0x00, 0x05, 0x5D, 0x05,
    0x00, 0xD6, 0x0C, 0x00, 0x00, 0x4D, 0x2C, 0x1C, 0x05, 0x5B, 0x03, 0x00, 0xD6, 0x0C, 0x00, 0x05, 0x5D,
    0x05, 0x00, 0xD6, 0x0C, 0x00, 0x00, 0x4B, 0x00, 0xD6, 0x0C });
std::vector<BYTE> NothingNew(std::vector<BYTE> r) {
    r[1] = 0x00;
    return r;
}
// 5B acknowledgement and 5D result in one frame: length 0x10, level 0x33 = 51 at [18].
const std::vector<BYTE> kAnswer51 = Report({ 0x07, 0x10, 0x80, 0x05, 0x5B, 0x03, 0x00, 0xD6, 0x0C, 0x00,
                                             0x05, 0x5D, 0x05, 0x00, 0xD6, 0x0C, 0x00, 0x00, 0x33 });
// The same two messages in separate frames.
const std::vector<BYTE> kAckOnly = Report({ 0x07, 0x07, 0x80, 0x05, 0x5B, 0x03, 0x00, 0xD6, 0x0C, 0x00 });
std::vector<BYTE> ResultOnly(BYTE pct) { return Report({ 0x07, 0x09, 0x80, 0x05, 0x5D, 0x05, 0x00, 0xD6, 0x0C, 0x00, 0x00, pct }); }
std::vector<BYTE> Answer(BYTE pct) {
    auto r = kAnswer51;
    r[18] = pct;
    return r;
}
// Data, but no battery marker: a reply that carries the firmware string.
const std::vector<BYTE> kOther = Report({ 0x07, 0x0C, 0x80, 0x05, 0x5B, 0x08, 0x00, 0x01, 0x09, 0x00, 'v', '1', '.', '0' });
const std::vector<BYTE> kRequest = Report({ 0x06, 0x07, 0x80, 0x05, 0x5A, 0x03, 0x00, 0xD6, 0x0C });
const wchar_t* const kNote = L"unplug the dongle and plug it back in";

constexpr USHORT kVid = 0x3329, kXbox = 0x4B18, kPc = 0x4B19, kXboxCable = 0x4B1E;
const USHORT kAllPids[] = { 0x4B1A, 0x4B1E, 0x4B19, 0x4B18, 0x4B29, 0x4B28 };

int failures = 0, opens = 0, writes = 0, reads = 0;
bool openOk = true, writeOk = true, getOk = true;
std::vector<BYTE> lastWrite;
std::vector<std::wstring> opened;
std::map<std::wstring, std::vector<std::vector<BYTE>>> frames;  // per collection: frames in order, the last one repeats
std::map<std::wstring, size_t> cursor;
std::map<std::wstring, std::wstring> products;  // per collection; "Audeze Maxwell HID" when not set
const char* current = "";

void Check(bool result, const char* expression, int line) {
    if (!result) { std::printf("FAIL %s:%d %s\n", current, line, expression); ++failures; }
}
#define CHECK(x) Check((x), #x, __LINE__)

HidColl Vendor(USHORT pid, const wchar_t* path) {
    HidColl c;
    c.vid = kVid;
    c.pid = pid;
    c.iface = 0;
    c.path = path;
    c.caps.UsagePage = 0xFF13;
    c.caps.Usage = 0x0001;
    c.caps.InputReportByteLength = c.caps.OutputReportByteLength = 62;
    return c;
}

// The dongle's consumer-control collection: never opened.
HidColl Consumer(USHORT pid) {
    HidColl c = Vendor(pid, L"consumer");
    c.caps.UsagePage = 0x000C;
    c.caps.InputReportByteLength = 2;
    c.caps.OutputReportByteLength = 0;
    return c;
}

// The driver remembers a headset across polls; a poll without any Maxwell forgets it, like an unplugged dongle.
void Forget() {
    PollContext none;
    PollAudeze(none);
}

void ResetIo() {
    opens = writes = reads = 0;
    openOk = writeOk = getOk = true;
    lastWrite.clear();
    opened.clear();
    cursor.clear();
}

// The Xbox dongle as it enumerates on Windows; `forget` starts from a headset the driver has not seen.
PollContext XboxDongle(std::vector<std::vector<BYTE>> script, bool forget = true) {
    if (forget) {
        Forget();
        products.clear();
    }
    ResetIo();
    frames.clear();
    frames[L"xbox"] = script;
    PollContext ctx;
    ctx.colls = { Consumer(kXbox), Vendor(kXbox, L"xbox") };
    return ctx;
}

bool AllClaimed(const PollContext& ctx) {
    for (USHORT pid : kAllPids)
        if (!ctx.IsClaimed(kVid, pid)) return false;
    return true;
}

// One poll of the Xbox dongle; returns the single entry it reported.
DeviceStatus PollOnce(std::vector<std::vector<BYTE>> script, bool forget, ULONGLONG* elapsed = nullptr) {
    auto ctx = XboxDongle(script, forget);
    auto start = GetTickCount64();
    PollAudeze(ctx);
    if (elapsed) *elapsed = GetTickCount64() - start;
    CHECK(ctx.out.size() == 1 && AllClaimed(ctx));
    return ctx.out.empty() ? DeviceStatus{} : ctx.out[0];
}

void Level(std::vector<std::vector<BYTE>> script, int percent, int expectedReads) {
    auto s = PollOnce(script, true);
    if (s.percent != percent) std::printf("  got %d%%\n", s.percent);
    CHECK(s.percent == percent && !s.asleep && s.note.empty() && !s.charging);
    CHECK(writes == 1 && reads == expectedReads);
}
}  // namespace

bool HidDev::Open(const HidColl& c) {
    ++opens;
    c_ = c;
    opened.push_back(c.path);
    CHECK(c.page() == 0xFF13);  // never the consumer-control collection
    return openOk;
}
void HidDev::Close() {}
bool HidDev::Write(const std::vector<BYTE>& data, int) {
    ++writes;
    lastWrite = data;
    return writeOk;
}
bool HidDev::GetInputReport(BYTE reportId, std::vector<BYTE>& out) {
    ++reads;
    CHECK(reportId == 0x07);
    if (!getOk) return false;
    const auto& f = frames[c_.path];
    size_t& i = cursor[c_.path];
    out = f.empty() ? kEmpty : f[i < f.size() ? i : f.size() - 1];
    i++;
    return true;
}
std::wstring HidProduct(const HidColl& c) {
    auto it = products.find(c.path);
    return it == products.end() ? L"Audeze Maxwell HID" : it->second;
}

int main() {
    current = "Xbox dongle answers: level, identity, the exact read-only query";
    {
        auto ctx = XboxDongle({ kEmpty, kAnswer51 });
        PollAudeze(ctx);
        CHECK(ctx.out.size() == 1 && AllClaimed(ctx));
        CHECK(opens == 1 && writes == 1 && reads == 3);
        CHECK(lastWrite == kRequest);
        CHECK(opened.size() == 1 && opened[0] == L"xbox");
        if (ctx.out.size() == 1) {
            const auto& s = ctx.out[0];
            CHECK(s.percent == 51 && !s.asleep && !s.charging && s.connected && s.note.empty());
            CHECK(s.key == L"audeze:maxwell");
            CHECK(s.name == L"Audeze Maxwell");
            CHECK(s.kind == DeviceKind::Headset);
            CHECK(s.via == L"2.4 GHz dongle");
            CHECK(s.graceSec >= 60);
        }
        auto before = opens;
        PollAudeze(ctx);  // already claimed: not asked twice
        CHECK(opens == before && ctx.out.size() == 1);
    }
    current = "captured Xbox frame: the new result, not the older one left over in the frame";
    Level({ kCapturedXbox, NothingNew(kCapturedXbox), NothingNew(kCapturedXbox) }, 68, 3);
    current = "frames that only repeat an earlier answer are not empty";
    {
        PollOnce({ NothingNew(kCapturedXbox) }, true);
        auto s = PollOnce({ NothingNew(kCapturedXbox) }, false);
        CHECK(s.percent == 68 && s.note.empty() && !s.asleep && reads == 3);
    }
    current = "the newest answer within the first three reads counts";
    Level({ Answer(40), Answer(41), Answer(42) }, 42, 3);
    current = "1% and 100% are valid levels, above 100 is not a marker";
    Level({ Answer(1) }, 1, 3);
    Level({ Answer(100) }, 100, 3);
    Level({ Answer(101), Answer(101), Answer(101), Answer(77) }, 77, 4);

    current = "a late answer after several empty frames is still read";
    Level({ kEmpty, kEmpty, kEmpty, kEmpty, kEmpty, kEmpty, Answer(80) }, 80, 7);
    current = "acknowledgement and result in separate frames";
    Level({ kEmpty, kAckOnly, kEmpty, kEmpty, ResultOnly(75) }, 75, 5);
    current = "an acknowledgement on its own is not 0%";
    {
        auto s = PollOnce({ kAckOnly }, true);
        CHECK(s.asleep && s.percent < 0 && s.note.empty() && reads == 10);
    }

    current = "stuck dongle: asleep once, then the replug advice, bounded";
    {
        ULONGLONG took = 0;
        auto s = PollOnce({ kEmpty }, true, &took);
        CHECK(s.asleep && s.percent < 0 && s.note.empty() && s.connected);
        CHECK(writes == 1 && reads == 10 && took < 1000);
        s = PollOnce({ kEmpty }, false, &took);
        CHECK(!s.asleep && s.percent < 0 && s.note == kNote && s.connected && !s.charging);
        CHECK(writes == 1 && reads == 10 && took < 1000);
        s = PollOnce({ kEmpty }, false);
        CHECK(s.note == kNote);
        current = "stuck dongle: an answer clears the advice, and the count starts over";
        s = PollOnce({ kAnswer51 }, false);
        CHECK(s.percent == 51 && s.note.empty() && !s.asleep);
        s = PollOnce({ kEmpty }, false);
        CHECK(s.asleep && s.note.empty());
        s = PollOnce({ kEmpty }, false);
        CHECK(s.note == kNote);
    }
    current = "stuck dongle: an unplugged dongle starts over";
    {
        PollOnce({ kEmpty }, true);
        PollOnce({ kEmpty }, false);
        Forget();
        auto s = PollOnce({ kEmpty }, false);
        CHECK(s.asleep && s.note.empty());
    }

    current = "headset off (XBOX Dongle name): no I/O, claimed, nothing shown, stuck count cleared";
    {
        PollOnce({ kEmpty }, true);
        PollOnce({ kEmpty }, false);
        for (const wchar_t* name : { L"Audeze Maxwell XBOX Dongle", L"Audeze Maxwell Dongle" }) {
            auto ctx = XboxDongle({ kAnswer51 }, false);
            products[L"xbox"] = name;
            PollAudeze(ctx);
            CHECK(ctx.out.empty() && opens == 0 && writes == 0 && ctx.IsClaimed(kVid, kXbox));
        }
        products.clear();
        auto s = PollOnce({ kEmpty }, false);
        CHECK(s.asleep && s.note.empty());
    }

    current = "frames with other data are never called stuck";
    {
        ULONGLONG took = 0;
        for (bool forget : { true, false, false }) {
            auto s = PollOnce({ kOther }, forget, &took);
            CHECK(s.asleep && s.note.empty() && reads == 10 && took < 1000);
        }
        auto s = PollOnce({ kEmpty, kOther, kEmpty }, true);
        s = PollOnce({ kEmpty, kOther, kEmpty }, false);
        CHECK(s.asleep && s.note.empty());
    }
    current = "failed reads, failed write, failed open: asleep, never stuck, no extra reads";
    for (int fault = 0; fault < 3; fault++) {
        for (bool forget : { true, false, false }) {
            auto ctx = XboxDongle({ kEmpty }, forget);
            if (fault == 0) getOk = false;
            if (fault == 1) writeOk = false;
            if (fault == 2) openOk = false;
            PollAudeze(ctx);
            CHECK(ctx.out.size() == 1 && AllClaimed(ctx));
            if (ctx.out.size() == 1) CHECK(ctx.out[0].asleep && ctx.out[0].note.empty());
            CHECK(reads == (fault == 0 ? 3 : 0));
        }
    }

    current = "0% right after the headset appears is not believed";
    {
        auto s = PollOnce({ Answer(0) }, true);
        CHECK(s.asleep && s.percent < 0 && s.note.empty());
    }

    current = "PC dongle 4B19 reads the same way";
    {
        Forget();
        ResetIo();
        frames.clear();
        frames[L"pc"] = { kAnswer51 };
        PollContext ctx;
        ctx.colls = { Vendor(kPc, L"pc") };
        PollAudeze(ctx);
        CHECK(ctx.out.size() == 1 && AllClaimed(ctx));
        if (ctx.out.size() == 1) CHECK(ctx.out[0].percent == 51 && ctx.out[0].via == L"2.4 GHz dongle");
    }

    // Alone, the cable is a composite device (HID on interface 0 plus audio); with the dongle also plugged in it
    // re-enumerates as a HID-only device, so its collections have no interface number.
    current = "captured Xbox cable frame: level, USB cable, charging, composite and HID-only layouts";
    for (int iface : { 0, -1 }) {
        Forget();
        ResetIo();
        frames.clear();
        frames[L"cable"] = { kCapturedXboxCable, NothingNew(kCapturedXboxCable), NothingNew(kCapturedXboxCable) };
        PollContext ctx;
        ctx.colls = { Consumer(kXboxCable), Vendor(kXboxCable, L"cable") };
        for (auto& c : ctx.colls) c.iface = iface;
        PollAudeze(ctx);
        CHECK(ctx.out.size() == 1 && AllClaimed(ctx));
        CHECK(opened.size() == 1 && opened[0] == L"cable" && writes == 1 && reads == 3 && lastWrite == kRequest);
        if (ctx.out.size() == 1) {
            const auto& s = ctx.out[0];
            CHECK(s.percent == 69 && s.charging && !s.asleep && s.note.empty());
            CHECK(s.via == L"USB cable" && s.key == L"audeze:maxwell" && s.name == L"Audeze Maxwell");
        }
    }

    // The dongle then names itself "... XBOX Dongle" and only repeats its stale buffer (length 0, 76%).
    current = "headset off on its cable, dongle plugged in: the cable's charging level, the dongle untouched";
    {
        Forget();
        ResetIo();
        frames.clear();
        frames[L"cable"] = { kCapturedXboxCableOff, NothingNew(kCapturedXboxCableOff) };
        frames[L"xbox"] = { NothingNew(kCapturedXbox) };
        products[L"xbox"] = L"Audeze Maxwell XBOX Dongle";
        products[L"cable"] = L"Audeze Maxwell XBOX Headset";
        PollContext ctx;
        ctx.colls = { Vendor(kXbox, L"xbox"), Vendor(kXboxCable, L"cable") };
        for (auto& c : ctx.colls) c.iface = -1;
        PollAudeze(ctx);
        CHECK(ctx.out.size() == 1 && AllClaimed(ctx));
        CHECK(opened.size() == 1 && opened[0] == L"cable");
        if (ctx.out.size() == 1) {
            const auto& s = ctx.out[0];
            CHECK(s.percent == 77 && s.charging && !s.asleep && s.note.empty() && s.via == L"USB cable");
        }
        products.clear();
    }

    current = "cable wins over the dongle and means charging; a silent cable is never called stuck";
    for (int poll = 0; poll < 3; poll++) {
        if (poll == 0) Forget();
        ResetIo();
        frames.clear();
        frames[L"cable"] = { poll == 0 ? Answer(90) : kEmpty };
        frames[L"xbox"] = { Answer(50) };
        PollContext ctx;
        ctx.colls = { Vendor(kXbox, L"xbox"), Vendor(kXboxCable, L"cable") };
        PollAudeze(ctx);
        CHECK(ctx.out.size() == 1 && AllClaimed(ctx));
        CHECK(opened.size() == 1 && opened[0] == L"cable");
        if (ctx.out.size() == 1) {
            const auto& s = ctx.out[0];
            CHECK(s.via == L"USB cable" && s.note.empty());
            if (poll == 0) CHECK(s.percent == 90 && s.charging && !s.asleep);
            else CHECK(s.asleep && s.percent < 0);
        }
    }

    current = "strict matching: no I/O to other devices or collections";
    for (int field = 0; field < 4; ++field) {
        auto ctx = XboxDongle({ kAnswer51 });
        auto& c = ctx.colls.back();
        switch (field) {
        case 0: c.vid = 0x3328; break;
        case 1: c.pid = 0x4B17; break;
        case 2: c.caps.UsagePage = 0xFF00; break;
        case 3: c.caps.Usage = 0x0002; break;
        }
        PollAudeze(ctx);
        CHECK(ctx.out.empty() && ctx.claimed.empty() && opens == 0 && writes == 0);
    }
    {
        auto ctx = XboxDongle({ kAnswer51 });
        ctx.Claim(kVid, kXbox);
        PollAudeze(ctx);
        CHECK(ctx.out.empty() && opens == 0);
    }

    if (failures) { std::printf("Audeze: %d failed checks\n", failures); return 1; }
    std::puts("Audeze Maxwell driver tests passed (Xbox dongle, late and split answers, stuck dongle, headset off, faults, power-on 0%, PC dongle, cable, strict matching, deadline).");
}
