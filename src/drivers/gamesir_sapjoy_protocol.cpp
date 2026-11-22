#include "gamesir_sapjoy_protocol.h"

#include <cstring>

namespace GameSirSapjoy {

bool BuildDeviceInfoRequest(std::uint8_t* out, std::size_t cap) {
    if (!out || cap < kOutputReportLen) return false;
    std::memset(out, 0, kOutputReportLen);
    out[0] = kOutputReportId;
    out[1] = 0x03;                 // len: report id excluded, counts [1]..[3]
    out[2] = kCmdDeviceInfo;       // GetDeviceInfo
    out[3] = kSubDeviceInfo;       // sub 0x01
    return true;
}

Reading DecodeDeviceInfo(const std::uint8_t* reply, std::size_t size) {
    Reading r;
    // The pad streams other reports (state frames) on the same collection, so filter on every field
    // of the header rather than on the report id alone. [14] is the last byte we read, so a shorter
    // buffer is not a GetDeviceInfo reply.
    if (!reply || size < 15) return r;
    if (reply[0] != kInputReportId) return r;
    if (reply[2] != kCmdDeviceInfo || reply[3] != kSubDeviceInfo) return r;

    if (reply[1] < 14 || static_cast<std::size_t>(reply[1]) + 1 > size) return r;
    for (std::size_t i = 0; i < r.address.size(); ++i) r.address[i] = reply[7+i];
    r.answered = true;
    r.deviceId = reply[4];
    r.deviceType = reply[14];
    const std::uint8_t battery = reply[13];
    if (battery == 0xFF) {
        r.charging = true;         // no percentage while charging, the caller keeps the last value
        return r;
    }
    r.percent = battery > 100 ? 100 : battery;   // the vendor app clamps above 100
    return r;
}

}  // namespace GameSirSapjoy
