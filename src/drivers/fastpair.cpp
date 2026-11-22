// Google Fast Pair battery broadcasts: left, right and case levels of earbuds over Bluetooth.
//
// Fast Pair earbuds (Jabra Elite, Pixel Buds, many Sony / JBL / Bose models ...) put their battery in
// the non-discoverable Fast Pair advertisement, in clear, for Android's battery popup. Windows only
// reads one level (the standard Battery Service), so the Bluetooth fallback uses this for the split.
// Service data of 16-bit UUID 0xFE2C, after the UUID:
//   [0] version/flags (0x00), then fields with a header byte 0bLLLLTTTT (L = length, T = type):
//   T 0 / 2  account key filter (shown / hidden UI)     T 1  salt
//   T 3 / 4  battery (shown / hidden UI): one byte per component, in order left, right, case;
//            bit 7 = charging, bits 0-6 = level 0-100, 0x7F = unknown
//   T 5      remaining battery time
// Discoverable-mode advertisements carry only the 3-byte model id (no version byte) and are ignored.
// Layout from the Fast Pair specification (developers.google.com/nearby/fast-pair/specifications/
// extensions/batterynotification); verified on a Jabra Elite 8 Active against the Sound+ app.
//
// Messages are matched to the paired device by Bluetooth address: the address must be the public
// one Windows paired with (devices broadcasting from a rotating random address are not matched).
#include "drivers.h"
#include "ble_adverts.h"

#include <map>

namespace {

SRWLOCK g_lock = SRWLOCK_INIT;
std::map<UINT64, FastPairBattery> g_battery;  // by Bluetooth address

int Component(BYTE b, bool& charging) {
    int level = b & 0x7F;
    charging = level <= 100 && (b & 0x80) != 0;
    return level <= 100 ? level : -1;
}

bool Parse(const BYTE* d, UINT32 n, FastPairBattery& out) {
    if (n < 2 || d[0] != 0x00) return false;  // version 0; also skips the 3-byte discoverable model id
    for (UINT32 i = 1; i < n;) {
        UINT32 len = d[i] >> 4, type = d[i] & 0x0F;
        if (i + 1 + len > n) return false;
        if ((type == 3 || type == 4) && len >= 1) {
            out.left = Component(d[i + 1], out.leftChg);
            if (len >= 2) out.right = Component(d[i + 2], out.rightChg);
            if (len >= 3) out.box = Component(d[i + 3], out.boxChg);
            return out.left >= 0 || out.right >= 0 || out.box >= 0;
        }
        i += 1 + len;
    }
    return false;
}

}  // namespace

void FastPairOnAdvert(UINT64 addr, const BYTE* data, UINT32 len) {
    FastPairBattery b;
    if (!Parse(data, len, b)) return;
    b.tick = GetTickCount64();
    AcquireSRWLockExclusive(&g_lock);
    g_battery[addr] = b;
    ReleaseSRWLockExclusive(&g_lock);
}

bool FastPairLookup(UINT64 addr, FastPairBattery& out) {
    ULONGLONG now = GetTickCount64();
    AcquireSRWLockShared(&g_lock);
    auto it = g_battery.find(addr);
    bool ok = it != g_battery.end() && now - it->second.tick < 5 * 60 * 1000;
    if (ok) out = it->second;
    ReleaseSRWLockShared(&g_lock);
    return ok;
}
