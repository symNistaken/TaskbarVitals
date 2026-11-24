// Exercise the JBL driver for the Quantum 810 Wireless dongle (0ECB:2069) with a simulated HID transport; no hardware
// access. The 810 answers the HIDV1 GET 68 / GET 49 feature reports on interface 5 (Windows dump of a real dongle:
// one HID collection, MI_05 FF13:0001, input 2 / output 62 / feature 64). Only read requests may be sent, and the
// other JBL models must keep their exact matching.
#include "../src/drivers/drivers.h"

#include <cassert>
#include <cstdio>
#include <map>

namespace {
// What the simulated dongle answers, per HID interface number.
struct Behaviour {
    BYTE link = 1, percent = 0;
};
std::map<int, Behaviour> dongle;
std::vector<std::vector<BYTE>> setFeatures;  // every SetFeature payload
std::vector<BYTE> getFeatureIds;             // every GetFeatureByte report id
int writes = 0, opens = 0, writableProbes = 0;
bool openedReadOnly = false;

// Default: the 810's collection exactly as `BatteryHub.exe --dump` lists it.
HidColl Collection(int iface = 5, USHORT page = 0xFF13, USHORT usage = 0x0001, USHORT feat = 64, USHORT pid = 0x2069) {
    HidColl c;
    c.path = L"jbl";
    c.vid = 0x0ECB;
    c.pid = pid;
    c.iface = iface;
    c.caps.UsagePage = page;
    c.caps.Usage = usage;
    c.caps.FeatureReportByteLength = feat;
    c.caps.InputReportByteLength = 2;
    c.caps.OutputReportByteLength = 62;
    return c;
}

PollContext Poll(std::vector<HidColl> colls, std::map<int, Behaviour> b) {
    dongle = std::move(b);
    setFeatures.clear();
    getFeatureIds.clear();
    writes = opens = 0;
    PollContext ctx;
    ctx.colls = std::move(colls);
    PollJbl(ctx);
    return ctx;
}
Behaviour Answers(BYTE percent, BYTE link = 1) {
    Behaviour b;
    b.link = link;
    b.percent = percent;
    return b;
}
}  // namespace

// Stand-ins for the Win32 HID transport. The production driver is linked unchanged.
bool HidDev::Open(const HidColl& c) {
    c_ = c;
    h_ = reinterpret_cast<HANDLE>(1);
    overlapped_ = true;
    openedReadOnly = false;
    ++opens;
    return true;
}
bool HidDev::OpenReadOnly(const HidColl& c) {
    Open(c);
    openedReadOnly = true;
    return true;
}
void HidDev::Close() { h_ = INVALID_HANDLE_VALUE; }
void HidDev::Flush() {}
bool HidDev::Write(const std::vector<BYTE>&, int) { ++writes; return true; }
int HidDev::Read(std::vector<BYTE>&, int) { return -1; }
bool HidDev::GetFeatureByte(BYTE id, BYTE& value) {
    getFeatureIds.push_back(id);
    if (!openedReadOnly) ++writableProbes;  // the HIDV1 probe must only ever use a read-only handle
    auto it = dongle.find(c_.iface);
    if (it == dongle.end() || (id != 0x68 && id != 0x49)) return false;
    value = id == 0x68 ? it->second.link : it->second.percent;
    return true;
}
bool HidDev::SetFeature(const std::vector<BYTE>& data) {
    setFeatures.push_back(data);
    return true;
}
std::wstring Hex4(USHORT v) {
    wchar_t text[8];
    swprintf(text, 8, L"%04X", v);
    return text;
}

int main() {
    // The real dongle layout: level, name, kind, claim.
    auto ctx = Poll({ Collection() }, { { 5, Answers(75) } });
    assert(ctx.out.size() == 1);
    assert(ctx.out[0].key == L"jbl:2069" && ctx.out[0].name == L"JBL Quantum 810 Wireless");
    assert(ctx.out[0].kind == DeviceKind::Headset && ctx.out[0].via == L"2.4 GHz receiver");
    assert(ctx.out[0].percent == 75 && !ctx.out[0].charging);
    assert(ctx.IsClaimed(0x0ECB, 0x2069));
    // Only read requests: GET 68 then GET 49 on a read-only handle; no feature written, nothing on the output pipe.
    assert((getFeatureIds == std::vector<BYTE>{ 0x68, 0x49 }));
    assert(setFeatures.empty() && writes == 0);

    // 100 % is a level; 101 is not.
    ctx = Poll({ Collection() }, { { 5, Answers(100) } });
    assert(ctx.out.size() == 1 && ctx.out[0].percent == 100);
    ctx = Poll({ Collection() }, { { 5, Answers(101) } });
    assert(ctx.out.empty() && ctx.claimed.empty());

    // Headset off (link 0) or an unknown link value: nothing listed, and no battery request after the link one.
    for (BYTE link : { 0x00, 0x02, 0xFF }) {
        ctx = Poll({ Collection() }, { { 5, Answers(60, link) } });
        assert(ctx.out.empty() && ctx.claimed.empty());
        assert((getFeatureIds == std::vector<BYTE>{ 0x68 }));
    }

    // No answer at all: nothing listed, nothing claimed.
    ctx = Poll({ Collection() }, {});
    assert(ctx.out.empty() && ctx.claimed.empty());

    // Only interface 5 is probed: the same answer on another interface is ignored and never opened.
    ctx = Poll({ Collection(3), Collection(0, 0x000C) }, { { 3, Answers(75) }, { 0, Answers(75) } });
    assert(ctx.out.empty() && opens == 0);
    // A collection without a feature report is never opened.
    ctx = Poll({ Collection(5, 0xFF13, 0x0001, 1) }, { { 5, Answers(75) } });
    assert(ctx.out.empty() && opens == 0);

    // The 810 is not an HIDV3 device: even a FF12 collection with HIDV3-sized reports gets no HIDV3 query.
    HidColl ff12 = Collection(3, 0xFF12, 0);
    ff12.caps.InputReportByteLength = 64;
    ctx = Poll({ ff12 }, { { 3, Answers(75) } });
    assert(ctx.out.empty() && setFeatures.empty());

    // A claimed product is skipped without opening anything.
    PollContext claimed;
    claimed.colls = { Collection() };
    claimed.Claim(0x0ECB, 0x2069);
    dongle = { { 5, Answers(75) } };
    opens = 0;
    PollJbl(claimed);
    assert(claimed.out.empty() && opens == 0);

    // Exact matching is kept for the other models.
    ctx = Poll({ Collection(5, 0x0001, 0, 64, 0x206B) }, { { 5, Answers(50) } });   // 350 needs interface 3
    assert(ctx.out.empty());
    ctx = Poll({ Collection(3, 0x0001, 1, 64, 0x206B) }, { { 3, Answers(50) } });   // and usage 0
    assert(ctx.out.empty());
    ctx = Poll({ Collection(3, 0x0001, 0, 64, 0x206B) }, { { 3, Answers(50) } });
    assert(ctx.out.size() == 1 && ctx.out[0].name == L"JBL Quantum 350 Wireless");
    ctx = Poll({ Collection(3, 0xFF13, 1, 64, 0x205C) }, { { 3, Answers(50) } });   // 610 needs interface 5
    assert(ctx.out.empty());
    ctx = Poll({ Collection(5, 0xFF13, 1, 64, 0x205C) }, { { 5, Answers(50) } });
    assert(ctx.out.size() == 1 && ctx.out[0].name == L"JBL Quantum 610 Wireless");
    ctx = Poll({ Collection(5, 0xFF13, 1, 64, 0x2070) }, { { 5, Answers(50) } });   // unknown PID
    assert(ctx.out.empty());

    // Every HIDV1 probe in this file ran on a read-only handle.
    assert(writableProbes == 0);

    std::puts("JBL Quantum 810 driver tests passed (real layout, boundaries, link off, interface 5 only, no HIDV3, "
              "read-only requests, other models).");
}
