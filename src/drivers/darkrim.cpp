// DARKRIM SHADOW receiver (3654:6901), hardware-tested, no vendor software required.
// Source: https://app.darkrim.net/hub/zh.html -> DARKRIM 0.1.26, statically inspected:
// sendGetBasicInfo @ 140124C80, frame builder @ 140118D30 / 140119200.
// Only GET 0010 (Request 01), battery TLV selector 01 00, is sent. The reply
// carries TLV 01: bit 7 charging, bits 0..6 percentage. See docs/darkrim.md.
#include "drivers.h"

namespace {
USHORT Crc(const std::vector<BYTE>& r, size_t count) {
    USHORT crc = 0xFFFF;
    for (size_t i = 0; i < count; ++i) {
        crc ^= (USHORT)r[i] << 8;
        for (int bit = 0; bit < 8; ++bit)
            crc = (USHORT)((crc << 1) ^ ((crc & 0x8000) ? 0x1021 : 0));
    }
    return crc;
}

std::vector<BYTE> BatteryRequest(BYTE seq) {
    // FE seq cmdLE type index count lengthLE payload CRC-BE; then HID report id 0.
    std::vector<BYTE> r = { 0xFE, seq, 0x10, 0, 1, 0, 1, 2, 0, 1, 0 };
    USHORT crc = Crc(r, r.size());
    r.push_back((BYTE)(crc >> 8)); r.push_back((BYTE)crc);
    r.insert(r.begin(), 0);
    return r;
}

bool BatteryReply(const std::vector<BYTE>& r, BYTE seq, int& percent, bool& charging) {
    if (r.size() < 11 || r[0] != 0xFE || r[1] != seq || r[2] != 0x10 || r[3] != 0 ||
        r[4] != 2 || r[5] != 0 || r[6] > 1) return false;
    size_t len = (size_t)r[7] | ((size_t)r[8] << 8);
    if (len > 53 || len + 11 > r.size()) return false;
    size_t end = 9 + len;
    if (Crc(r, end) != (USHORT)((r[end] << 8) | r[end + 1])) return false;
    int value = -1;
    for (size_t i = 9; i < end;) {
        if (end - i < 2) return false;
        BYTE type = r[i++], size = r[i++];
        if (size > end - i) return false;
        if (type == 1) {
            if (size != 1 || value >= 0 || (r[i] & 0x7F) > 100) return false;
            value = r[i];
        }
        i += size;
    }
    if (value < 0) return false;
    percent = value & 0x7F;
    charging = (value & 0x80) != 0;
    return true;
}
}  // namespace

void PollDarkrim(PollContext& ctx) {
    constexpr USHORT vid = 0x3654, pid = 0x6901;
    if (ctx.IsClaimed(vid, pid)) return;
    static BYTE nextSeq = 0;
    for (const HidColl* c : ctx.Find(vid, pid, [](const HidColl& c) {
        return c.iface == 3 && c.page() == 0xFF02 && c.usage() == 2 && c.inLen() == 65 && c.outLen() == 65;
    })) {
        HidDev d;
        if (!d.Open(*c)) continue;
        d.Flush();
        BYTE seq = nextSeq = (BYTE)(nextSeq % 15 + 1);
        if (!d.Write(BatteryRequest(seq), 200)) continue;
        // The receiver first sends several startup notifications; the matching
        // GET response was measured at ~406 ms. Keep one deadline across them.
        const ULONGLONG end = GetTickCount64() + 800;
        for (;;) {
            int left = (int)((LONGLONG)end - (LONGLONG)GetTickCount64());
            std::vector<BYTE> r;
            if (left <= 0 || d.Read(r, left) < 0) break;
            DeviceStatus s;
            if (!BatteryReply(r, seq, s.percent, s.charging)) continue;
            s.key = L"darkrim:SHADOW";
            s.name = L"DARKRIM SHADOW";
            s.kind = DeviceKind::Headset;
            s.via = L"2.4 GHz receiver";
            ctx.Add(s, vid, pid);
            return;
        }
    }
}
