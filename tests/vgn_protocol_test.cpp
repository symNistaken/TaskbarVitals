// Frame-level checks for the VGN battery decoders. No hardware, no Win32: the captured replies
// come from the request/reply layouts in VGN's own HUB client.
#include "../src/drivers/vgn_protocol.h"

#include <cassert>
#include <cstdio>
#include <vector>

using namespace Vgn;

namespace {

std::vector<std::uint8_t> Pad(std::vector<std::uint8_t> v, std::size_t n, std::uint8_t fill = 0) {
    v.resize(n, fill);
    return v;
}

}  // namespace

int main() {
    // ---- Da: [id][04][00][00][00][level][charging][mvHi][mvLo]... ----------------------------
    {
        // HidDev keeps the report id for numbered reports.
        // [reportId][cmd][0][addrHi][addrLo][len][level][charging][mvHi][mvLo]
        std::vector<std::uint8_t> reply = Pad({ 0x08, 0x04, 0x00, 0x00, 0x00, 0x00, 87, 0x00, 0x0F, 0x8C }, 16);
        Reading r = DecodeDa(reply.data(), reply.size(), 8);
        assert(r.answered && r.percent == 87 && !r.charging && r.millivolts == 0x0F8C);

        // ...and drops it only when the report is unnumbered, so the same frame without the id
        // must also decode once the id is 0.
        std::vector<std::uint8_t> noId(reply.begin() + 1, reply.end());
        Reading r2 = DecodeDa(noId.data(), noId.size(), 0);
        assert(r2.answered && r2.percent == 87 && r2.millivolts == 0x0F8C);

        // Charging bit, and an out-of-range level must be refused rather than shown as 255%.
        // Level sits at reply[6] (the command block starts at reply[1]); 149 must be refused.
        reply[6] = 0x95;
        assert(!DecodeDa(reply.data(), reply.size(), 8).answered);

        // A reply to another command is not a battery answer.
        reply[6] = 55;
        reply[1] = 0x03;  // DeviceOnLine, not a battery reply
        assert(!DecodeDa(reply.data(), reply.size(), 8).answered);
    }

    // ---- Da request: checksum = 85 - sum(bytes 0..14) - reportId ------------------------------
    {
        std::uint8_t body[16] = {};
        assert(BuildDaRequest(body, sizeof(body), 4, 8) == 16);
        assert(body[0] == 4 && body[15] == 0x49);
        assert(BuildDaRequest(body, sizeof(body), 3, 8) == 16 && body[0] == 3);
        assert(BuildDaRequest(body, 8, 4, 8) == 0);  // refuses a short buffer
    }

    // ---- King: level at +16, wired when bit 1 of the config byte is clear ---------------------
    {
        // The client slices the reply at byte 7 after removing the report id, so with the id kept
        // the config block starts at reply[8]: [8]=cmd, [8+8]=flags, [8+9]=level.
        std::vector<std::uint8_t> reply(64, 0);
        reply[0] = 0x20;   // report id 32
        reply[8] = 0x08;   // GetUserMouseConfig echo
        reply[16] = 0x00;  // flags: bit 1 clear -> cable
        reply[17] = 64;    // level
        Reading r = DecodeKing(reply.data(), reply.size(), 32);
        assert(r.answered && r.percent == 64 && r.wired && r.charging);

        reply[16] = 0x02;  // bit 1 set -> wireless
        Reading r2 = DecodeKing(reply.data(), reply.size(), 32);
        assert(r2.answered && r2.percent == 64 && !r2.wired && !r2.charging);
    }

    // ---- Kc2: 127 & byte 19 ---------------------------------------------------------------
    {
        // The dongle answers reads on 179 even though writes go out on 181.
        std::vector<std::uint8_t> reply(32, 0);
        reply[0] = 0xB3;  // 179
        reply[1] = 0x06;
        reply[20] = 0x90;  // 0x90 = 144, so the low 7 bits give 16
        Reading r = DecodeKc2(reply.data(), reply.size(), 179);
        assert(r.answered && r.percent == 16);

        reply[20] = 100;
        assert(DecodeKc2(reply.data(), reply.size(), 179).percent == 100);

        reply[20] = 0xFF;  // 127 -> refused, not 127%
        assert(!DecodeKc2(reply.data(), reply.size(), 179).answered);
    }

    // ---- LdMs: solicited (02 25 ...) and unsolicited 0x50 ----------------------------------
    {
        std::vector<std::uint8_t> reply(32, 0);
        reply[0] = 0xA0;  // 160
        reply[1] = 0x02;
        reply[2] = 0x25;
        reply[3] = 73;
        Reading r = DecodeLdMs(reply.data(), reply.size(), 160);
        assert(r.answered && r.percent == 73);

        // Unsolicited 0x50 status: [id][50][lo][hi]...[7]=level. With the id at [0] the level is
        // at [8], and the counter bytes [1][2] tell wired from wireless.
        std::vector<std::uint8_t> push(32, 0);
        push[0] = 0xA0;
        push[1] = 0x50;
        push[2] = 0x01;
        push[3] = 0x02;
        push[8] = 41;
        Reading r2 = DecodeLdMs(push.data(), push.size(), 160);
        assert(r2.answered && r2.percent == 41 && !r2.wired);
    }

    // ---- Yongjiaxin: 0x55 0x30, level at +8 ----------------------------------------------
    {
        std::vector<std::uint8_t> reply(64, 0);
        reply[0] = 0x00;  // report id 0 is dropped by HidDev, so nothing to skip
        reply[1] = 0x55;
        reply[2] = 0x30;
        reply[9] = 88;
        Reading r = DecodeYongjiaxin(reply.data() + 1, reply.size() - 1, 0);
        assert(r.answered && r.percent == 88 && !r.wired);
    }

    // ---- Tjx (N87): 30 02 00 len ... -----------------------------------------------------
    {
        std::vector<std::uint8_t> reply = { 0xF0, 0x30, 0x02, 0x00, 0x02, 0, 0, 0, 61, 0x01 };
        Reading r = DecodeTjx(reply.data(), reply.size(), 240);
        assert(r.answered && r.percent == 61 && r.charging);

        reply[7] = 0x00;  // length 0 means "no payload", not 0%
        reply[4] = 0x00;
        assert(!DecodeTjx(reply.data(), reply.size(), 240).answered);
    }

    // ---- Weisheng (S99): command echoes at +2, level at +7, 0xFF means asleep -------------
    {
        // A real capture, byte for byte: 04 00 00 1A 06 00 00 00 4F ...
        std::vector<std::uint8_t> reply = { 0x04, 0x00, 0x00, 0x1A, 0x06, 0x00, 0x00, 0x00, 0x4F, 0x00 };
        Reading r = DecodeWeisheng(reply.data(), reply.size(), 4);
        assert(r.answered && r.percent == 79);

        std::vector<std::uint8_t> asleep = { 0x04, 0x00, 0x00, 0xFF, 0x06, 0x00, 0x00, 0x00, 0x00 };
        assert(!DecodeWeisheng(asleep.data(), asleep.size(), 4).answered);

        std::uint8_t body[8] = {};
        assert(BuildWeishengRequest(body, sizeof(body), 26) == 7);
        assert(body[2] == 26 && body[3] == 6);
    }

    // ---- Beiying feature (wired keyboard): level at +8, no command echo -------------------
    {
        std::vector<std::uint8_t> reply(520, 0);
        reply[0] = 0x09;
        reply[9] = 45;
        Reading r = DecodeBeiyingFeature(reply.data(), reply.size(), 9);
        assert(r.answered && r.percent == 45);
    }

    // ---- Beiying output (dongle): 19-byte frame, level at +4 ------------------------------
    {
        std::vector<std::uint8_t> reply(19, 0);
        reply[0] = 0x13;
        reply[1] = 0x4A;
        reply[5] = 52;
        Reading r = DecodeBeiyingOutput(reply.data(), reply.size(), 19);
        assert(r.answered && r.percent == 52 && !r.charging);

        std::uint8_t body[24] = {};
        assert(BuildBeiyingOutputRequest(body, sizeof(body), 0x4A, 0x01, 19) == 19);
        assert(body[0] == 0x4A && body[1] == 0x01);
        // checksum covers every byte plus the report id
        unsigned sum = 19;
        for (int i = 0; i < 18; i++) sum += body[i];
        assert(body[18] == (std::uint8_t)(sum & 0xFF));
    }

    // ---- Rongyuan (Neon75): feature report, echoed command or bare level ------------------
    {
        // GetFeature(0) does not strip anything, so byte 0 is already the payload: the answered
        // command sits at [0] and the level at [1].
        std::vector<std::uint8_t> echoed(64, 0);
        echoed[0] = 130;
        echoed[1] = 66;
        Reading r = DecodeRongyuan(echoed.data(), echoed.size(), 0);
        assert(r.answered && r.percent == 66);

        std::uint8_t body[64] = {};
        assert(BuildRongyuanRequest(body, sizeof(body), 130) == 64);
        assert(body[0] == 130 && body[7] == (std::uint8_t)((255 - 130) & 0xFF));
    }

    // ---- Device table --------------------------------------------------------------------
    {
        assert(Find(0x3554, 0xFB3D) != nullptr);                    // Y2 Ultra, Da
        assert(Find(0x3554, 0xFB3D)->family == Family::Da);
        assert(Find(0x373B, 0x1028)->family == Family::Weisheng);   // S87 wired
        assert(Find(0x373B, 0x1029)->receiver);                     // S87 dongle
        // Ids that other drivers already claim are deliberately absent from this table
        // (see the header of vgn_table.inc), so the two drivers cannot fight over a device.
        assert(Find(0x320F, 0x5055) == nullptr);                    // atk_keyboard.cpp
        assert(Find(0x320F, 0x5088) == nullptr);                    // atk_keyboard.cpp
        assert(Find(0x3554, 0xF505) == nullptr);                    // kysona.cpp
        assert(Find(0x3151, 0x5038) == nullptr);                    // rongyuan.cpp
        assert(Find(0xA8A4, 0x2355)->family == Family::Tjx);        // N87
        assert(Find(0x258A, 0x024E)->channel == Channel::Feature);  // V98Pro V3 wired
        assert(Find(0x258A, 0x024F)->channel == Channel::Output);   // V98Pro V3 dongle
        assert(Find(0x1CA6, 0x0104) == nullptr);                    // Neon75: SDK family, not mapped
    }

    std::puts("VGN protocol tests passed (frames, offsets, checksums, device table).");
    return 0;
}
