#include "../src/drivers/soundcore_protocol.h"

#include <cassert>
#include <cstdio>
#include <vector>

using Bytes = std::vector<std::uint8_t>;

int main() {
    // Outbound requests must match OpenSCQ30's verified test vectors byte for byte.
    auto reqState = Soundcore::BuildEmptyRequest(Soundcore::kCmdState);
    const std::array<std::uint8_t, 10> wantState = { 0x08, 0xEE, 0x00, 0x00, 0x00, 0x01, 0x01, 0x0A, 0x00, 0x02 };
    assert(reqState == wantState);

    auto reqBat = Soundcore::BuildEmptyRequest(Soundcore::kCmdBatteryLevel);
    const std::array<std::uint8_t, 10> wantBat = { 0x08, 0xEE, 0x00, 0x00, 0x00, 0x01, 0x03, 0x0A, 0x00, 0x04 };
    assert(reqBat == wantBat);

    auto reqChg = Soundcore::BuildEmptyRequest(Soundcore::kCmdBatteryCharging);
    const std::array<std::uint8_t, 10> wantChg = { 0x08, 0xEE, 0x00, 0x00, 0x00, 0x01, 0x04, 0x0A, 0x00, 0x05 };
    assert(reqChg == wantChg);

    auto reqSn = Soundcore::BuildEmptyRequest(Soundcore::kCmdSerialAndFw);
    const std::array<std::uint8_t, 10> wantSn = { 0x08, 0xEE, 0x00, 0x00, 0x00, 0x01, 0x05, 0x0A, 0x00, 0x06 };
    assert(reqSn == wantSn);

    // Live capture from soundcore Life Q30 (A3028, firmware 04.33): 1/5 = 20%, not charging.
    const Bytes q30Level = { 0x09, 0xFF, 0x00, 0x00, 0x01, 0x01, 0x03, 0x0B, 0x00, 0x01, 0x19 };
    const Bytes q30Charging = { 0x09, 0xFF, 0x00, 0x00, 0x01, 0x01, 0x04, 0x0B, 0x00, 0x00, 0x19 };
    const Bytes q30ChargingYes = { 0x09, 0xFF, 0x00, 0x00, 0x01, 0x01, 0x04, 0x0B, 0x00, 0x01, 0x1A };
    const Bytes q30State = {
        0x09, 0xFF, 0x00, 0x00, 0x01, 0x01, 0x01, 0x4D, 0x00, 0x01, 0x00, 0xFE, 0xFE, 0xB4, 0xB4, 0xB4,
        0xB4, 0xB4, 0xB4, 0xB4, 0xB4, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x01, 0x02, 0x00, 0x00,
        0x30, 0x34, 0x2E, 0x33, 0x33, 0x33, 0x30, 0x32, 0x38, 0x34, 0x34, 0x30, 0x45, 0x43, 0x43, 0x34,
        0x34, 0x34, 0x37, 0x39, 0x38, 0x00, 0x01, 0x01, 0x01, 0x00, 0x00, 0x01, 0x69
    };

    Soundcore::Packet pkt;
    Soundcore::BatteryStatus st;
    assert(Soundcore::FindInboundPacket(q30Level.data(), q30Level.size(), Soundcore::kCmdBatteryLevel, pkt));
    assert(Soundcore::ParseBatteryLevelBody(pkt.body, Soundcore::ScaleForModelCode("3028"), st));
    assert(!st.dual && st.percent == 20 && !st.charging);

    assert(Soundcore::FindInboundPacket(q30ChargingYes.data(), q30ChargingYes.size(), Soundcore::kCmdBatteryCharging, pkt));
    assert(Soundcore::ApplyBatteryChargingBody(pkt.body, st));
    assert(st.charging);

    assert(Soundcore::FindInboundPacket(q30Charging.data(), q30Charging.size(), Soundcore::kCmdBatteryCharging, pkt));
    assert(Soundcore::ApplyBatteryChargingBody(pkt.body, st));
    assert(!st.charging);

    // State update fallback + model code extraction ("3028" from "3028440ECC444798").
    assert(Soundcore::FindInboundPacket(q30State.data(), q30State.size(), Soundcore::kCmdState, pkt));
    assert(Soundcore::ExtractModelCode(pkt.body) == "3028");
    assert(!Soundcore::IsDualModelCode("3028"));
    Soundcore::BatteryStatus stState;
    assert(Soundcore::ParseStateUpdateBody(pkt.body, Soundcore::ScaleForModelCode("3028"), false, stState));
    assert(!stState.dual && stState.percent == 20 && !stState.charging);

    // Dual-battery TWS packets (from OpenSCQ30 unit tests): L=3/5 (60%), R=4/5 (80%), L charging.
    const Bytes twsLevel = { 0x09, 0xFF, 0x00, 0x00, 0x01, 0x01, 0x03, 0x0C, 0x00, 0x03, 0x04, 0x20 };
    const Bytes twsChg = { 0x09, 0xFF, 0x00, 0x00, 0x01, 0x01, 0x04, 0x0C, 0x00, 0x01, 0x00, 0x1B };
    Soundcore::BatteryStatus tws;
    assert(Soundcore::FindInboundPacket(twsLevel.data(), twsLevel.size(), Soundcore::kCmdBatteryLevel, pkt));
    assert(Soundcore::ParseBatteryLevelBody(pkt.body, Soundcore::Scale{}, tws));
    assert(tws.dual && tws.left == 60 && tws.right == 80 && tws.percent == 60);
    assert(Soundcore::FindInboundPacket(twsChg.data(), twsChg.size(), Soundcore::kCmdBatteryCharging, pkt));
    assert(Soundcore::ApplyBatteryChargingBody(pkt.body, tws));
    assert(tws.leftCharging && !tws.rightCharging && tws.charging);

    // One earbud disconnected (0xFF) reports only the connected earbud.
    Soundcore::BatteryStatus oneBud;
    assert(Soundcore::ParseBatteryLevelBody({ 0xFF, 0x04 }, Soundcore::Scale{}, oneBud));
    assert(oneBud.dual && oneBud.left == -1 && oneBud.right == 80 && oneBud.percent == 80);

    // Model-specific scales: Space One Pro (3062, 0..9 -> 10%..100%), P30i (3959, 0..10), Liberty 4 Pro (3954, 0..100).
    assert(Soundcore::RawToPercent(0, Soundcore::ScaleForModelCode("3062")) == 10);
    assert(Soundcore::RawToPercent(9, Soundcore::ScaleForModelCode("3062")) == 100);
    assert(Soundcore::RawToPercent(7, Soundcore::ScaleForModelCode("3959")) == 70);
    assert(Soundcore::RawToPercent(83, Soundcore::ScaleForModelCode("3954")) == 83);
    assert(Soundcore::ScaleFromNameLower("soundcore space one pro").levelOffset == 1);
    assert(Soundcore::ScaleFromNameLower("soundcore liberty 4 pro").maxLevel == 100);

    // FastPair Message Stream battery packets:
    // Single headphone (40%, not charging): 03 03 00 01 28
    const Bytes fpSingle = { 0x03, 0x01, 0x00, 0x03, 0x68, 0x58, 0xCE, 0x03, 0x03, 0x00, 0x01, 0x28 };
    Soundcore::BatteryStatus fpSt;
    assert(Soundcore::ParseFastPairStream(fpSingle.data(), fpSingle.size(), fpSt));
    assert(!fpSt.dual && fpSt.percent == 40 && !fpSt.charging);

    // Single headphone (75%, charging -> 0x80 | 75 = 0xCB): 03 03 00 01 CB
    const Bytes fpChg = { 0x03, 0x03, 0x00, 0x01, 0xCB };
    assert(Soundcore::ParseFastPairStream(fpChg.data(), fpChg.size(), fpSt));
    assert(!fpSt.dual && fpSt.percent == 75 && fpSt.charging);

    // Dual TWS earbuds (Left: 80% charging -> 0xD0, Right: 70% not charging -> 0x46, Case: 90% -> 0x5A)
    const Bytes fpTws = { 0x03, 0x03, 0x00, 0x03, 0xD0, 0x46, 0x5A };
    assert(Soundcore::ParseFastPairStream(fpTws.data(), fpTws.size(), fpSt));
    assert(fpSt.dual && fpSt.left == 80 && fpSt.leftCharging && fpSt.right == 70 && !fpSt.rightCharging && fpSt.charging && fpSt.percent == 70);

    // Corrupted checksum is rejected.
    Bytes bad = q30Level;
    bad.back() ^= 0x01;
    assert(!Soundcore::FindInboundPacket(bad.data(), bad.size(), Soundcore::kCmdBatteryLevel, pkt));

    // Noise before a valid packet is skipped.
    Bytes noisy = { 0x00, 0x09, 0xFF, 0x12 };
    noisy.insert(noisy.end(), q30Level.begin(), q30Level.end());
    assert(Soundcore::FindInboundPacket(noisy.data(), noisy.size(), Soundcore::kCmdBatteryLevel, pkt));
    assert(pkt.body.size() == 1 && pkt.body[0] == 0x01);

    std::puts("soundcore_protocol_test: ok");
    return 0;
}
