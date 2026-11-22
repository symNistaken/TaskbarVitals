// Scyrox V12M on its 2.4 GHz receiver 395A:0006 (issue #1380). Only this exact id is handled: the
// Scyrox V6S / V8S on the same vendor id (receiver 000E, cable 000B / 000C) answer a different status
// command (2E 00 03), and the older V6 / V8 sit on Compx's 3554.
// Read-only profile read from Scyrox's S-Center 2.0 web driver, on the vendor collection FF00:0002 of
// interface 2 (64-byte input and output reports):
//   Output report 08: [08][01][00 ...][checksum], checksum = low byte of 08 + 01 = 09 in the last byte.
//   The receiver answers with a stream of 64-byte input reports 08 (one 08 01 frame per profile plus axis
//   frames 23 / 25, ended by an 08 01 frame with [3] = 0). The frame of the active profile has
//   [1] = 01, [3] != 0, [4] = active profile = [5] (profile in this frame, 0..3), [7] = DPI stage count 1..5,
//   [62] = 0 (1 = error), and the battery percent at [20] (after five 16-bit DPI values and the polling rate).
//   The protocol has no charging field that the sources confirm, so charging is not shown.
// From late-autumn-gif/mousedock (MIT): ReferenceBatteryProtocol.cs / ScyroxV12Dpi.cs, profile 395A:0006
// FF00:0002 with tests in ReferenceBatteryRegression.cs; the same frame and checks in MoFeng2223/BrimDeck
// (src/BrimDeck.Core/Brands/Scyrox.cs). Nothing here writes settings: command 01 only reads the profiles.
#include "drivers.h"

namespace {

const USHORT kVid = 0x395A;
const USHORT kV12mReceiver = 0x0006;

// Returns the battery percent from the active profile's frame, or -1 when there is no valid reply.
int ReadBattery(HidDev& d) {
    std::vector<BYTE> req(64, 0);
    req[0] = 0x08;
    req[1] = 0x01;   // read profiles (current configuration)
    req[63] = 0x09;  // low byte of 08 + 01
    d.Flush();
    if (!d.Write(req)) return -1;
    ULONGLONG end = GetTickCount64() + 1500;
    std::vector<BYTE> r;
    int pct = -1;
    for (;;) {
        int left = (int)((LONGLONG)end - (LONGLONG)GetTickCount64());
        if (left <= 0) break;
        if (d.Read(r, left) < 0) break;
        if (r.size() != 64 || r[0] != 0x08 || r[1] != 0x01) continue;  // axis frames, other replies
        if (r[3] == 0) break;  // end of the stream (or our own request echoed back)
        if (r[62] == 1 || r[4] > 3 || r[4] != r[5] || r[7] < 1 || r[7] > 5) continue;  // not the active profile
        if (r[20] > 100) break;
        pct = r[20];
        break;
    }
    d.Flush();  // drop the rest of the profile stream
    return pct;
}

}  // namespace

void PollScyrox(PollContext& ctx) {
    if (ctx.IsClaimed(kVid, kV12mReceiver)) return;
    for (const HidColl* c : ctx.Find(kVid, kV12mReceiver, [](const HidColl& c) {
             return c.page() == 0xFF00 && c.usage() == 0x0002 && c.outLen() == 64 && c.inLen() == 64;
         })) {
        HidDev d;
        if (!d.Open(*c)) continue;
        int pct = ReadBattery(d);
        if (pct < 0) continue;  // mouse asleep / off, or no valid reply
        DeviceStatus s;
        s.key = L"scyrox:" + Hex4(kV12mReceiver);
        s.name = L"Scyrox V12M";
        s.kind = DeviceKind::Mouse;
        s.via = L"2.4 GHz receiver";
        s.graceSec = 1800;  // keep the last value while the mouse sleeps
        s.percent = pct;
        ctx.Add(s, kVid, kV12mReceiver);
        break;
    }
}
