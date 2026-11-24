#pragma once
// Emitter for push-capable HID devices: holds the collections the decoders want open, read-only,
// with a read always pending, and asks for a refresh when a decoder recognises an event (for
// example a mouse switched on behind a receiver that stays plugged in, which Windows does not
// report). Vendor-neutral: all protocol knowledge is in the IHidEventDecoder implementations.
//
// Each open handle gets its own copy of every input report, so listening takes nothing away from
// the poller or from vendor software. Nothing is ever written to a device.
#include <windows.h>

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "drivers/event_decoders.h"
#include "event_source.h"

class HidEventListener final : public IEventSource {
public:
    using Log = std::function<void(const std::wstring&)>;
    // `log`, when set, receives every report read and what was made of it (for `--events`).
    explicit HidEventListener(std::vector<std::unique_ptr<IHidEventDecoder>> decoders, Log log = {});
    ~HidEventListener() override;
    HidEventListener(const HidEventListener&) = delete;
    HidEventListener& operator=(const HidEventListener&) = delete;

    bool Start(IRefreshSink& sink) override;
    void Stop() override;
    void DevicesChanged() override;

private:
    struct Reader;
    static DWORD WINAPI ThreadMain(LPVOID self);
    void Run();
    void Reopen();
    void CloseAll();
    void OnReport(const Reader& r, DWORD n);

    std::vector<std::unique_ptr<IHidEventDecoder>> decoders_;
    SignalEdges edges_;  // survives Reopen, so a reopened device's first repeat is not news
    Log log_;
    IRefreshSink* sink_ = nullptr;
    HANDLE thread_ = nullptr, stop_ = nullptr, rescan_ = nullptr;
    std::vector<std::unique_ptr<Reader>> readers_;  // touched only by the listener thread
};
