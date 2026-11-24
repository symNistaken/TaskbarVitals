// Audeze Maxwell headsets (VID 3329) on the USB dongle or the USB-C cable.
// Ported from HaloBattery (github.com/HeyOkay/HaloBattery, providers/audeze.py, MIT; the dongle 4B19 and the
// cable 4B1A were tested there on hardware), whose packets come from HeadsetControl
// (lib/devices/audeze_maxwell.hpp). The Xbox dongle 4B18 and the Xbox cable 4B1E were tested with this driver;
// captured frames and the protocol notes are in docs/audeze_maxwell.md.
//   Vendor collection FF13:0001. Request: 62-byte output report 06; answer: 62-byte input report 07,
//   fetched with GetInputReport. The answer is a rolling buffer of recent replies, so it is scanned for the
//   battery marker D6 0C 00 00 <percent>.
//   Byte 1 of an answer is the length of the new data in it, and 0 means "nothing new yet" whatever the other
//   bytes hold (HeadsetControl, lib/devices/audeze_maxwell_device.hpp). The headset answers after 60-120 ms, so
//   three reads 60 ms apart are always made, and while no battery marker has come the dongle is read up to 10
//   times (about 0.6 s) before this poll gives up.
// Only the single battery query (attribute 0x0CD6) is sent, which HaloBattery measured to be enough on the
// dongle and the cable. HaloBattery falls back to HeadsetControl's 14-packet initialisation, which contains
// one packet it could not confirm to be a pure read, so that fallback is deliberately left out here.
// With no headset linked the dongle names itself "... Dongle" ("Audeze Maxwell XBOX Dongle" for the Xbox one)
// and keeps answering with a stale value, so it is not asked then. There is no charging flag: a Maxwell always
// charges on its USB-C cable, so the cable answering means charging.
// Stuck dongle: a dongle can stop passing requests to the headset while audio keeps playing. It then answers
// every request with an empty frame (07 00 80 00 ...) until it is unplugged and plugged back in; no request is
// known to bring it back. Seen on the Xbox dongle 4B18 (HaloBattery PR #107, and while testing this driver), on
// a Maxwell 2 dongle (HeadsetControl issue #573) and likely in HeadsetControl issue #460. One such poll is shown
// as asleep (a headset that is just waking up); from the second in a row the dongle is shown with no level and
// the advice to replug it, instead of "asleep" forever.
#include "drivers.h"

#include <cwctype>
#include <map>

namespace {

constexpr USHORT kVid = 0x3329;
struct Model { USHORT pid; bool cable; const wchar_t* name; };
const Model kModels[] = { { 0x4B1A, true, L"Audeze Maxwell" }, { 0x4B1E, true, L"Audeze Maxwell" },  // cables first: the direct connection wins
                          { 0x4B19, false, L"Audeze Maxwell" }, { 0x4B18, false, L"Audeze Maxwell" },
                          { 0x4B29, false, L"Audeze Maxwell 2" }, { 0x4B28, false, L"Audeze Maxwell 2" } };
constexpr int kReads = 3;       // always made: the answer can arrive a little later; the newest frame counts
constexpr int kMaxReads = 10;   // while no battery marker has come
constexpr int kStuckPolls = 2;  // polls in a row with only empty frames before a dongle counts as stuck

// The acknowledgement 5B 03 00 D6 0C <status 00> is not a level, although it looks like the marker with 0% when
// zero padding follows it in a frame of its own; the result is 5D 05 00 D6 0C 00 00 <percent>.
bool IsAcknowledgement(const std::vector<BYTE>& b, size_t i) {
    return i >= 3 && b[i - 3] == 0x5B && b[i - 2] == 0x03 && b[i - 1] == 0x00;
}

int FindLevel(const std::vector<BYTE>& b) {
    for (size_t i = 0; i + 4 < b.size(); i++)
        if (b[i] == 0xD6 && b[i + 1] == 0x0C && b[i + 2] == 0x00 && b[i + 3] == 0x00 && b[i + 4] <= 100 &&
            !IsAcknowledgement(b, i))
            return b[i + 4];
    return -1;
}

// Input report 07 whose length byte says it carries nothing new.
bool IsEmptyFrame(const std::vector<BYTE>& r) { return r.size() >= 3 && r[0] == 0x07 && r[1] == 0x00; }

struct Reading {
    int level = -1;
    bool silent = false;  // the dongle answered, but every frame was empty
};

Reading ReadLevel(const HidColl& c) {
    Reading out;
    HidDev d;
    if (!d.Open(c)) return out;
    std::vector<BYTE> req(62, 0);
    BYTE q[] = { 0x06, 0x07, 0x80, 0x05, 0x5A, 0x03, 0x00, 0xD6, 0x0C };
    std::copy(std::begin(q), std::end(q), req.begin());
    if (!d.Write(req)) return out;
    int frames = 0, empty = 0;
    for (int i = 0; i < kMaxReads; i++) {  // up to ~0.6 s for a late answer (replaces sending the query twice)
        if (i >= kReads && (out.level >= 0 || frames == 0)) break;  // answered, or the dongle cannot be read
        Sleep(60);
        std::vector<BYTE> r;
        if (!d.GetInputReport(0x07, r)) continue;
        frames++;
        if (IsEmptyFrame(r)) empty++;
        int l = FindLevel(r);
        if (l >= 0) out.level = l;
    }
    out.silent = out.level < 0 && frames > 0 && empty == frames;
    return out;
}

}  // namespace

void PollAudeze(PollContext& ctx) {
    static std::map<USHORT, ULONGLONG> firstSeen;  // a headset reports 0% for a moment right after power-on
    static std::map<USHORT, int> silentPolls;      // polls in a row with only empty frames (stuck dongle)
    for (const Model& m : kModels) {
        if (ctx.IsClaimed(kVid, m.pid)) continue;
        auto colls = ctx.Find(kVid, m.pid, [](const HidColl& c) { return c.page() == 0xFF13 && c.usage() == 0x0001 && c.inLen() > 0; });
        if (colls.empty()) {
            firstSeen.erase(m.pid);
            silentPolls.erase(m.pid);
            continue;
        }
        std::wstring product = HidProduct(*colls[0]);
        std::wstring lower = product;
        for (auto& ch : lower) ch = (wchar_t)towlower(ch);
        if (!m.cable && lower.size() >= 6 && lower.compare(lower.size() - 6, 6, L"dongle") == 0) {
            ctx.Claim(kVid, m.pid);  // no headset linked: nothing to read (and the stored value is stale)
            firstSeen.erase(m.pid);
            silentPolls.erase(m.pid);
            continue;
        }
        Reading r = ReadLevel(*colls[0]);
        int& silent = silentPolls[m.pid];
        silent = r.silent ? silent + 1 : 0;
        ULONGLONG now = GetTickCount64();
        if (!firstSeen.count(m.pid)) firstSeen[m.pid] = now;
        DeviceStatus s;
        s.key = L"audeze:maxwell";  // dongle and cable are one headset
        s.name = m.name;
        s.kind = DeviceKind::Headset;
        s.via = m.cable ? L"USB cable" : L"2.4 GHz dongle";
        s.graceSec = 300;
        if (!m.cable && silent >= kStuckPolls) {
            s.note = L"unplug the dongle and plug it back in";  // stuck: only a replug brings the answers back
        } else if (r.level < 0 || (r.level == 0 && now - firstSeen[m.pid] < 90000)) {
            s.asleep = true;  // no reading yet (or the power-on 0%): keep the last value
        } else {
            s.percent = r.level;
            s.charging = m.cable;
        }
        ctx.Add(s, kVid, m.pid);
        for (const Model& o : kModels) ctx.Claim(kVid, o.pid);  // one entry for the headset
        return;
    }
}
