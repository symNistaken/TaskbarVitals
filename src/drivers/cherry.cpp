// CHERRY MX 3.0S Wireless on its 2.4 GHz dongle (USB 046A:00EA, "MX 3.0S Dongle") or on the USB
// cable (046A:00E3, "MX 3.0S Wireless Keyboard"). Over Bluetooth Windows' own level is used instead.
//
// Both expose a 64-byte vendor channel on usage page FF1C, usage 0092 (a second FF1C:0092
// collection with 9-byte reports is not used). The framing and the battery command come from the
// community tool B1Chat/BatteryWatch (MIT), which verified them on the CHERRY PIXIU 75 receiver
// (046A:01E4); the CHERRY MX 3.0S Wireless uses the same usage page, usage and report size.
//
//   frame:  [0] report ID 4, [1..2] checksum (LE) = sum(bytes[3..end]) & 0xFFFF, [3] type, [4..] payload
//   query:  type 0x1A, payload 03 00 00 55 (read-only status request)
//   reply:  [0] 4, [3] 0x1A, [8] battery percent, [9] non-zero while USB powered
//
// Verified on a real MX 3.0S Wireless through the dongle. The cable (00E3) uses the same channel layout
// and is assumed to answer the same query. The matching and the reply checks below are deliberately
// strict, so a device that answers differently is skipped rather than misreported.
#include "drivers.h"

namespace {

constexpr USHORT kVid = 0x046A;  // CHERRY
constexpr USHORT kPage = 0xFF1C;
constexpr USHORT kUsage = 0x0092;
constexpr size_t kLen = 64;  // report length including the report ID
constexpr BYTE kReportId = 0x04;
constexpr BYTE kStatusType = 0x1A;

struct Model {
    USHORT pid;
    const wchar_t* via;
};
// Cable first: when it is plugged in the keyboard is powered and awake, and the dongle may still be
// attached too. Only one entry is ever reported for the keyboard.
constexpr Model kModels[] = { { 0x00E3, L"USB cable" }, { 0x00EA, L"2.4 GHz receiver" } };

// The one and only frame this driver ever sends.
std::vector<BYTE> BuildQuery() {
    std::vector<BYTE> f(kLen, 0);
    f[0] = kReportId;
    f[3] = kStatusType;
    f[4] = 0x03;
    f[7] = 0x55;
    unsigned sum = 0;
    for (size_t i = 3; i < f.size(); ++i) sum += f[i];
    f[1] = (BYTE)(sum & 0xFF);
    f[2] = (BYTE)((sum >> 8) & 0xFF);
    return f;
}

bool IsStatusReply(const std::vector<BYTE>& r) { return r.size() >= 10 && r[0] == kReportId && r[3] == kStatusType; }

}  // namespace

// Sends the status query on one collection. Returns false when nothing usable answered.
static bool Query(const HidColl& c, std::vector<BYTE>& reply) {
    HidDev d;
    if (!d.Open(c)) return false;
    d.Flush();
    const std::vector<BYTE> query = BuildQuery();

    // The dongle only answers while the keyboard is awake: two tries, well under a second in all.
    for (int attempt = 0; attempt < 2; ++attempt) {
        if (!d.Write(query, 300)) return false;
        const ULONGLONG end = GetTickCount64() + 400;
        for (;;) {
            const LONGLONG left = (LONGLONG)end - (LONGLONG)GetTickCount64();
            if (left <= 0) break;
            std::vector<BYTE> r;
            if (d.Read(r, (int)left) < 0) break;
            if (IsStatusReply(r)) {
                reply = std::move(r);
                return true;
            }
        }
    }
    return false;
}

static void PollCherryXtrfy(PollContext& ctx);  // Cherry XTRFY 1K Mouse Receiver, below

static void PollCherryKeyboard(PollContext& ctx) {
    for (const Model& m : kModels) {
        if (ctx.IsClaimed(kVid, m.pid)) continue;
        for (const HidColl* c : ctx.Find(kVid, m.pid, [](const HidColl& c) {
                 return c.page() == kPage && c.usage() == kUsage && c.inLen() == kLen && c.outLen() == kLen;
             })) {
            std::vector<BYTE> reply;
            if (!Query(*c, reply)) continue;  // keyboard off or asleep: show nothing (last value is kept for a while)

            DeviceStatus s;
            s.key = L"cherry:mx30s";  // same entry whichever way the keyboard is connected
            s.name = L"CHERRY MX 3.0S Wireless";
            s.kind = DeviceKind::Keyboard;
            s.via = m.via;
            if (reply[8] <= 100) {
                s.percent = reply[8];
                s.charging = reply[9] != 0;  // USB powered, i.e. plugged in
            } else {
                s.asleep = true;  // answered without a usable level
            }
            s.graceSec = 1800;  // the dongle stops answering while the keyboard sleeps
            ctx.Add(s, kVid, m.pid);
            return;
        }
    }
}

void PollCherry(PollContext& ctx) {
    PollCherryKeyboard(ctx);
    PollCherryXtrfy(ctx);
}

// ---------------------------------------------------------------------------
// Cherry XTRFY 1K Mouse Receiver (046A:0330) and the mouse paired to it.
//
// The receiver exposes a 17-byte vendor channel on usage page FF02, usage 0002 (in 17 / out 17).
// It speaks the shared CompX framing also used by the Corsair Sabre V2 Pro (1B1C:2B2A, corsair.cpp)
// and the Areson receivers (areson.cpp): 17-byte frames, report ID 08, checksum = 0x55 - sum of
// the first 16 bytes. Two read-only commands are sent, nothing else:
//   query:  [08][cmd][00 ...][checksum]
//     03 = link status -> [6] non-zero while the mouse is connected
//     04 = GetPower    -> [6] percent, [7] charging, [8..9] millivolts (big endian)
//   reply:  arrives on the same collection.
// Captured live from a Cherry XTRFY 1K Mouse Receiver with the mouse at 95% / 4084 mV:
//   08 04 00 00 00 02 5f 00 0f f4 00 00 00 00 00 00 e5
namespace {

constexpr USHORT kXtrfyPid = 0x0330;
constexpr size_t kXtrfyLen = 17;
constexpr BYTE kXtrfyReportId = 0x08;
constexpr BYTE kXtrfyLink = 0x03;
constexpr BYTE kXtrfyPower = 0x04;

std::vector<BYTE> XtrfyQuery(BYTE cmd) {
    std::vector<BYTE> f(kXtrfyLen, 0);
    f[0] = kXtrfyReportId;
    f[1] = cmd;
    unsigned sum = 0;
    for (size_t i = 0; i + 1 < f.size(); ++i) sum += f[i];
    f[kXtrfyLen - 1] = (BYTE)(0x55 - sum);
    return f;
}

bool XtrfyChecksumOk(const std::vector<BYTE>& r) {
    if (r.size() != kXtrfyLen) return false;
    unsigned sum = 0;
    for (size_t i = 0; i + 1 < r.size(); ++i) sum += r[i];
    return (BYTE)(0x55 - sum) == r[kXtrfyLen - 1];
}

// One command/reply exchange on the FF02:0002 collection. Fills `r` when a well-formed reply to
// exactly this command arrived.
bool XtrfyExchange(HidDev& d, BYTE cmd, std::vector<BYTE>& r) {
    d.Flush();
    if (!d.Write(XtrfyQuery(cmd), 300)) return false;
    const ULONGLONG end = GetTickCount64() + 500;
    for (;;) {
        const LONGLONG left = (LONGLONG)end - (LONGLONG)GetTickCount64();
        if (left <= 0) return false;
        std::vector<BYTE> frame;
        if (d.Read(frame, (int)left) < 0) return false;
        if (frame.size() == kXtrfyLen && frame[0] == kXtrfyReportId && frame[1] == cmd && XtrfyChecksumOk(frame)) {
            r = std::move(frame);
            return true;
        }
    }
}

}  // namespace

static void PollCherryXtrfy(PollContext& ctx) {
    if (ctx.IsClaimed(kVid, kXtrfyPid)) return;
    for (const HidColl* c : ctx.Find(kVid, kXtrfyPid, [](const HidColl& c) {
             return c.page() == 0xFF02 && c.usage() == 0x0002 && c.inLen() == kXtrfyLen && c.outLen() == kXtrfyLen;
         })) {
        HidDev d;
        if (!d.Open(*c)) return;
        DeviceStatus s;
        std::vector<BYTE> r;
        bool answered = false;
        // Check whether the mouse is switched on and linked before asking it for power.
        if (XtrfyExchange(d, kXtrfyLink, r)) {
            answered = true;
            s.connected = r[6] != 0;
        }
        if ((!answered || s.connected) && XtrfyExchange(d, kXtrfyPower, r)) {
            answered = true;
            s.connected = true;
            if (r[6] <= 100) s.percent = r[6];
            s.charging = r[7] != 0;
            s.millivolts = (r[8] << 8) | r[9];
        } else if (answered && s.connected) {
            s.asleep = true;  // linked, but no usable power reply
        }
        if (!answered) return;  // silent: mouse switched off or asleep; the last value is kept
        if (!s.connected) { ctx.Claim(kVid, kXtrfyPid); return; }
        s.key = L"cherry:xtrfy";
        s.name = L"Cherry XTRFY wireless mouse";
        s.kind = DeviceKind::Mouse;
        s.via = L"2.4 GHz receiver";
        s.graceSec = 1800;  // existing UI keeps the last reading, dimmed, while the mouse sleeps
        ctx.Add(s, kVid, kXtrfyPid);
        return;
    }
}
