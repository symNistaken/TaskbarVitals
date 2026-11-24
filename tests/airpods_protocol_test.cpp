#include "../src/drivers/airpods_protocol.h"

#include <cassert>
#include <cstdio>
#include <vector>

using Bytes = std::vector<std::uint8_t>;

int main() {
    // 1. AAP single-battery (AirPods Max): 1%-granular discharging, charging, and full 100%.
    {
        const Bytes maxDischarging = {
            0x04, 0x00, 0x04, 0x00, 0x04, 0x00, 0x01,
            0x01, 0x01, 0x49, 0x02, 0x01  // 73%, discharging (0x02)
        };
        AirPodsAap::BatteryReading r;
        assert(AirPodsAap::ParseBatteryPacket(maxDischarging.data(), maxDischarging.size(), r));
        assert(r.singleModel);
        assert(r.single == 73 && !r.singleCharging);
        assert(r.left == -1 && r.right == -1 && r.box == -1);

        const Bytes maxCharging = {
            0x04, 0x00, 0x04, 0x00, 0x04, 0x00, 0x01,
            0x01, 0x01, 0x64, 0x01, 0x01  // 100%, charging (0x01)
        };
        assert(AirPodsAap::ParseBatteryPacket(maxCharging.data(), maxCharging.size(), r));
        assert(r.singleModel && r.single == 100 && r.singleCharging);
    }

    // 2. AAP dual-bud + case (AirPods Pro): 0x01 charging, 0x02 discharging, 0x05 charging in case.
    {
        const Bytes proStatus = {
            0x04, 0x00, 0x04, 0x00, 0x04, 0x00, 0x03,
            0x02, 0x01, 0x5B, 0x02, 0x01,  // Right 91%, discharging
            0x04, 0x01, 0x4E, 0x05, 0x01,  // Left 78%, charging in case (0x05)
            0x08, 0x01, 0x2A, 0x01, 0x01   // Case 42%, charging (0x01)
        };
        AirPodsAap::BatteryReading r;
        assert(AirPodsAap::ParseBatteryPacket(proStatus.data(), proStatus.size(), r));
        assert(!r.singleModel);
        assert(r.right == 91 && !r.rightCharging);
        assert(r.left == 78 && r.leftCharging);
        assert(r.box == 42 && r.boxCharging);
    }

    // 3. Disconnected (0x04) / absent (0xFF) / (0x00, 0) sentinels must not overwrite known readings.
    {
        AirPodsAap::BatteryReading state;
        const Bytes full = {
            0x04, 0x00, 0x04, 0x00, 0x04, 0x00, 0x03,
            0x02, 0x01, 0x53, 0x02, 0x01,  // Right 83%
            0x04, 0x01, 0x55, 0x02, 0x01,  // Left 85%
            0x08, 0x01, 0x3D, 0x02, 0x01   // Case 61%
        };
        AirPodsAap::BatteryReading p1;
        assert(AirPodsAap::ParseBatteryPacket(full.data(), full.size(), p1));
        AirPodsAap::MergeBattery(state, p1);
        assert(state.left == 85 && state.right == 83 && state.box == 61);

        // Lid closed: case reports 0x04 (disconnected) with level 0, right updates to 82%,
        // left slot reports 0xFF unknown.
        const Bytes partial = {
            0x04, 0x00, 0x04, 0x00, 0x04, 0x00, 0x03,
            0x02, 0x01, 0x52, 0x02, 0x01,  // Right 82%
            0x04, 0x01, 0xFF, 0x02, 0x01,  // Left 0xFF (unknown)
            0x08, 0x01, 0x00, 0x04, 0x01   // Case disconnected (0x04)
        };
        AirPodsAap::BatteryReading p2;
        assert(AirPodsAap::ParseBatteryPacket(partial.data(), partial.size(), p2));
        AirPodsAap::MergeBattery(state, p2);
        assert(state.right == 82);
        assert(state.left == 85);  // preserved
        assert(state.box == 61);   // preserved

        // All disconnected or (0x00, 0) sentinel fails ParseBatteryPacket cleanly.
        const Bytes allDisconnected = {
            0x04, 0x00, 0x04, 0x00, 0x04, 0x00, 0x02,
            0x04, 0x01, 0x00, 0x04, 0x01,
            0x08, 0x01, 0x00, 0x00, 0x01
        };
        AirPodsAap::BatteryReading p3;
        assert(!AirPodsAap::ParseBatteryPacket(allDisconnected.data(), allDisconnected.size(), p3));
    }

    // 4. Reject malformed / truncated / out-of-range AAP packets.
    {
        AirPodsAap::BatteryReading r;
        const Bytes wrongOpcode = { 0x04, 0x00, 0x04, 0x00, 0x06, 0x00, 0x01, 0x01, 0x01, 0x50, 0x02, 0x01 };
        assert(!AirPodsAap::ParseBatteryPacket(wrongOpcode.data(), wrongOpcode.size(), r));

        const Bytes truncated = { 0x04, 0x00, 0x04, 0x00, 0x04, 0x00, 0x01, 0x01, 0x01, 0x50, 0x02 };
        assert(!AirPodsAap::ParseBatteryPacket(truncated.data(), truncated.size(), r));

        const Bytes zeroCount = { 0x04, 0x00, 0x04, 0x00, 0x04, 0x00, 0x00 };
        assert(!AirPodsAap::ParseBatteryPacket(zeroCount.data(), zeroCount.size(), r));

        const Bytes countFive = {
            0x04, 0x00, 0x04, 0x00, 0x04, 0x00, 0x05,
            0x01, 0x01, 0x50, 0x02, 0x01, 0x02, 0x01, 0x50, 0x02, 0x01,
            0x04, 0x01, 0x50, 0x02, 0x01, 0x08, 0x01, 0x50, 0x02, 0x01,
            0x01, 0x01, 0x50, 0x02, 0x01
        };
        assert(!AirPodsAap::ParseBatteryPacket(countFive.data(), countFive.size(), r));

        const Bytes over100 = { 0x04, 0x00, 0x04, 0x00, 0x04, 0x00, 0x01, 0x01, 0x01, 0x65, 0x02, 0x01 };
        assert(!AirPodsAap::ParseBatteryPacket(over100.data(), over100.size(), r));
    }

    // 5. Stream extraction (split frames, sticky packets, non-battery packets, and >4KB garbage cap).
    {
        Bytes stream;
        AirPodsAap::BatteryReading merged;

        // Chunk 1: ear detection packet + first half of battery packet (header split).
        const Bytes chunk1 = {
            0x99, 0x88,  // noise
            0x04, 0x00, 0x04, 0x00, 0x06, 0x00, 0x00, 0x01,  // ear detection
            0x04, 0x00, 0x04, 0x00, 0x04, 0x00, 0x02,        // battery header (count=2)
            0x04, 0x01, 0x43                                 // partial left=67%
        };
        stream.insert(stream.end(), chunk1.begin(), chunk1.end());
        assert(!AirPodsAap::ExtractBatteryPackets(stream, merged));
        assert(stream.size() == 10);  // incomplete battery frame retained

        // Chunk 2: rest of battery frame + a newer single-battery frame sticky behind it + trailing partial prefix.
        const Bytes chunk2 = {
            0x02, 0x01, 0x02, 0x01, 0x44, 0x01, 0x01,        // completes left=67% dis, right=68% chg
            0x04, 0x00, 0x04, 0x00, 0x04, 0x00, 0x01,
            0x04, 0x01, 0x42, 0x02, 0x01,                    // newer left=66% dis
            0x04, 0x00                                       // partial header prefix for next chunk
        };
        stream.insert(stream.end(), chunk2.begin(), chunk2.end());
        assert(AirPodsAap::ExtractBatteryPackets(stream, merged));
        assert(merged.left == 66 && !merged.leftCharging);
        assert(merged.right == 68 && merged.rightCharging);
        assert(stream == (Bytes{ 0x04, 0x00 }));

        // Garbage buffer > 4096 bytes stays bounded.
        Bytes junk(5000, 0xAA);
        AirPodsAap::BatteryReading junkOut;
        assert(!AirPodsAap::ExtractBatteryPackets(junk, junkOut));
        assert(junk.empty());
    }

    // 6. Real AirPods Max (PID 0x200A) BLE proximity capture from local hardware: 70% charging.
    {
        const Bytes maxBle = {
            0x07, 0x19, 0x01, 0x0A, 0x20, 0x00, 0x07, 0x90,
            0x03, 0x10, 0x04, 0xDE, 0xF0, 0x1B, 0x4D, 0x39,
            0x88, 0xF6, 0x5F, 0x49, 0x9F, 0x50, 0xC2, 0xC0,
            0xE5, 0xE6, 0x26
        };
        AirPodsBle::Advert a;
        assert(AirPodsBle::Parse(maxBle.data(), maxBle.size(), a));
        assert(a.model == 0x200A && a.single);
        assert(a.left == 70 && a.leftChg);
        assert(a.right == -1 && a.box == -1);
    }

    // 7. Dual-bud BLE decoding with leftBroadcasts bit toggled.
    {
        Bytes proLeft = {
            0x07, 0x19, 0x01, 0x14, 0x20, 0x20,  // bit 5 set -> left broadcasts
            0x87,                                // cur (left)=70%, other (right)=80%
            0x56                                 // chg: cur(bit0)=1, other(bit1)=0, box(bit2)=1 -> 0x5; box=60%
        };
        proLeft.resize(27, 0x00);
        AirPodsBle::Advert a;
        assert(AirPodsBle::Parse(proLeft.data(), proLeft.size(), a));
        assert(!a.single && a.leftBroadcasts);
        assert(a.left == 70 && a.leftChg);
        assert(a.right == 80 && !a.rightChg);
        assert(a.box == 60 && a.boxChg);

        Bytes proRight = proLeft;
        proRight[5] = 0x00;  // right broadcasts
        proRight[6] = 0xF9;  // cur (right)=90%, other (left)=unknown (15)
        proRight[7] = 0x0F;  // no charging, case unknown (15)
        assert(AirPodsBle::Parse(proRight.data(), proRight.size(), a));
        assert(!a.single && !a.leftBroadcasts);
        assert(a.left == -1 && !a.leftChg);
        assert(a.right == 90 && !a.rightChg);
        assert(a.box == -1 && !a.boxChg);
    }

    // 8. PickBestAdvert regression: stale 20s-old high-RSSI address must yield to fresh 1s-old address.
    {
        const std::uint64_t now = 100000;
        AirPodsBle::Advert staleStrong{};
        staleStrong.model = 0x200A;
        staleStrong.single = true;
        staleStrong.left = 80;
        staleStrong.rssi = -45;
        staleStrong.tick = now - 20000;  // 20s old -> outside 10s active window

        AirPodsBle::Advert freshWeaker{};
        freshWeaker.model = 0x200A;
        freshWeaker.single = true;
        freshWeaker.left = 70;
        freshWeaker.rssi = -60;
        freshWeaker.tick = now - 1000;   // 1s old -> inside 10s active window

        AirPodsBle::Advert best;
        assert(AirPodsBle::PickBestAdvert({ staleStrong, freshWeaker }, 0x200A, false, now, best));
        assert(best.left == 70 && best.rssi == -60);

        // Concurrent interference within 10s active window: strongest RSSI wins.
        AirPodsBle::Advert neighbor{};
        neighbor.model = 0x200A;
        neighbor.single = true;
        neighbor.left = 20;
        neighbor.rssi = -76;
        neighbor.tick = now - 500;
        assert(AirPodsBle::PickBestAdvert({ neighbor, freshWeaker }, 0x200A, false, now, best));
        assert(best.left == 70 && best.rssi == -60);

        // Both outside 10s active window (connected device 5-min fallback): newest tick wins.
        AirPodsBle::Advert olderStrong = staleStrong;
        olderStrong.tick = now - 60000;
        olderStrong.left = 90;
        AirPodsBle::Advert newerWeak = freshWeaker;
        newerWeak.tick = now - 25000;
        newerWeak.left = 60;
        assert(AirPodsBle::PickBestAdvert({ olderStrong, newerWeak }, 0x200A, true, now, best));
        assert(best.left == 60);
    }

    // 9. Dual-bud missing side merge rejects distant (>15 dB RSSI difference) stranger packets.
    {
        const std::uint64_t now = 100000;
        AirPodsBle::Advert localBud{};
        localBud.model = 0x2014;
        localBud.single = false;
        localBud.left = 80;
        localBud.right = -1;
        localBud.box = 50;
        localBud.rssi = -50;
        localBud.tick = now - 1000;

        AirPodsBle::Advert distantStranger{};
        distantStranger.model = 0x2014;
        distantStranger.single = false;
        distantStranger.left = -1;
        distantStranger.right = 10;
        distantStranger.box = -1;
        distantStranger.rssi = -78;  // 28 dB weaker -> rejected
        distantStranger.tick = now - 1500;

        AirPodsBle::Advert best;
        assert(AirPodsBle::PickBestAdvert({ localBud, distantStranger }, 0x2014, false, now, best));
        assert(best.left == 80 && best.right == -1 && best.box == 50);

        // Local other bud within 15 dB merges properly.
        distantStranger.rssi = -58;
        assert(AirPodsBle::PickBestAdvert({ localBud, distantStranger }, 0x2014, false, now, best));
        assert(best.left == 80 && best.right == 10 && best.box == 50);
    }

    std::puts("airpods_protocol_test: ok");
    return 0;
}
