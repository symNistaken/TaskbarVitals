// Rongyuan 2.4 GHz receivers, used by Attack Shark, Ajazz and many other mice and keyboards (and
// some MSI devices). One feature report returns both the keyboard and the mouse slot:
//   request [00 F7 00 ...] on interface 2, vendor collection FFFF:0002
//   reply   [00][ok][keyboard %][mouse %][keyboard offline][mouse offline]...
// Receiver list and protocol from github.com/qu1ncky/BatteryTray (RongyuanDongleProvider, MIT),
// itself taken from the official ATTACK SHARK web driver.
// The status report carries no model name, so names come from the receiver's product string with
// the link words stripped ("AJAZZ 2.4G 8K" -> "AJAZZ wireless mouse"). 3151:402D is shared by
// several mice (Attack Shark R3, AJAZZ AJ179 Pro, ...); it is only called "Attack Shark R3" when
// the product string names no other brand.
//
// Mouse model on AJAZZ-vendor receivers (VID 3151). Many brands share these PIDs (3151:5007 alone is used by
// the AJAZZ AJ159 APEX 8K dock, Havit, KEYCEO, StarBlaze and others), so the PID says nothing about the mouse.
// The mouse reports its model as a 32-bit device id, read through the receiver the way the vendor web driver
// (GearHub at qmk.top, device-discovery.js: CommonRyDongle ___getNormalID / ___changeDevice / read) does:
//   [00 F6 05]                     route the following command to the mouse slot (0A = keyboard slot)
//   F7 status until byte 6 = 1     receiver ready to send
//   [00 8F 00 00 00 00 00 00 70]   GET_INFO; byte 8 = FF - sum(bytes 1..7)
//   F7 status until byte 1 = 1     mouse answered
//   [00 FC]                        fetch the answer
//   GET_FEATURE 0                  [00][8F][id, u32 little endian]
// None of these change a setting: F6 only picks which paired device the receiver forwards to, and it is sent
// only while the keyboard slot is empty. Model ids from the same web driver's device table; 1755 was read
// from an AJ159 APEX on its 8K dock (3151:5007), which reported 100% in the status byte the web driver also uses.
#include "drivers.h"

#include <algorithm>
#include <cwctype>
#include <sstream>
#include <map>

namespace {

struct MouseModel {
    UINT id;
    const wchar_t* name;
};

const MouseModel kAjazzMice[] = {
    { 1754, L"AJAZZ AJ159 PRO" },  { 3767, L"AJAZZ AJ159 PRO" },      { 1755, L"AJAZZ AJ159 APEX" },
    { 3346, L"AJAZZ AJ159 APEX" }, { 3806, L"AJAZZ AJ159 APEX" },     { 3407, L"AJAZZ AJ159P MC PRO" },
    { 1851, L"AJAZZ AJ179 PRO" },  { 3766, L"AJAZZ AJ179 PRO" },      { 1852, L"AJAZZ AJ179 APEX" },
    { 3849, L"AJAZZ AJ179 APEX" }, { 3441, L"AJAZZ AJ179P MC PRO" },
};

const wchar_t* AjazzMouseName(UINT id) {
    for (const auto& m : kAjazzMice)
        if (m.id == id) return m.name;
    return nullptr;
}

// Model id per receiver path, read once per connection. A receiver that did not answer is asked again on
// the next polls, at most kIdTries times, so a mouse without this command costs no more than that.
struct MouseIdState {
    UINT id = 0;
    int tries = 0;
};
const int kIdTries = 3;
std::map<std::wstring, MouseIdState> g_mouseIds;

const UINT kReceivers[] = {
    0x0DB0197E, 0x0DB08E9A, 0x145F0376, 0x145F038D, 0x14A5C045, 0x25AA2008, 0x25AA4008, 0x25AA4018, 0x31514011,
    0x31514014, 0x31514017, 0x31514021, 0x31514027, 0x3151402D, 0x31514038, 0x31515006, 0x31515007, 0x31515008,
    0x31515020, 0x31515026, 0x3151502B, 0x31515031, 0x31515037, 0x31515038, 0x3151503D, 0x3151503F, 0x31515040,
    0x31515042, 0x31515045, 0x3151504B, 0x3151504E, 0x31515050, 0x31515052, 0x31515055, 0x3151505A, 0x342DE3E0,
    0x342DE3E1, 0x342DE3E2, 0x342DE3E6, 0x342DE3E7, 0x342DE3E8, 0x342DE3F5, 0x342DE3F6, 0x342DE3F7, 0x342DE3FB,
    0x342DE3FC, 0x342DE3FD, 0x347AA302, 0x347AA305, 0x374AA205, 0x374AA217, 0x374AA220, 0x374AA223, 0x374AA226,
    0x374AA229, 0x374AA234, 0x374AA237, 0x374AA251, 0x374AA263, 0x374AA268, 0x374AA311, 0x374AA314, 0x374AA319,
    0x374AA322, 0x374AA329, 0x374AA355, 0x379A1804, 0x379A1807, 0x38A90002, 0x38EE0000, 0x38EE0002, 0x38EE0039,
    0x391D1A04, 0x391D1A08, 0x391D1A0E, 0x391D1A13, 0x391D1A25, 0x391D1A2B, 0x391D1A34, 0x391D2017, 0x391D2A05,
};

bool IsReceiver(USHORT vid, USHORT pid) {
    UINT key = ((UINT)vid << 16) | pid;
    return std::find(std::begin(kReceivers), std::end(kReceivers), key) != std::end(kReceivers);
}

// Product string without link or device-type words: "AJAZZ 2.4G 8K" -> "AJAZZ", "2.4G Wireless Receiver" -> "",
// "Akko 2.4G Wireless Keyboard" -> "Akko" (the caller appends " wireless keyboard" / " wireless mouse").
std::wstring Brand(const std::wstring& product) {
    std::wistringstream in(product);
    std::wstring word, out;
    while (in >> word) {
        std::wstring u = word;
        for (auto& ch : u) ch = (wchar_t)std::towupper(ch);
        if (u == L"2.4G" || u == L"2.4GHZ" || u == L"8K" || u == L"4K" || u == L"2K" || u == L"1K" || u == L"RECEIVER" ||
            u == L"WIRELESS" || u == L"DONGLE" || u == L"USB" || u == L"KEYBOARD" || u == L"MOUSE")
            continue;
        out += (out.empty() ? L"" : L" ") + word;
    }
    return out;
}

bool QueryStatus(HidDev& d, std::vector<BYTE>& reply) {
    for (int attempt = 0; attempt < 3; attempt++) {
        if (attempt) Sleep(100);
        std::vector<BYTE> cmd(d.coll().featLen(), 0);
        cmd[1] = 0xF7;
        if (!d.SetFeature(cmd)) return false;
        Sleep(20);
        if (d.GetFeature(0, reply) && reply.size() >= 7 && (reply[2] | reply[3] | reply[4] | reply[5] | reply[6]) != 0) return true;
    }
    return false;
}

// Polls the status until `index` reads 1 (6 = ready to send, 1 = answer ready). At most 5 x ~110 ms.
bool WaitStatus(HidDev& d, size_t index) {
    std::vector<BYTE> cmd(d.coll().featLen(), 0), r;
    cmd[1] = 0xF7;
    for (int i = 0; i < 5; i++) {
        Sleep(100);
        if (!d.SetFeature(cmd)) return false;
        Sleep(10);
        if (d.GetFeature(0, r) && r.size() > index && r[index] == 1) return true;
    }
    return false;
}

// Device id of the mouse paired to the receiver (see the header), 0 when it does not answer.
UINT ReadMouseId(HidDev& d) {
    size_t len = d.coll().featLen();
    if (len < 9) return 0;
    std::vector<BYTE> route(len, 0), info(len, 0), fetch(len, 0), r;
    route[1] = 0xF6;
    route[2] = 0x05;
    info[1] = 0x8F;
    info[8] = 0xFF - 0x8F;
    fetch[1] = 0xFC;
    if (!d.SetFeature(route) || !WaitStatus(d, 6) || !d.SetFeature(info) || !WaitStatus(d, 1) || !d.SetFeature(fetch)) return 0;
    Sleep(10);
    if (!d.GetFeature(0, r) || r.size() < 6 || r[1] != 0x8F) return 0;
    UINT id = r[2] | (UINT)r[3] << 8 | (UINT)r[4] << 16 | (UINT)r[5] << 24;
    return id == 0xFFFFFFFF ? 0 : id;
}

}  // namespace

void PollRongyuan(PollContext& ctx) {
    std::vector<std::pair<USHORT, USHORT>> products;
    for (const auto& c : ctx.colls)
        if (IsReceiver(c.vid, c.pid) && !ctx.IsClaimed(c.vid, c.pid) &&
            std::find(products.begin(), products.end(), std::make_pair(c.vid, c.pid)) == products.end())
            products.push_back({ c.vid, c.pid });

    std::map<std::wstring, MouseIdState> seenIds;  // receivers still present, with the mouse online
    for (auto [vid, pid] : products) {
        for (const HidColl* c : ctx.Find(vid, pid, [](const HidColl& c) { return c.iface == 2 && c.page() == 0xFFFF && c.usage() == 0x0002 && c.featLen() >= 7; })) {
            HidDev d;
            if (!d.OpenFeatureOnly(*c)) continue;
            std::vector<BYTE> r;
            if (!QueryStatus(d, r)) continue;
            std::wstring product = HidProduct(*c);
            bool generic = product.empty() || product.find(L"Receiver") != std::wstring::npos || product.find(L"2.4G") != std::wstring::npos;
            const wchar_t* mouseName = nullptr;
            bool mouseIdAsked = false;  // the mouse's own id was asked for (shared dock: the receiver name says little)
            if (vid == 0x3151 && r[5] == 0) {  // AJAZZ-vendor receiver with the mouse online
                MouseIdState st;
                auto known = g_mouseIds.find(c->path);
                if (known != g_mouseIds.end()) st = known->second;
                // Ask only while the keyboard slot is empty, so routing the receiver to the mouse cannot
                // redirect a vendor app that is talking to a paired keyboard.
                if (!st.id && st.tries < kIdTries && r[4] != 0) {
                    st.id = ReadMouseId(d);
                    st.tries++;
                }
                if (st.tries) seenIds[c->path] = st;
                mouseName = AjazzMouseName(st.id);
                mouseIdAsked = st.tries > 0;
            }
            bool any = false;
            // Slot layout: [2] keyboard %, [3] mouse %, [4] keyboard offline, [5] mouse offline.
            for (int slot = 0; slot < 2; slot++) {
                bool keyboard = slot == 0;
                if (r[keyboard ? 4 : 5] != 0) continue;  // nothing connected in this slot
                BYTE level = r[keyboard ? 2 : 3];
                DeviceStatus s;
                s.key = L"rongyuan:" + Hex4(vid) + L":" + Hex4(pid) + (keyboard ? L":kb" : L":ms");
                std::wstring brand = Brand(product), upper = brand;
                for (auto& ch : upper) ch = (wchar_t)std::towupper(ch);
                bool otherBrand = !brand.empty() && upper.find(L"ATTACK SHARK") == std::wstring::npos;
                if (vid == 0x3151 && pid == 0x402D && !keyboard && !otherBrand) s.name = L"Attack Shark R3";
                else if (vid == 0x3151 && pid == 0x5038 && keyboard) s.name = L"MonsGeek keyboard";  // receiver says "MonsGeek 2.4G Wireless Keyboard"
                else if (!keyboard && mouseName) s.name = mouseName;
                // Not for a mouse whose own id was read and is not a known model: docks like 3151:5007 are
                // shared by several brands, so the receiver's brand would often be wrong (PR #382).
                else if (generic && !brand.empty() && (keyboard || !mouseIdAsked))
                    s.name = brand + (keyboard ? L" wireless keyboard" : L" wireless mouse");
                else if (vid == 0x3151 && pid == 0x5006 && keyboard && product.find(L"Akko") != std::wstring::npos)
                    s.name = L"Akko Air 01";
                else if (generic) s.name = keyboard ? L"Wireless keyboard" : L"Wireless mouse";
                else s.name = product;
                s.kind = keyboard ? DeviceKind::Keyboard : DeviceKind::Mouse;
                s.via = L"2.4 GHz receiver";

                // Retain the last battery reading while this keyboard is sleeping: the receiver then sets the
                // keyboard offline flag ([4]), so without a grace period the keyboard vanished a few minutes
                // after the last key press. Hardware-tested: MonsGeek 3151:5038, Akko 5075B Plus V2 3151:4011.
                if (vid == 0x3151 && (pid == 0x5038 || pid == 0x4011) && keyboard) {
                    s.graceSec = 1800;
                }

                if (level >= 1 && level <= 100) s.percent = level;
                ctx.out.push_back(s);
                any = true;
            }
            ctx.Claim(vid, pid);
            (void)any;
            break;
        }
    }
    g_mouseIds = std::move(seenIds);  // a mouse that went offline or a receiver that was unplugged is asked again
}
