#pragma once
// Recognises the HID++ frames a Logitech receiver sends on its own (not as a reply), and parses
// the receiver's notification flags register. Header-only and portable so it can be unit tested.
//
// Device connection, HID++ 1.0 sub id 0x41, sent by Unifying / Bolt / LIGHTSPEED receivers:
//   [0x10][device index 1..6][0x41][protocol][flags][wpid lo][wpid hi]
//   flags bit 6 set = link not established (device switched off or out of range).
// The receiver sends it only while bit 0x000100 ("wireless") of register 0x00 is set. This app
// never writes that register (read-only rule); G HUB, Options+ and Solaar set it.
// Sources: Solaar (hidpp10 notifications), openlogi-hidpp receiver.rs (NotificationFlags).
#include <cstddef>
#include <cstdint>

namespace hidpp {

struct Notification {
    enum class Kind { None, DeviceConnection } kind = Kind::None;
    std::uint8_t index = 0;  // device index on the receiver
    bool linkUp = false;
};

inline bool IsHidppReport(const std::uint8_t* r, std::size_t n) { return n >= 7 && (r[0] == 0x10 || r[0] == 0x11); }

inline Notification Classify(const std::uint8_t* r, std::size_t n) {
    Notification out;
    // 0x41 is not a register-access sub id (0x80..0x83), so a reply to anyone's request,
    // including our own polls, can never look like this: no feedback loop.
    if (!IsHidppReport(r, n) || r[1] < 1 || r[1] > 6 || r[2] != 0x41) return out;
    out.kind = Notification::Kind::DeviceConnection;
    out.index = r[1];
    out.linkUp = (r[4] & 0x40) == 0;
    return out;
}

constexpr std::uint32_t kWirelessNotifications = 0x000100;

// Request for register 0x00 of the receiver itself (index 0xFF): a read, never a write.
inline void NotificationFlagsRequest(std::uint8_t out[7]) {
    const std::uint8_t req[7] = { 0x10, 0xFF, 0x81, 0x00, 0, 0, 0 };
    for (int i = 0; i < 7; ++i) out[i] = req[i];
}

// Parses the reply to NotificationFlagsRequest. Returns false for any other frame.
inline bool ParseNotificationFlags(const std::uint8_t* r, std::size_t n, std::uint32_t& flags) {
    if (!IsHidppReport(r, n) || r[1] != 0xFF || r[2] != 0x81 || r[3] != 0x00) return false;
    flags = (std::uint32_t)r[4] << 16 | (std::uint32_t)r[5] << 8 | r[6];
    return true;
}

}  // namespace hidpp
