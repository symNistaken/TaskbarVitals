// Darmoshark M3 and M3S over their Telink receivers. Protocol reference:
// https://github.com/Barba2k2/darmoshark-m3-configurator/blob/main/PROTOCOL.md
#include "drivers.h"

#include <algorithm>

namespace {
constexpr USHORT kVid = 0x248A;
struct Model {
    USHORT receiver;
    USHORT mouse;
    const wchar_t* name;
};

constexpr Model kModels[] = {
    {0xFF30, 0xFF12, L"Darmoshark M3"},
    {0xFF31, 0xFF18, L"Darmoshark M3S"},
};
constexpr BYTE kFeature = 0x51;
constexpr BYTE kBond = 0x03;
constexpr BYTE kIdentify = 0x06;
constexpr ULONGLONG kReceiverTimeoutMs = 1000;
constexpr ULONGLONG kFirstAttemptMs = 300;
constexpr ULONGLONG kQueryTimeoutMs = 500;

USHORT ReadId(const std::vector<BYTE>& reply, size_t offset) {
    return static_cast<USHORT>(reply[offset] | (reply[offset + 1] << 8));
}

bool Query(HidDev& device, BYTE opcode, std::vector<BYTE>& reply, ULONGLONG receiverDeadline) {
    const auto start = GetTickCount64();
    if (start >= receiverDeadline) return false;
    const auto deadline = (std::min)(receiverDeadline, start + kQueryTimeoutMs);

    device.Flush();
    if (GetTickCount64() >= deadline || !device.SetFeature({kFeature, opcode})) return false;

    std::vector<BYTE> input;
    for (auto now = GetTickCount64(); now < deadline; now = GetTickCount64()) {
        const auto waitMs = static_cast<int>((std::min)(20ULL, deadline - now));
        if (device.Read(input, waitMs) >= 3 &&
            input[0] == 0x54 && input[1] == 0xE4 && input[2] == 2) return false;
        if (GetTickCount64() >= deadline) return false;
        if (device.GetFeature(kFeature, reply) && reply.size() >= 12 &&
            reply[0] == kFeature && reply[1] == opcode) return GetTickCount64() < deadline;
    }
    return false;
}

int ReadBattery(HidDev& device, USHORT mouse, ULONGLONG deadline) {
    // The bond query replaces any previous battery reply in the receiver's
    // feature buffer before we request another one.
    std::vector<BYTE> reply;
    if (!Query(device, kBond, reply, deadline) ||
        ReadId(reply, 3) != kVid || ReadId(reply, 5) != mouse || !reply[7]) return -1;
    if (!Query(device, kIdentify, reply, deadline) || reply[2] != 1 ||
        ReadId(reply, 4) != kVid || ReadId(reply, 6) != mouse || reply[11] > 100) return -1;
    return reply[11];
}

void PollModel(PollContext& ctx, const Model& model) {
    if (ctx.IsClaimed(kVid, model.receiver)) return;
    for (const HidColl* coll : ctx.Find(kVid, model.receiver, [](const HidColl& c) {
             return c.iface == 1 && c.page() == 0x008C && c.usage() == 1 &&
                    c.inLen() == 33 && c.outLen() == 33 && c.featLen() == 1001;
         })) {
        const auto deadline = GetTickCount64() + kReceiverTimeoutMs;
        HidDev device;
        if (!device.Open(*coll)) continue;

        // Leave time to reread the bond and retry if the first battery request goes unanswered.
        const auto firstDeadline = (std::min)(deadline, GetTickCount64() + kFirstAttemptMs);
        int percent = ReadBattery(device, model.mouse, firstDeadline);
        if (percent < 0 && GetTickCount64() < deadline) percent = ReadBattery(device, model.mouse, deadline);
        if (percent < 0) continue;

        DeviceStatus status;
        status.key = ctx.StableKey(L"darmoshark:" + Hex4(model.receiver));
        status.name = model.name;
        status.kind = DeviceKind::Mouse;
        status.via = L"2.4 GHz receiver";
        status.percent = percent;
        ctx.Add(status, kVid, model.receiver);
        return;
    }
}
}  // namespace

void PollDarmoshark(PollContext& ctx) {
    for (const auto& model : kModels) {
        PollModel(ctx, model);
    }
}

// Darmoshark M3S Pro on its Nordic "1K NRF Receiver" (1915:0723), verified on hardware.
// Protocol read from a USB capture of Darmoshark's software. Vendor collection: interface 3,
// usage page FF0A, 64-byte reports without a report id. All 64 bytes of a frame sum to 0xA1 (mod 256).
//   Request  [41][00][81][01] 0 ... [csum]
//   Reply    [41][00][8C][01][..][..][percent] ...
namespace {
constexpr USHORT kNrfVid = 0x1915;
constexpr USHORT kNrfReceiver = 0x0723;
constexpr size_t kNrfFrame = 64;
constexpr BYTE kNrfSum = 0xA1;
constexpr ULONGLONG kNrfTimeoutMs = 600;

BYTE NrfChecksum(const std::vector<BYTE>& frame) {
    unsigned sum = 0;
    for (size_t i = 0; i + 1 < kNrfFrame; ++i) sum += frame[i];
    return static_cast<BYTE>((kNrfSum - sum) & 0xFF);
}

bool NrfReadBattery(HidDev& device, int& percent) {
    std::vector<BYTE> request(kNrfFrame + 1, 0);  // [0] = report id 0
    request[1] = 0x41;
    request[3] = 0x81;
    request[4] = 0x01;
    request[kNrfFrame] = NrfChecksum(std::vector<BYTE>(request.begin() + 1, request.end()));

    device.Flush();
    if (!device.Write(request)) return false;

    const auto deadline = GetTickCount64() + kNrfTimeoutMs;
    std::vector<BYTE> reply;
    for (auto now = GetTickCount64(); now < deadline; now = GetTickCount64()) {
        const auto waitMs = static_cast<int>((std::min)(100ULL, deadline - now));
        if (device.Read(reply, waitMs) < static_cast<int>(kNrfFrame)) continue;
        if (reply[0] != 0x41 || reply[2] != 0x8C || reply[3] != 0x01) continue;
        if (reply[kNrfFrame - 1] != NrfChecksum(reply) || reply[6] > 100) continue;
        percent = reply[6];
        return true;
    }
    return false;
}
}  // namespace

void PollDarmosharkNrf(PollContext& ctx) {
    if (ctx.IsClaimed(kNrfVid, kNrfReceiver)) return;
    for (const HidColl* coll : ctx.Find(kNrfVid, kNrfReceiver, [](const HidColl& c) {
             return c.iface == 3 && c.page() == 0xFF0A && c.usage() == 1 &&
                    c.inLen() == 65 && c.outLen() == 65;
         })) {
        HidDev device;
        int percent = 0;
        if (!device.Open(*coll) || !NrfReadBattery(device, percent)) continue;

        DeviceStatus status;
        status.key = L"darmoshark:" + Hex4(kNrfVid) + L":" + Hex4(kNrfReceiver);
        status.name = L"Darmoshark M3S Pro";
        status.kind = DeviceKind::Mouse;
        status.via = L"2.4 GHz receiver";
        status.percent = percent;
        ctx.Add(status, kNrfVid, kNrfReceiver);
        return;
    }
}
