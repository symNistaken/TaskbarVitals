// Captured frames from two receivers, decoded without hardware.
//   Compx 3554:F527   Zaopin Z2 Mini on its 8K receiver, FF02:0002, report 08
//   ROYUAN 3151:4011  MonsGeek M5W board on its 2.4 GHz dongle, FFFF:0002, F7
// See docs/zaopin_8k_receiver.md and docs/royuan_2.4g_keyboard.md.
//
// Self-contained on purpose: it defines the two decoders inline instead of
// including a header, so it compiles on its own and does not touch the build
// files. Build and run with:
//   g++ -std=c++17 -O2 -Wall -Wextra tests/receiver_frames_test.cpp -o build/receiver_frames_test
//   ./build/receiver_frames_test
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <vector>

namespace {

// --- Compx 3554:F527, Kysona framing, report 08, 17 bytes -------------------
// Already handled by src/drivers/kysona.cpp; decoded here to pin the captures.
// checksum = 0x55 - sum(bytes 0..15), modulo 256.
std::uint8_t KysonaChecksum(const std::uint8_t* b) {
    unsigned sum = 0;
    for (int i = 0; i < 16; i++) sum += b[i];
    return (std::uint8_t)(0x55 - sum);
}

struct KysonaBattery {
    int percent = -1;
    bool charging = false;
    int millivolts = 0;
    bool connected = false;
};

bool DecodeKysonaBattery(const std::uint8_t* r, std::size_t size, KysonaBattery& out) {
    if (!r || size < 17 || r[0] != 0x08 || r[1] != 0x04) return false;
    if (r[16] != KysonaChecksum(r)) return false;
    if (r[5] != 0x02) return false;  // the harness reports a connected mouse as 02
    if (r[6] < 1 || r[6] > 100) return false;
    KysonaBattery b;
    b.percent = r[6];
    b.charging = r[7] != 0;
    b.millivolts = (r[8] << 8) | r[9];
    b.connected = true;
    out = b;
    return true;
}

// --- ROYUAN 3151:4011, dongle status F7, 64-byte feature report -------------
// Request: report ID 0, command in the first payload byte, checksum in the
// eighth: 0xFF - sum(payload bytes 0..6), which is 0x08 for F7.
std::uint8_t RoyuanRequestChecksum(const std::uint8_t* payload) {
    unsigned sum = 0;
    for (int i = 0; i < 7; i++) sum += payload[i];
    return (std::uint8_t)(0xFF - (sum & 0xFF));
}

struct RoyuanStatus {
    bool answered = false;
    int percent = -1;
    bool rfReady = false;
};

// A 16-byte payload with the report ID still in front.
//
// Byte 4 is NOT an offline flag on this firmware: it reads 1 while the board is
// connected and reporting, so PollRongyuan's slot check would skip the keyboard.
// Byte 1 is not a bool either (it carried 0x34, the percentage). Charging is
// deliberately not decoded: the byte reads 1 with no cable attached.
bool DecodeRoyuanStatus(const std::uint8_t* r, std::size_t size, RoyuanStatus& out) {
    if (!r || size < 7) return false;
    RoyuanStatus s;
    s.answered = r[1] != 0;
    if (r[2] >= 1 && r[2] <= 100) s.percent = r[2];
    s.rfReady = r[6] != 0;
    out = s;
    return s.percent >= 0;
}

}  // namespace

int main() {
    // --- Compx 3554:F527 -----------------------------------------------------
    // 95%, discharging, 4080 mV. Byte 16 = 0x55 - sum(0..15), and sum was 0x6C.
    const std::vector<std::uint8_t> captured = {
        0x08, 0x04, 0x00, 0x00, 0x00, 0x02, 0x5F, 0x00, 0x0F, 0xF0,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xE9
    };
    assert(KysonaChecksum(captured.data()) == 0xE9);
    KysonaBattery b;
    assert(DecodeKysonaBattery(captured.data(), captured.size(), b));
    assert(b.percent == 95 && !b.charging && b.millivolts == 4080 && b.connected);

    // A second capture at 45%, 3837 mV, and its charging counterpart.
    std::vector<std::uint8_t> discharging = {
        0x08, 0x04, 0x00, 0x00, 0x00, 0x02, 0x2D, 0x00, 0x0E, 0xFD,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
    };
    discharging[16] = KysonaChecksum(discharging.data());
    assert(DecodeKysonaBattery(discharging.data(), discharging.size(), b));
    assert(b.percent == 45 && !b.charging && b.millivolts == 3837);

    auto charging = discharging;
    charging[7] = 1;
    charging[16] = KysonaChecksum(charging.data());
    assert(DecodeKysonaBattery(charging.data(), charging.size(), b));
    assert(b.percent == 45 && b.charging);

    // Rejections: wrong report id, wrong command echo, corrupt checksum,
    // wrong status byte, out-of-range level, short frame.
    for (int index : { 0, 1, 7, 16 }) {
        auto bad = captured;
        bad[index] ^= 0x01;
        assert(!DecodeKysonaBattery(bad.data(), bad.size(), b));
    }
    auto offline = captured;
    offline[5] = 0x00;
    offline[16] = KysonaChecksum(offline.data());
    assert(!DecodeKysonaBattery(offline.data(), offline.size(), b));
    auto empty = captured;
    empty[6] = 0x00;
    empty[16] = KysonaChecksum(empty.data());
    assert(!DecodeKysonaBattery(empty.data(), empty.size(), b));
    assert(!DecodeKysonaBattery(captured.data(), 16, b));
    assert(!DecodeKysonaBattery(nullptr, 0, b));

    // --- ROYUAN 3151:4011 ---------------------------------------------------
    std::vector<std::uint8_t> request(64, 0x00);
    request[0] = 0xF7;
    request[7] = RoyuanRequestChecksum(request.data());
    assert(request[7] == 0x08);

    // Captured reply: 52%, rf ready, byte 4 set while the board is connected.
    const std::vector<std::uint8_t> status = {
        0x00, 0x34, 0x34, 0x00, 0x01, 0x01, 0x01,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
    };
    RoyuanStatus s;
    assert(DecodeRoyuanStatus(status.data(), status.size(), s));
    assert(s.answered && s.percent == 52 && s.rfReady);

    // Byte 4 must not be read as an offline flag: setting or clearing it leaves
    // the reading intact, because this firmware sets it while connected.
    for (std::uint8_t flag : { 0x00, 0x01 }) {
        auto variant = status;
        variant[4] = flag;
        assert(DecodeRoyuanStatus(variant.data(), variant.size(), s));
        assert(s.percent == 52);
    }

    // A dongle holding no frame from the board answers 0: unknown, not empty.
    auto noFrame = status;
    noFrame[2] = 0x00;
    assert(!DecodeRoyuanStatus(noFrame.data(), noFrame.size(), s));
    auto tooHigh = status;
    tooHigh[2] = 101;
    assert(!DecodeRoyuanStatus(tooHigh.data(), tooHigh.size(), s));
    assert(!DecodeRoyuanStatus(status.data(), 6, s));

    std::puts("receiver_frames_test: ok");
}
