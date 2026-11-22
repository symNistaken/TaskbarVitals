// Frame-level checks for the GameSir SAPJoy battery decoder. No hardware, no Win32.
// The reply layout comes from captured traffic; the MAC here is a placeholder, not a real device.
#include "../src/drivers/gamesir_sapjoy_protocol.h"

#include <cassert>
#include <cstdio>
#include <cstring>
#include <vector>

using namespace GameSirSapjoy;

namespace {

// [0]=input report id, [1]=len, [2]=cmd, [3]=sub, [4]=id, [5..6]=firmware, [7..12]=MAC,
// [13]=battery, [14]=type, rest zero — 64-byte input report.
std::vector<std::uint8_t> Reply(std::uint8_t battery, std::uint8_t type = 0x00) {
    std::vector<std::uint8_t> r(64, 0x00);
    r[0] = 0x06;
    r[1] = 0x0E;  // 14 bytes including the length byte
    r[2] = 0x0A;
    r[3] = 0x01;
    r[4] = 0x03;  // device id
    r[5] = 0x01;  // firmware high
    r[6] = 0x16;  // firmware low
    const std::uint8_t mac[6] = { 0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF };
    std::memcpy(r.data() + 7, mac, sizeof(mac));
    r[13] = battery;
    r[14] = type;
    return r;
}

}  // namespace

int main() {
    // ---- request ---------------------------------------------------------------------------
    {
        std::uint8_t req[kOutputReportLen];
        std::memset(req, 0xEE, sizeof(req));
        assert(BuildDeviceInfoRequest(req, sizeof(req)));
        assert(req[0] == 0x07);   // output report id
        assert(req[1] == 0x03);   // len counts itself and excludes the report id
        assert(req[2] == 0x0A);   // GetDeviceInfo
        assert(req[3] == 0x01);   // sub
        for (std::size_t i = 4; i < kOutputReportLen; ++i) assert(req[i] == 0x00);  // zero padded

        std::uint8_t tooSmall[16];
        assert(!BuildDeviceInfoRequest(tooSmall, sizeof(tooSmall)));   // refuses a short buffer
    }

    // ---- a normal reading ------------------------------------------------------------------
    {
        auto r = Reply(0x60);   // 96 %
        const Reading d = DecodeDeviceInfo(r.data(), r.size());
        assert(d.answered);
        assert(!d.charging);
        assert(d.percent == 96);
        assert(d.deviceId == 0x03);
        assert(d.address[0] == 0xAA && d.address[5] == 0xFF);
        assert(d.deviceType == 0x00);
    }

    // ---- charging: 255 carries no percentage ------------------------------------------------
    {
        auto r = Reply(0xFF);
        const Reading d = DecodeDeviceInfo(r.data(), r.size());
        assert(d.answered);
        assert(d.charging);
        assert(d.percent == -1);   // caller keeps the last known value
    }

    // ---- above 100 is clamped, not rejected -------------------------------------------------
    {
        auto r = Reply(0xD0);   // 208
        const Reading d = DecodeDeviceInfo(r.data(), r.size());
        assert(d.answered);
        assert(d.percent == 100);
    }

    // ---- boundaries -------------------------------------------------------------------------
    {
        auto zero = Reply(0x00);
        assert(DecodeDeviceInfo(zero.data(), zero.size()).percent == 0);
        auto full = Reply(100);
        assert(DecodeDeviceInfo(full.data(), full.size()).percent == 100);
    }

    // ---- anything that is not a GetDeviceInfo reply is ignored ------------------------------
    {
        auto state = Reply(0x60);
        state[0] = 0x12;   // a gamepad state frame on the same collection
        assert(!DecodeDeviceInfo(state.data(), state.size()).answered);

        auto wrongCmd = Reply(0x60);
        wrongCmd[2] = 0x01;   // some other command
        assert(!DecodeDeviceInfo(wrongCmd.data(), wrongCmd.size()).answered);

        auto wrongSub = Reply(0x60);
        wrongSub[3] = 0x02;
        assert(!DecodeDeviceInfo(wrongSub.data(), wrongSub.size()).answered);

        auto shortReply = Reply(0x60);
        shortReply.resize(14);   // [14] (type) is missing
        assert(!DecodeDeviceInfo(shortReply.data(), shortReply.size()).answered);

        for (auto length : {0, 13, 64, 255}) {
            auto badLength=Reply(96);badLength[1]=length;
            assert(!DecodeDeviceInfo(badLength.data(),badLength.size()).answered);
        }
        assert(!DecodeDeviceInfo(nullptr, 0).answered);
    }

    std::puts("GameSir SAPJoy protocol tests passed (request frame, offsets, charging, clamping, filtering).");
    return 0;
}
