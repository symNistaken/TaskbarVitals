// Microsoft Phone Link stores a localized Adaptive Card in its package folder.
// The card contains the phone connection status and battery information.
// Supports English and Japanese connection messages and battery tooltips.
// Reads localized fields from the JSON card layout; rejects malformed or ambiguous readings.
// The file is an undocumented Phone Link implementation detail.
// Read-only, no device or network access.
#include "drivers.h"
#include "phonelink_protocol.h"

namespace {

// A card that has not been rewritten for this long is shown dimmed, as a last known value.
constexpr ULONGLONG kStaleSec = 3 * 60 * 60;

bool ReadUtf8File(const std::wstring& path, std::wstring& out, FILETIME& written) {
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER size{};
    bool ok = GetFileSizeEx(h, &size) && size.QuadPart > 0 && size.QuadPart < 4 * 1024 * 1024 && GetFileTime(h, nullptr, nullptr, &written);
    std::string bytes;
    if (ok) {
        bytes.resize((size_t)size.QuadPart);
        DWORD got = 0;
        ok = ReadFile(h, bytes.data(), (DWORD)bytes.size(), &got, nullptr) && got == bytes.size();
    }
    CloseHandle(h);
    if (!ok) return false;
    int n = MultiByteToWideChar(CP_UTF8, 0, bytes.data(), (int)bytes.size(), nullptr, 0);
    if (n <= 0) return false;
    out.resize(n);
    MultiByteToWideChar(CP_UTF8, 0, bytes.data(), (int)bytes.size(), out.data(), n);
    return true;
}
}  // namespace

void PollPhoneLink(PollContext& ctx) {
    wchar_t base[MAX_PATH];
    DWORD n = GetEnvironmentVariableW(L"LOCALAPPDATA", base, MAX_PATH);
    if (!n || n >= MAX_PATH) return;
    std::wstring path = std::wstring(base) + L"\\Packages\\Microsoft.YourPhone_8wekyb3d8bbwe\\LocalState\\StartMenu\\StartMenuCompanion.json";

    std::wstring json;
    FILETIME written{};

    if (!ReadUtf8File(path, json, written))
        return;

    PhoneLink::Reading reading;
    if (!PhoneLink::Parse(json, reading))
        return;

    FILETIME now{};
    GetSystemTimeAsFileTime(&now);
    ULARGE_INTEGER a{}, b{};
    a.LowPart = now.dwLowDateTime, a.HighPart = now.dwHighDateTime;
    b.LowPart = written.dwLowDateTime, b.HighPart = written.dwHighDateTime;
    ULONGLONG ageSec = a.QuadPart > b.QuadPart ? (a.QuadPart - b.QuadPart) / 10000000ull : 0;

    DeviceStatus s;
    s.key = L"phonelink";
    s.name = reading.name;
    s.via = L"Phone Link";
    s.kind = DeviceKind::Phone;
    s.percent = reading.percent;
    s.charging = reading.charging;
    s.asleep = ageSec > kStaleSec;

    ctx.out.push_back(s);
}
