#pragma once
// Something that notices device activity by itself and asks for a refresh: an emitter (PR #535).
// New push channels (HID++ 2.0 battery events, Bluetooth GATT notifications, ...) are added as
// new IEventSource classes; the worker does not change.
#include <cstdint>

// Where an emitter sends its requests. The app debounces them into one poll.
struct IRefreshSink {
    virtual ~IRefreshSink() = default;
    // Poll in about `delayMs` (a just-connected device needs a moment before it answers).
    virtual void RequestRefresh(std::uint32_t delayMs) = 0;
};

struct IEventSource {
    virtual ~IEventSource() = default;
    virtual bool Start(IRefreshSink& sink) = 0;
    virtual void Stop() = 0;
    // The set of HID devices changed (arrival, removal, resume): reopen whatever is held.
    virtual void DevicesChanged() = 0;
};
