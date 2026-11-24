// Wobkey Rainy75 V2 keyboard on its 2.4 GHz receiver (36B0:3071) or USB cable (36B0:3075).
// Battery telemetry is pushed passively: after the 0x93 "enable" command the keyboard broadcasts a
// 0xBB report about every 5 s carrying the percent and charge state. No further query is needed.
//
// Framing (unnumbered 256-byte report, report id 0, on the FF60:0061 vendor collection):
//   BE <cmd> <len> <payload...> ED, zero-padded to the report length
//   enable (TX):    BE 93 05 01 ED
//   telemetry (RX): BE BB 06 <percent> <charge> ED   (percent = byte 3; charge: 0 = not charging)
//
// Protocol captured from WOBHub (https://wob.mkbms.cn/), Wobkey's WebHID configurator, which logs the
// full TX/RX traffic of its "Rainy75 V2 battery query" tool.
#include "drivers.h"

#include <algorithm>
#include <map>
#include <memory>

namespace {

constexpr USHORT kVid = 0x36B0;
constexpr USHORT kReceiver = 0x3071;
constexpr USHORT kCable = 0x3075;
constexpr USHORT kPage = 0xFF60;
constexpr USHORT kUsage = 0x0061;
constexpr int kPollBudgetMs = 4000;
constexpr int kBufferedReadMs = 30;

// Keep handles open between polls (like PollRapoo) so the OS buffers passive 0xBB reports arriving
// during the 60 s refresh interval instead of missing them.
std::map<std::wstring, std::unique_ptr<HidDev>> g_wobkey;

bool IsTelemetry(const std::vector<BYTE>& r) {
    return r.size() >= 6 && r[0] == 0xBE && r[1] == 0xBB && r[2] == 0x06;
}

int RemainingMs(ULONGLONG deadline) {
    const auto now = GetTickCount64();
    return now < deadline ? static_cast<int>(deadline - now) : 0;
}

// Keep the newest queued report. If the first push (~5 s apart) misses the
// deadline, the retained handle lets the next poll pick up the queued report.
bool ReadBattery(HidDev& d, ULONGLONG deadline, int& percent, bool& charging) {
    auto decode = [&](const std::vector<BYTE>& r) {
        if (!IsTelemetry(r)) return false;
        percent = Clamp100(r[3]);
        charging = r[4] != 0;
        return true;
    };
    bool got = false;
    for (int i = 0; i < 64; i++) {
        const int left = RemainingMs(deadline);
        if (!left) return got;
        std::vector<BYTE> r;
        if (d.Read(r, std::min(kBufferedReadMs, left)) < 0) break;
        if (decode(r)) got = true;
    }
    if (got) return true;
    while (const int left = RemainingMs(deadline)) {
        std::vector<BYTE> r;
        if (d.Read(r, left) < 0) return false;
        if (decode(r)) return true;
    }
    return false;
}

}  // namespace

void PollWobkey(PollContext& ctx) {
    // Always drop disconnected handles before a successful poll can return.
    for (auto it = g_wobkey.begin(); it != g_wobkey.end();) {
        const bool present = std::any_of(ctx.colls.begin(), ctx.colls.end(), [&](const HidColl& c) {
            return c.path == it->first;
        });
        it = present ? std::next(it) : g_wobkey.erase(it);
    }

    // The cable wins when both are connected (it powers the keyboard and stays awake).
    for (USHORT pid : { kCable, kReceiver }) {
        if (ctx.IsClaimed(kVid, pid)) continue;
        for (const HidColl* c : ctx.Find(kVid, pid, [](const HidColl& c) {
                 return c.page() == kPage && c.usage() == kUsage && c.inLen() >= 256 && c.outLen() >= 256;
             })) {
            const ULONGLONG deadline = GetTickCount64() + kPollBudgetMs;
            auto slot = g_wobkey.find(c->path);
            if (slot == g_wobkey.end()) {
                auto device = std::make_unique<HidDev>();
                if (!device->Open(*c)) continue;
                const int left = RemainingMs(deadline);
                if (!left || !device->Write({ 0x00, 0xBE, 0x93, 0x05, 0x01, 0xED }, std::min(500, left)))
                    continue;
                slot = g_wobkey.emplace(c->path, std::move(device)).first;
            }
            int percent = -1;
            bool charging = false;
            if (!ReadBattery(*slot->second, deadline, percent, charging)) continue;  // asleep/off: keep the last value

            DeviceStatus s;
            s.key = L"wobkey:rainy75v2";
            s.name = L"Rainy75 V2";
            s.kind = DeviceKind::Keyboard;
            s.via = (pid == kCable) ? L"USB cable" : L"2.4 GHz receiver";
            s.percent = percent;
            s.charging = charging;
            s.graceSec = 1800;  // keep the last value, dimmed, while the keyboard sleeps
            ctx.Add(s, kVid, pid);
            if (pid == kCable) ctx.Claim(kVid, kReceiver);
            return;
        }
    }
}
