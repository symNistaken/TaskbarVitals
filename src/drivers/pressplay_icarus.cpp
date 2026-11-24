// Press Play ICARUS V2 Nearlink (7768:6434) and AJAZZ AJ159NL P NearLink (7768:6425).
// ICARUS uses its own documented 06 05 / 06 05 26 battery reply.
// AJAZZ uses the receiver's 06 FA status input report: byte 6 is percent and byte 7 is
// the charging flag. Its setup exchange mirrors the official AJAZZ app's read-only
// output reports captured on HID interface 1 (FFB2:0001).
#include "drivers.h"

#include <map>
#include <memory>

namespace {

constexpr USHORT kVid = 0x7768;
constexpr USHORT kIcarusPid = 0x6434;
constexpr USHORT kAjazzPid = 0x6425;
constexpr USHORT kUsagePage = 0xFFB2;
constexpr USHORT kUsage = 0x0001;
constexpr size_t kReportLength = 64;
constexpr BYTE kReportId = 0x06;

// Press Play ICARUS V2 protocol.
constexpr BYTE kIcarusBatteryCommand = 0x05;
constexpr BYTE kIcarusReplyMarker = 0x26;
constexpr size_t kIcarusPercentOffset = 13;

bool QueryIcarusBattery(const HidColl& c, int& percent) {
    HidDev d;
    if (!d.Open(c)) return false;
    d.Flush();

    std::vector<BYTE> request(c.outLen(), 0), reply;
    request[0] = kReportId;
    request[1] = kIcarusBatteryCommand;
    int n = d.SetOutputAndRead(request, reply, 500);
    if (n < (int)(kIcarusPercentOffset + 1) || reply.size() < kIcarusPercentOffset + 1 ||
        reply[0] != kReportId || reply[1] != kIcarusBatteryCommand ||
        reply[2] != kIcarusReplyMarker || reply[kIcarusPercentOffset] > 100)
        return false;

    percent = reply[kIcarusPercentOffset];
    return true;
}

void PollIcarus(PollContext& ctx) {
    if (ctx.IsClaimed(kVid, kIcarusPid)) return;
    for (const HidColl* c : ctx.Find(kVid, kIcarusPid, [](const HidColl& c) {
             return c.page() == kUsagePage && c.usage() == kUsage && c.inLen() == kReportLength &&
                    c.outLen() == kReportLength;
         })) {
        int percent = -1;
        if (!QueryIcarusBattery(*c, percent)) continue;

        DeviceStatus s;
        s.key = L"pressplay:icarus-v2";
        s.name = L"Press Play ICARUS V2 Nearlink";
        s.kind = DeviceKind::Mouse;
        s.via = L"2.4 GHz receiver";
        s.percent = percent;
        ctx.Add(s, kVid, kIcarusPid);
        break;
    }
}

// The official AJAZZ app sends this read-only initialization/status sequence to report ID 6.
// Each command is padded to the 64-byte output-report length by HidDev.
const std::vector<std::vector<BYTE>>& AjazzStartupCommands() {
    static const std::vector<std::vector<BYTE>> commands = {
        { 0x06, 0x05 },
        { 0x06, 0x05 },
        { 0x06, 0x07 },
        { 0x06, 0x11 },
        { 0x06, 0x31, 0x3B },
        { 0x06, 0x31, 0x05, 0x3B },
    };
    return commands;
}

bool DecodeAjazzStatus(const std::vector<BYTE>& report, int& percent, bool& charging) {
    // Captured AJAZZ NearLink input report:
    // 06 FA 03 00 00 03 [percent 0..100] [charging 0|1] ...
    if (report.size() < 8 || report[0] != 0x06 || report[1] != 0xFA ||
        report[2] != 0x03 || report[5] != 0x03 || report[6] > 100 || report[7] > 1)
        return false;
    percent = report[6];
    charging = report[7] != 0;
    return true;
}

struct AjazzSession {
    std::unique_ptr<HidDev> device;
    int percent = -1;
    bool charging = false;
};

// Keep the receiver handle open between BatteryHub polls so input status reports can queue
// between refreshes (the AJAZZ app also keeps its HID collection open).
std::map<std::wstring, AjazzSession> g_ajazzSessions;

void PollAjazzNearlink(PollContext& ctx) {
    if (ctx.IsClaimed(kVid, kAjazzPid)) return;

    std::set<std::wstring> present;
    for (const HidColl* c : ctx.Find(kVid, kAjazzPid, [](const HidColl& c) {
             return c.iface == 1 && c.page() == kUsagePage && c.usage() == kUsage &&
                    c.inLen() == kReportLength && c.outLen() == kReportLength;
         })) {
        present.insert(c->path);
        AjazzSession& session = g_ajazzSessions[c->path];
        if (!session.device) {
            session.device = std::make_unique<HidDev>();
            if (!session.device->Open(*c)) {
                g_ajazzSessions.erase(c->path);
                continue;
            }
            session.device->Flush();
        }

        auto accept = [&](const std::vector<BYTE>& report) {
            int percent = -1;
            bool charging = false;
            if (!DecodeAjazzStatus(report, percent, charging)) return false;
            session.percent = percent;
            session.charging = charging;
            return true;
        };

        // Reproduce the app's observed setup sequence until the first valid status arrives.
        // Once initialized, keep the HID collection open and listen for later 06 FA status pushes
        // rather than repeatedly replaying the app's startup/configuration exchange.
        if (session.percent < 0) {
            for (const auto& command : AjazzStartupCommands()) {
                std::vector<BYTE> reply;
                if (session.device->SetOutputAndRead(command, reply, 250) > 0)
                    accept(reply);
            }
        }

        // The battery/charging status is a separate 06 FA input report, not the 06 05/07/11/31
        // command echo. It may arrive just after setup or be queued after a state change, so
        // listen briefly and retain the last valid reading between BatteryHub polls.
        // Wait long only until the first reading: after that a push is either queued already or the
        // last value stands, and a 1.5 s wait on every refresh would slow down all other devices.
        const bool first = session.percent < 0;
        for (int i = 0; i < 6; ++i) {
            std::vector<BYTE> report;
            int n = session.device->Read(report, i == 0 && first ? 1500 : 50);
            if (n < 0) break;
            accept(report);
        }

        if (session.percent >= 0) {
            DeviceStatus s;
            s.key = L"ajazz:aj159nlp-nearlink";
            s.name = L"AJAZZ AJ159NL P";
            s.kind = DeviceKind::Mouse;
            s.via = L"2.4 GHz receiver";
            s.percent = session.percent;
            s.charging = session.charging;
            s.graceSec = 1800;
            ctx.Add(s, kVid, kAjazzPid);
            break;
        }
    }

    for (auto it = g_ajazzSessions.begin(); it != g_ajazzSessions.end();) {
        it = present.count(it->first) ? std::next(it) : g_ajazzSessions.erase(it);
    }
}

}  // namespace

void PollPressPlayIcarus(PollContext& ctx) {
    PollIcarus(ctx);
    PollAjazzNearlink(ctx);
}
