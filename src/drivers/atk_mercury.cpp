// ATK Mercury I / M1 headset on its 373B:107C 2.4 GHz receiver. The receiver exposes a
// 64-byte feature-report mailbox on FF22:0001 (mi_00/col02). Each command is sent with
// SetFeature, followed by a 300 ms wait and GetFeature. The protocol and addresses come from
// Joule PR #79: https://github.com/DillanCidral/Joule/pull/79
#include "drivers.h"

#include <algorithm>
#include <cwctype>

namespace {

constexpr USHORT kVid = 0x373B, kPid = 0x107C, kReceiverVersion = 0x0005;
constexpr DWORD kPowerPointerAddress = 0x0100FE94;
constexpr DWORD kExpectedPowerPointer = 0x0100FE5C;
constexpr DWORD kCapacityAddress = 0x0100FE78;

bool IsMercuryCollection(const HidColl& c) {
    std::wstring path = c.path;
    std::transform(path.begin(), path.end(), path.begin(), towlower);
    return c.version == kReceiverVersion && c.page() == 0xFF22 && c.usage() == 0x0001 &&
           c.featLen() == 64 && path.find(L"&mi_00&col02") != std::wstring::npos;
}

bool Query(HidDev& d, const std::vector<BYTE>& request, std::vector<BYTE>& reply) {
    if (request.size() != 64 || !d.SetFeature(request)) return false;
    Sleep(300);
    return d.GetFeature(request[0], reply) && reply.size() == 64;
}

bool KnownFirmwareReply(const std::vector<BYTE>& reply) {
    static const BYTE joule[] = { 0xAA, 0x01, 0x00, 0x00, 0x00, 0x07 };
    static const BYTE mercuryM1[] = {
        0xAA, 0x01, 0x00, 0x00, 0x01, 0x03, 0xFF, 0x25, 0x05, 0x08, 0x11, 0x34
    };
    return reply.size() == 64 &&
           (std::equal(std::begin(joule), std::end(joule), reply.begin()) ||
            std::equal(std::begin(mercuryM1), std::end(mercuryM1), reply.begin()));
}

std::vector<BYTE> MemoryRequest(DWORD address) {
    std::vector<BYTE> request(64, 0);
    request[0] = 0xAA;
    request[1] = 0x03;
    request[3] = 0x01;
    request[4] = 0x04;
    request[5] = (BYTE)(address >> 24);
    request[6] = (BYTE)(address >> 16);
    request[7] = (BYTE)(address >> 8);
    request[8] = (BYTE)address;
    return request;
}

bool ParseMemoryReply(const std::vector<BYTE>& reply, DWORD& value) {
    if (reply.size() != 64 || reply[0] != 0xAA || reply[1] != 0x03 || reply[2] != 0x04)
        return false;
    value = (DWORD)reply[3] | ((DWORD)reply[4] << 8) | ((DWORD)reply[5] << 16) |
            ((DWORD)reply[6] << 24);
    return true;
}

bool ReadBattery(const HidColl& c, int& percent) {
    HidDev d;
    if (!d.OpenFeatureOnly(c)) return false;

    std::vector<BYTE> request(64, 0), reply;
    request[0] = 0xAA;
    request[1] = 0x01;
    if (!Query(d, request, reply) || !KnownFirmwareReply(reply)) return false;

    DWORD value = 0;
    if (!Query(d, MemoryRequest(kPowerPointerAddress), reply) ||
        !ParseMemoryReply(reply, value) || value != kExpectedPowerPointer)
        return false;

    if (!Query(d, MemoryRequest(kCapacityAddress), reply) ||
        !ParseMemoryReply(reply, value) || value > 100)
        return false;
    percent = (int)value;
    return true;
}

}  // namespace

void PollAtkMercury(PollContext& ctx) {
    if (ctx.IsClaimed(kVid, kPid)) return;

    bool present = false;
    for (const HidColl& c : ctx.colls)
        present |= c.vid == kVid && c.pid == kPid;
    if (!present) return;

    // This VID:PID belongs to the headset receiver even if its firmware or collection layout is
    // unknown. Keep the generic ATK mouse driver from sending an unrelated protocol to it.
    ctx.Claim(kVid, kPid);
    for (const HidColl* c : ctx.Find(kVid, kPid, IsMercuryCollection)) {
        int percent = -1;
        if (!ReadBattery(*c, percent)) continue;

        DeviceStatus s;
        s.key = L"atk_mercury:107C";
        s.name = L"ATK Mercury I / M1";
        s.kind = DeviceKind::Headset;
        s.via = L"2.4 GHz receiver";
        s.percent = percent;
        ctx.Add(s, kVid, kPid);
        return;
    }
}
