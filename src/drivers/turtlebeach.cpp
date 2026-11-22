// Turtle Beach Stealth 700 Gen 3 headsets (VID 10F5) on the USB transmitter or the headset's own USB cable.
// Ported from HaloBattery (github.com/HeyOkay/HaloBattery, providers/turtlebeach.py, MIT), whose Stealth 700
// Gen 3 path comes from @PaulRichez's reverse engineering of Swarm II (github.com/PaulRichez/
// halobattery-stealth700, docs/protocol.md, MIT; tested on the Stealth 700X Gen 3 transmitter 10F5:2251, and
// the headset's cable 10F5:2253 answers the same request). lykosapps/Neap (FINDINGS.md, GPL-3.0; only its
// findings are used, no code) lists the Stealth 700 PC, PlayStation and Xbox versions in Swarm II's catalogue as
// the same Airoha chip family, with the same control channel.
//   Vendor collection FF13:0001 on interface 3. Host -> device: 62-byte output report 06, `06 <len16 LE> <RACE
//   bytes>`; device -> host: input report 07 framed `07 <len> 00 <payload>`, fetched with GetInputReport.
//   1. RACE 0x9942, enable the chip's CoAP client: 05 5A 02 00 42 99 (what Swarm II sends on every poll).
//   2. RACE 0x9902 carrying a CoAP GET /GSI: 05 5A 0A 00 02 99 40 01 00 01 B3 47 53 49.
//   The answer is a JSON document {"OR":"GSI","KVP":{..., "240":"<percent>", ...}}, split across reports.
// Nothing else is sent, and nothing on the headset is changed. Charging is not reported: key 250 stayed "0"
// with the cable in on the Stealth 700 Gen 3 (HaloBattery #211), so it is left out.
// 10F5:2262 ("Stealth 700PC Gen 3", issue #1413) has the same FF13:0001 62-byte collection on interface 3;
// it is the PC version of the same headset and is read the same way (untested on hardware). The unconfirmed
// transmitter 10F5:2250 and the other models of the family (Stealth 600 Gen 3, Atlas Air, Stealth Pro II,
// which HaloBattery reads with different requests) are not enabled here.
#include "drivers.h"

#include <cwctype>
#include <string>

namespace {

constexpr USHORT kVid = 0x10F5;
struct Model { USHORT pid; bool cable; };
// The transmitters first: the same headset is listed once, through whichever answers first.
const Model kModels[] = { { 0x2251, false },  // Stealth 700X Gen 3 transmitter (tested in HaloBattery)
                          { 0x2262, false },  // Stealth 700PC Gen 3 transmitter (#1413)
                          { 0x2253, true } };  // the headset on its own USB cable (tested in HaloBattery)

bool Send(HidDev& d, std::initializer_list<BYTE> race) {
    std::vector<BYTE> buf(62, 0);
    buf[0] = 0x06;
    buf[1] = (BYTE)race.size();
    buf[2] = 0;
    size_t i = 3;
    for (BYTE b : race) buf[i++] = b;
    return d.Write(buf);
}

void Drain(HidDev& d, int n) {
    std::vector<BYTE> r;
    for (int i = 0; i < n; i++) d.GetInputReport(0x07, r);
}

// The newest "240" (battery percent) of a GSI document in the payload stream, or -1.
int FindLevel(const std::string& s) {
    if (s.find("\"GSI\"") == std::string::npos) return -1;
    int level = -1;
    const std::string key = "\"240\":\"";
    for (size_t p = s.find(key); p != std::string::npos; p = s.find(key, p + 1)) {
        size_t q = p + key.size();
        int v = 0, digits = 0;
        while (q < s.size() && s[q] >= '0' && s[q] <= '9' && digits < 4) v = v * 10 + (s[q++] - '0'), digits++;
        if (digits > 0 && q < s.size() && s[q] == '"' && v <= 100) level = v;
    }
    return level;
}

int ReadLevel(const HidColl& c) {
    HidDev d;
    if (!d.Open(c)) return -1;
    Drain(d, 5);
    if (!Send(d, { 0x05, 0x5A, 0x02, 0x00, 0x42, 0x99 })) return -1;  // enable the CoAP client
    Sleep(300);  // the chip needs a moment after the client is enabled
    Drain(d, 5);
    if (!Send(d, { 0x05, 0x5A, 0x0A, 0x00, 0x02, 0x99, 0x40, 0x01, 0x00, 0x01, 0xB3, 'G', 'S', 'I' })) return -1;
    std::string payload;
    for (int i = 0; i < 30; i++) {
        Sleep(30);
        std::vector<BYTE> r;
        if (!d.GetInputReport(0x07, r) || r.size() < 3 || r[0] != 0x07) continue;
        size_t n = r[1];
        if (n > r.size() - 3) n = r.size() - 3;
        payload.append((const char*)r.data() + 3, n);
        int level = FindLevel(payload);
        if (level >= 0) return level;
    }
    return -1;
}

std::wstring ModelName(const HidColl& c) {
    std::wstring product = HidProduct(c);  // e.g. "Stealth 700X Gen 3"
    if (product.empty()) return L"Turtle Beach Stealth 700 Gen 3";
    std::wstring lower = product;
    for (auto& ch : lower) ch = (wchar_t)towlower(ch);
    if (lower.compare(0, 12, L"turtle beach") != 0) product = L"Turtle Beach " + product;
    return product;
}

}  // namespace

void PollTurtleBeach(PollContext& ctx) {
    const HidColl* found = nullptr;
    const Model* foundModel = nullptr;
    int level = -1;
    for (const Model& m : kModels) {
        if (ctx.IsClaimed(kVid, m.pid)) continue;
        auto colls = ctx.Find(kVid, m.pid, [](const HidColl& c) {
            return c.page() == 0xFF13 && c.usage() == 0x0001 && c.inLen() > 0 && c.outLen() > 0;
        });
        if (colls.empty()) continue;
        if (!found) found = colls[0], foundModel = &m;
        level = ReadLevel(*colls[0]);
        if (level >= 0) {
            found = colls[0];
            foundModel = &m;
            break;
        }
    }
    if (!found) return;
    DeviceStatus s;
    s.key = L"turtlebeach:stealth700";  // transmitter and cable are one headset
    s.name = ModelName(*found);
    s.kind = DeviceKind::Headset;
    s.via = foundModel->cable ? L"USB cable" : L"2.4 GHz transmitter";
    s.graceSec = 300;
    if (level < 0)
        s.asleep = true;  // no record (headset off, or Swarm II holding the channel): keep the last value
    else
        s.percent = level;
    ctx.Add(s, kVid, foundModel->pid);
    for (const Model& o : kModels) ctx.Claim(kVid, o.pid);  // one entry for the headset
}
