#pragma once
// Vendor knowledge for push events, split three ways so each vendor's PR reads top to bottom:
//
//   where   EventChannel     data: which HID collections and report ids carry unsolicited frames
//   layout  <vendor>_protocol header, shared with the poll driver: byte offsets live in one place
//   meaning DeviceSignal     vendor-neutral, optional fields: each decoder fills what it knows
//
// HidEventListener (the engine) never knows a vendor. To add one: write the protocol parser (with a
// captured-frame test), implement IHidEventDecoder in <vendor>_events.cpp next to its driver, and
// register its factory below and in event_decoders.cpp.
#include <cstdint>
#include <initializer_list>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "../hid.h"

// Where a vendor's unsolicited frames arrive. Empty lists match anything.
struct EventChannel {
    std::vector<USHORT> pids;
    std::vector<USHORT> usages;
    std::vector<std::uint8_t> reportIds;  // first byte of the frame; others never reach Decode
    USHORT vid = 0;
    USHORT usagePage = 0;
    USHORT minInputLen = 0;

    bool Matches(const HidColl& c) const {
        return c.vid == vid && c.page() == usagePage && c.inLen() >= minInputLen && Has(pids, c.pid) &&
               Has(usages, c.usage());
    }
    bool Accepts(const std::uint8_t* frame, std::size_t n) const { return n && Has(reportIds, frame[0]); }

private:
    template <class T, class U>
    static bool Has(const std::vector<T>& list, U v) {
        if (list.empty()) return true;
        for (T x : list)
            if (x == v) return true;
        return false;
    }
};

// What a frame says about one device. A field is set only when the frame carries it: a Logitech
// receiver says "slot 2 is back" (present), a device that pushes its battery fills percent and
// charging. Any change is reason to poll; a complete reading could later skip the poll.
struct DeviceSignal {
    std::wstring subject;  // which device on the channel (receiver slot, ...); empty = the only one
    std::optional<bool> present;
    std::optional<int> percent;
    std::optional<bool> charging;
    std::wstring what;  // for `--events`, e.g. "device 2 connected"
};

struct IHidEventDecoder {
    virtual ~IHidEventDecoder() = default;
    virtual const wchar_t* Name() const = 0;
    virtual const EventChannel& Channel() const = 0;
    // Called only for frames the channel accepts. Empty when the frame is not an event (a reply,
    // input, noise).
    virtual std::optional<DeviceSignal> Decode(const std::uint8_t* frame, std::size_t n) const = 0;
    // How long to wait after a signal before polling (a just-connected device needs a moment).
    virtual std::uint32_t SettleMs() const { return 1500; }
};

// Signals are edges, not levels. Vendor software (openlogi, G HUB, Solaar) asks Logitech receivers
// to re-announce paired devices every few seconds, and devices that push their battery repeat the
// same value; only a change is worth a poll. Applied by the engine to every decoder.
class SignalEdges {
public:
    // `channel` identifies where the signal came from (the collection path).
    bool IsChange(const std::wstring& channel, const DeviceSignal& s) {
        auto [it, inserted] = known_.emplace(channel + L"|" + s.subject, Known{});
        Known& k = it->second;
        bool changed = inserted;  // first word about this device: cannot know it is a repeat
        changed |= Update(k.present, s.present);
        changed |= Update(k.percent, s.percent);
        changed |= Update(k.charging, s.charging);
        return changed;
    }

private:
    struct Known {
        std::optional<bool> present;
        std::optional<int> percent;
        std::optional<bool> charging;
    };
    template <class T>
    static bool Update(std::optional<T>& known, const std::optional<T>& now) {
        if (!now || known == now) return false;
        known = now;
        return true;
    }
    std::map<std::wstring, Known> known_;
};

// Vendor decoders, each in <vendor>_events.cpp next to its driver.
std::unique_ptr<IHidEventDecoder> MakeLogitechEventDecoder();  // HID++ receiver connection events

// Every registered decoder, for the listener.
std::vector<std::unique_ptr<IHidEventDecoder>> HidEventDecoders();
