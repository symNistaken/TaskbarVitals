#include "hid_listener.h"

namespace {

std::wstring HexReport(const BYTE* d, DWORD n) {
    static const wchar_t digits[] = L"0123456789ABCDEF";
    std::wstring s;
    for (DWORD i = 0; i < n; ++i) {
        if (i) s += L' ';
        s += digits[d[i] >> 4];
        s += digits[d[i] & 15];
    }
    return s;
}

}  // namespace

// One collection held open with a read always pending on it.
struct HidEventListener::Reader {
    HidColl coll;
    const IHidEventDecoder* decoder = nullptr;
    HANDLE h = INVALID_HANDLE_VALUE;
    OVERLAPPED ov{};
    std::vector<BYTE> buf;

    bool Open(const HidColl& c, const IHidEventDecoder* d) {
        coll = c;
        decoder = d;
        h = CreateFileW(c.path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
                        FILE_FLAG_OVERLAPPED, nullptr);
        ov.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        buf.assign(c.inLen(), 0);
        return h != INVALID_HANDLE_VALUE && ov.hEvent && Arm();
    }
    // Queues the next read. A read that completes at once still signals the event.
    bool Arm() {
        ResetEvent(ov.hEvent);
        return ReadFile(h, buf.data(), (DWORD)buf.size(), nullptr, &ov) || GetLastError() == ERROR_IO_PENDING;
    }
    ~Reader() {
        if (h != INVALID_HANDLE_VALUE) {
            DWORD n;
            CancelIoEx(h, &ov);
            GetOverlappedResult(h, &ov, &n, TRUE);  // the buffer must outlive the cancelled read
            CloseHandle(h);
        }
        if (ov.hEvent) CloseHandle(ov.hEvent);
    }
};

HidEventListener::HidEventListener(std::vector<std::unique_ptr<IHidEventDecoder>> decoders, Log log)
    : decoders_(std::move(decoders)), log_(std::move(log)) {}

HidEventListener::~HidEventListener() { Stop(); }

bool HidEventListener::Start(IRefreshSink& sink) {
    if (thread_) return true;
    sink_ = &sink;
    stop_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    rescan_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    thread_ = stop_ && rescan_ ? CreateThread(nullptr, 0, ThreadMain, this, 0, nullptr) : nullptr;
    return thread_ != nullptr;
}

void HidEventListener::Stop() {
    if (thread_) {
        SetEvent(stop_);
        WaitForSingleObject(thread_, 5000);
        CloseHandle(thread_);
        thread_ = nullptr;
    }
    if (stop_) CloseHandle(stop_);
    if (rescan_) CloseHandle(rescan_);
    stop_ = rescan_ = nullptr;
}

void HidEventListener::DevicesChanged() {
    if (rescan_) SetEvent(rescan_);  // auto-reset: a burst of arrivals is one reopen
}

DWORD WINAPI HidEventListener::ThreadMain(LPVOID self) {
    static_cast<HidEventListener*>(self)->Run();
    return 0;
}

void HidEventListener::Run() {
    Reopen();
    for (;;) {
        std::vector<HANDLE> hs = { stop_, rescan_ };
        for (auto& r : readers_) hs.push_back(r->ov.hEvent);
        DWORD w = WaitForMultipleObjects((DWORD)hs.size(), hs.data(), FALSE, INFINITE);
        if (w == WAIT_OBJECT_0) break;
        if (w == WAIT_OBJECT_0 + 1) {
            Reopen();
            continue;
        }
        size_t i = w - WAIT_OBJECT_0 - 2;
        if (i >= readers_.size()) break;  // WAIT_FAILED: should not happen, do not spin
        Reader& r = *readers_[i];
        DWORD n = 0;
        bool read = GetOverlappedResult(r.h, &r.ov, &n, FALSE) != FALSE;
        if (read && n) OnReport(r, n);
        // A failed read means the device went away; drop it rather than spin on the error.
        // DevicesChanged reopens whatever is still there.
        if (!read || !r.Arm()) readers_.erase(readers_.begin() + i);
    }
    CloseAll();
}

void HidEventListener::Reopen() {
    CloseAll();
    for (const HidColl& c : EnumHid()) {
        for (const auto& d : decoders_) {
            if (!d->Channel().Matches(c)) continue;
            if (readers_.size() >= MAXIMUM_WAIT_OBJECTS - 2) return;
            auto r = std::make_unique<Reader>();
            if (r->Open(c, d.get())) {
                if (log_)
                    log_(std::wstring(L"listening: ") + d->Name() + L"  " + Hex4(c.vid) + L":" + Hex4(c.pid) + L"  " +
                         Hex4(c.page()) + L":" + Hex4(c.usage()));
                readers_.push_back(std::move(r));
            }
            break;  // one decoder per collection
        }
    }
}

void HidEventListener::CloseAll() { readers_.clear(); }

void HidEventListener::OnReport(const Reader& r, DWORD n) {
    const BYTE* frame = r.buf.data();
    std::optional<DeviceSignal> s;
    if (r.decoder->Channel().Accepts(frame, n)) s = r.decoder->Decode(frame, n);
    bool change = s && edges_.IsChange(r.coll.path, *s);
    if (log_) {
        std::wstring what = s ? L"  -> " + s->what + (change ? L"" : L" (repeat, ignored)") : L"";
        log_(Hex4(r.coll.pid) + L"  " + HexReport(frame, n) + what);
    }
    if (change) sink_->RequestRefresh(r.decoder->SettleMs());
}
