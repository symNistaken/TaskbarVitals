// Logitech push events: device-connection notifications from Unifying / Bolt / LIGHTSPEED
// receivers. Frame layout and the notification-flag caveat: hidpp_events.h.
#include "event_decoders.h"
#include "hidpp_events.h"

namespace {

class LogitechEventDecoder final : public IHidEventDecoder {
public:
    const wchar_t* Name() const override { return L"Logitech HID++"; }

    const EventChannel& Channel() const override {
        static const EventChannel channel = [] {
            EventChannel c;
            c.vid = 0x046D;
            c.usagePage = 0xFF00;
            c.usages = { 0x0001, 0x0002 };  // short and long HID++ collections
            c.reportIds = { 0x10, 0x11 };   // short and long HID++ reports
            c.minInputLen = 7;
            return c;
        }();
        return channel;
    }

    std::optional<DeviceSignal> Decode(const std::uint8_t* r, std::size_t n) const override {
        hidpp::Notification ev = hidpp::Classify(r, n);
        if (ev.kind != hidpp::Notification::Kind::DeviceConnection) return std::nullopt;
        DeviceSignal s;
        s.subject = std::to_wstring(ev.index);
        s.present = ev.linkUp;
        s.what = L"device " + s.subject + (ev.linkUp ? L" connected" : L" disconnected");
        return s;
    }
};

}  // namespace

std::unique_ptr<IHidEventDecoder> MakeLogitechEventDecoder() { return std::make_unique<LogitechEventDecoder>(); }
