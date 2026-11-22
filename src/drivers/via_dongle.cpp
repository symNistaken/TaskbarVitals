// Keyboards on the shared "Wireless 2.4G Dongle" 36B0:3002, and PMO Hub keyboards on their USB cable.
// The dongle passes the keyboard's raw HID channel through on interface 1, usage page FF60 / usage
// 61, 33-byte input and output reports (report id 0 + 32 bytes). The cable exposes the same
// collection with 65-byte reports. Two firmware families answer on it:
//
// 1. PMO Hub firmware (PMO / WOBKEY Wave 75 RGB, Aurora 80, PMO P75, PMO 80). Frames are
//    [00] AA <cmd> <offset lo> <offset hi> <len> 00 00 ...  and replies echo AA <cmd> <offset>,
//    with 55 (OK) or 0F (error) at payload byte 7. A session is needed around the read:
//      START_COMM   AA 10 00 00 <len>   len = payload - 8 (0x18 on the dongle, 0x38 on the cable)
//                   dongle reply ... 55 00 B0 36 09 30: the paired keyboard is 36B0:3009 (LE)
//      GET_BATTERY  AA 30 00 00 18      reply ... 55 <percent> (without START it answers 0F)
//      STOP_COMM    AA 11 00 00 <len>   reply ... 55
//    The byte after the percent is read as a charging flag by pmohub.cn, but it stayed 00 on a
//    charging Wave 75, so it is not used. When the keyboard is on its cable, the dongle times out.
//    Sources: somebody2506's reverse engineering of pmohub.cn in issue 343, tested on a PMO Wave 75
//    RGB on the dongle and on the cable (78%, same as the Bluetooth reading), and pmohub.cn's own
//    web driver (protocol 1: REPORT_ID 170, CMD_START_COMM 16, CMD_STOP_COMM 17,
//    CMD_GET_BATTERY_STATUS 48 read with length 24, battery at reply byte 8, SUCCESS_FLAG 85,
//    FAILURE_FLAG 15; its device list has the cable PIDs 3009 WAVE 75, 3004 Aurora 80, 302B PMO P75,
//    3012 PMO 80, all protocol 1 on FF60:0061, with 3002 as their 2.4G dongle).
//    Untested on the Aurora 80, PMO P75 and PMO 80.
//
// 2. VIA firmware (URX Core75 "URX Core75 Wireless", EVO80 "EVO80 2.4G", issue 1406). The battery is
//    VIA custom value 7 on channel 0, read with VIA's read-only custom-get command 0x08:
//      request [00] 08 00 07 00 ...       report id 0, custom get, channel 0, value 7
//      reply        08 00 07 <level> ...  echoed header, level in percent (no charging state)
//    A firmware without that value answers FF (VIA "unhandled"), which is ignored.
//    Source: HaloBattery (github.com/HeyOkay/HaloBattery, providers/urx.py and docs/protocols.md,
//    "URX Core75 (2.4 GHz)"), taken from URX HUB's web driver and confirmed on a real Core75 on this
//    dongle (HaloBattery issue 245). Untested on the EVO80.
//
// Which firmware is behind the dongle is not visible from its descriptors (both report the generic
// "Wireless 2.4G Dongle" or similar), so the dongle tries the likely protocol first (VIA when the
// product string names a URX board, PMO Hub otherwise), then the other, and from then on only asks
// the one that answered. A dongle that answers neither is retried with a growing back-off instead of
// on every poll. Both protocols only read; each firmware ignores the other's frames (VIA answers
// FF "unhandled", the PMO firmware does not answer).
#include "drivers.h"

#include <algorithm>
#include <cwchar>
#include <map>

namespace {

constexpr USHORT kVid = 0x36B0;
constexpr USHORT kDongle = 0x3002;

struct PmoBoard {
    USHORT pid;  // the keyboard's own (cable) product id; also what the dongle reports as paired
    const wchar_t* name;
};
// pmohub.cn's device list (protocol 1, usage page FF60 / usage 61, dongle 36B0:3002).
const PmoBoard kPmoBoards[] = {
    { 0x3009, L"WAVE 75" },
    { 0x3004, L"Aurora 80" },
    { 0x302B, L"PMO P75" },
    { 0x3012, L"PMO 80" },
};

const wchar_t* PmoName(USHORT pid) {
    for (const auto& b : kPmoBoards)
        if (b.pid == pid) return b.name;
    return nullptr;
}

bool IsRawHid(const HidColl& c) {
    return c.iface == 1 && c.page() == 0xFF60 && c.usage() == 0x0061 && c.inLen() == c.outLen() &&
           (c.outLen() == 33 || c.outLen() == 65);
}

// Sends one request and waits for the reply that starts with `echo`. Returns the reply payload
// (report id stripped), or an empty vector on timeout.
std::vector<BYTE> Ask(HidDev& d, const std::vector<BYTE>& req, std::initializer_list<BYTE> echo, int ms) {
    d.Flush();
    if (!d.Write(req)) return {};
    ULONGLONG end = GetTickCount64() + ms;
    for (;;) {
        int left = (int)((LONGLONG)end - (LONGLONG)GetTickCount64());
        if (left <= 0) return {};
        std::vector<BYTE> r;
        int n = d.Read(r, left);  // report id 0 is stripped: r[0] is the first payload byte
        if (n < 0) return {};
        if (n < (int)echo.size() + 6 || !std::equal(echo.begin(), echo.end(), r.begin())) continue;  // e.g. a status push
        r.resize(n);
        return r;
    }
}

// --- PMO Hub ---------------------------------------------------------------------------------

std::vector<BYTE> PmoFrame(size_t payload, BYTE cmd, BYTE len) {
    std::vector<BYTE> f(1 + payload, 0);  // [0] = report id 0
    f[1] = 0xAA;
    f[2] = cmd;
    f[3] = 0x00;  // offset lo
    f[4] = 0x00;  // offset hi
    f[5] = len;
    return f;
}

bool PmoOk(const std::vector<BYTE>& r) { return r.size() > 8 && r[7] == 0x55; }

// Returns the percent (0..100) or -1. `paired` gets the keyboard pid from the START reply (dongle).
int PmoQuery(HidDev& d, USHORT* paired) {
    const size_t payload = d.coll().outLen() - 1;  // 32 or 64
    const BYTE len = (BYTE)(payload - 8);          // 0x18 or 0x38, as pmohub.cn uses
    std::vector<BYTE> start = Ask(d, PmoFrame(payload, 0x10, len), { 0xAA, 0x10, 0x00, 0x00 }, 600);
    if (start.empty()) start = Ask(d, PmoFrame(payload, 0x10, len), { 0xAA, 0x10, 0x00, 0x00 }, 600);  // link waking up
    if (!PmoOk(start)) {
        // Close the session anyway in case the START was taken; don't wait when nothing answers.
        if (start.empty()) d.Write(PmoFrame(payload, 0x11, len));
        else Ask(d, PmoFrame(payload, 0x11, len), { 0xAA, 0x11, 0x00, 0x00 }, 300);
        return -1;
    }
    if (paired && start.size() > 12 && (start[9] | start[10] << 8) == kVid) *paired = (USHORT)(start[11] | start[12] << 8);

    std::vector<BYTE> bat = Ask(d, PmoFrame(payload, 0x30, 0x18), { 0xAA, 0x30, 0x00, 0x00 }, 600);
    int level = (PmoOk(bat) && bat[8] <= 100) ? bat[8] : -1;

    Ask(d, PmoFrame(payload, 0x11, len), { 0xAA, 0x11, 0x00, 0x00 }, 600);  // always end the session
    return level;
}

// --- VIA -------------------------------------------------------------------------------------

// Returns the level (1..100), or -1 when the keyboard did not answer with one.
int ViaQuery(HidDev& d) {
    std::vector<BYTE> req(d.coll().outLen(), 0);  // [0] = report id 0
    req[1] = 0x08;                                // VIA id_custom_get_value
    req[2] = 0x00;                                // channel 0 (keyboard-specific values)
    req[3] = 0x07;                                // value 7: battery level
    for (int attempt = 0; attempt < 2; ++attempt) {  // the first request can be missed while the link wakes up
        std::vector<BYTE> r = Ask(d, req, { 0x08, 0x00, 0x07 }, 600);
        if (!r.empty()) return (r[3] >= 1 && r[3] <= 100) ? r[3] : -1;
    }
    return -1;
}

// "EVO80 2.4G" -> "EVO80", "URX Core75 Wireless" -> "URX Core75"; the generic dongle name -> fallback.
std::wstring ViaName(const HidColl& c) {
    std::wstring name = HidProduct(c);
    if (name.find(L"Dongle") != std::wstring::npos) return L"Wireless keyboard";
    for (const wchar_t* suffix : { L" 2.4G", L" Wireless" }) {
        size_t n = wcslen(suffix);
        if (name.size() > n && name.compare(name.size() - n, n, suffix) == 0) name.resize(name.size() - n);
    }
    return name.empty() ? L"Wireless keyboard" : name;
}

// --- per-product state across polls ---------------------------------------------------------

enum class Proto { Unknown, Pmo, Via };

struct State {
    Proto proto = Proto::Unknown;
    int misses = 0;         // consecutive polls without an answer
    ULONGLONG nextTry = 0;  // skip polling until then
    USHORT paired = 0;      // PMO keyboard pid seen behind the dongle
};
std::map<USHORT, State> g_state;

void Missed(State& st) {
    ++st.misses;
    // A known protocol that stops answering most likely means a sleeping keyboard (or one moved to
    // its cable): retry soon. After several misses, forget it in case another keyboard was paired.
    if (st.misses >= 6) st.proto = Proto::Unknown;
    int sec = st.proto != Proto::Unknown ? 20 : std::min(300, 30 << std::min(st.misses - 1, 4));
    st.nextTry = GetTickCount64() + sec * 1000ULL;
}

void Answered(State& st, Proto p) {
    st.proto = p;
    st.misses = 0;
    st.nextTry = 0;
}

DeviceStatus Make(USHORT keyPid, const std::wstring& name, const wchar_t* via, int level) {
    DeviceStatus s;
    s.key = L"via:" + Hex4(kVid) + L":" + Hex4(keyPid);  // dongle and cable share the keyboard's pid
    s.name = name;
    s.kind = DeviceKind::Keyboard;
    s.via = via;
    s.percent = level;
    s.graceSec = 1800;  // keep the last value if a sleeping keyboard stops answering
    return s;
}

// PMO Hub keyboard on its USB cable. Returns true when one answered.
bool PollCable(PollContext& ctx, USHORT pid) {
    if (ctx.IsClaimed(kVid, pid)) return false;
    auto colls = ctx.Find(kVid, pid, IsRawHid);
    if (colls.empty()) {
        g_state.erase(pid);  // unplugged: start fresh when it comes back
        return false;
    }
    State& st = g_state[pid];
    if (GetTickCount64() < st.nextTry) return false;
    for (const HidColl* c : colls) {
        HidDev d;
        if (!d.Open(*c)) continue;
        int level = PmoQuery(d, nullptr);
        if (level < 0) continue;
        Answered(st, Proto::Pmo);
        ctx.Add(Make(pid, PmoName(pid), L"USB cable", level), kVid, pid);
        return true;
    }
    Missed(st);
    return false;
}

void PollDongle(PollContext& ctx, bool cableAnswered) {
    if (ctx.IsClaimed(kVid, kDongle)) return;
    auto colls = ctx.Find(kVid, kDongle, [](const HidColl& c) { return IsRawHid(c) && c.outLen() == 33; });
    if (colls.empty()) {
        g_state.erase(kDongle);
        return;
    }
    State& st = g_state[kDongle];
    // A PMO keyboard on its cable leaves the dongle unanswered (START_COMM times out); skip it.
    if (cableAnswered && st.proto != Proto::Via) {
        ctx.Claim(kVid, kDongle);
        return;
    }
    if (GetTickCount64() < st.nextTry) return;
    const HidColl& c = *colls.front();
    HidDev d;
    if (!d.Open(c)) return;
    Proto order[2] = { Proto::Pmo, Proto::Via };
    if (st.proto == Proto::Via || (st.proto == Proto::Unknown && HidProduct(c).find(L"URX") != std::wstring::npos))
        std::swap(order[0], order[1]);
    int tries = st.proto == Proto::Unknown ? 2 : 1;  // once known, only ask the protocol that answered
    for (int i = 0; i < tries; ++i) {
        if (order[i] == Proto::Pmo) {
            USHORT paired = 0;
            int level = PmoQuery(d, &paired);
            if (level < 0) continue;
            if (paired) st.paired = paired;
            Answered(st, Proto::Pmo);
            USHORT keyPid = st.paired ? st.paired : kDongle;
            const wchar_t* name = PmoName(keyPid);
            ctx.Add(Make(keyPid, name ? name : L"Wireless keyboard", L"2.4 GHz receiver", level), kVid, kDongle);
            return;
        }
        int level = ViaQuery(d);
        if (level < 0) continue;
        Answered(st, Proto::Via);
        ctx.Add(Make(kDongle, ViaName(c), L"2.4 GHz receiver", level), kVid, kDongle);
        return;
    }
    Missed(st);
}

}  // namespace

void PollViaDongle(PollContext& ctx) {
    bool cable = false;
    for (const auto& b : kPmoBoards) cable |= PollCable(ctx, b.pid);
    PollDongle(ctx, cable);
}
