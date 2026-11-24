#include "update_win.h"
#include "app_version.h"
#include "update_apply.h"
#include "update_signature.h"
#include "i18n.h"
#include <winhttp.h>
#include <bcrypt.h>
#include <shellapi.h>
#include <commctrl.h>
#include <atomic>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

namespace updates {
namespace {
constexpr const wchar_t* Settings = L"Software\\WirelessBattery";
constexpr const wchar_t* Cache = L"Software\\WirelessBattery\\Updates";
enum class Operation { Idle, Checking, Downloading, Installing };
struct Result {
    ReleaseInfo release;
    std::wstring error;
    bool success = false;
    bool metadataValid = false;
    std::uint64_t retry = 0;
    std::wstring file;
};
Operation operation = Operation::Idle;
ReleaseInfo known;
CheckSchedule schedule;
bool automatic = true, manual = false, dialogOpen = false;
ULONGLONG started = 0;
std::thread worker;
std::mutex resultMutex;
Result result;
std::atomic<bool> done{false}, cancelled{false};
std::atomic<bool> shuttingDown{false};
std::atomic<unsigned> progress{0};
std::wstring lastError;
HANDLE applyProcess = nullptr;

void Cancel() { cancelled = true; }

HWND DialogOwner(HWND owner) {
    // The application's main window is hidden; TaskDialog requires a visible, enabled parent.
    return IsWindowVisible(owner) && IsWindowEnabled(owner) ? owner : nullptr;
}

void ShowMessage(HWND owner, const wchar_t* text) {
    if (dialogOpen || shuttingDown) return;
    dialogOpen = true;
    MessageBoxW(DialogOwner(owner), T(text), T(L"Taskbar Vitals update"), MB_OK | MB_ICONINFORMATION);
    dialogOpen = false;
}

std::uint64_t Now() {
    FILETIME ft;
    GetSystemTimeAsFileTime(&ft);
    ULARGE_INTEGER value;
    value.LowPart = ft.dwLowDateTime;
    value.HighPart = ft.dwHighDateTime;
    return value.QuadPart / 10000000 - 11644473600ULL; // Unix UTC, matching GitHub rate-limit headers.
}

std::wstring Widen(const std::string& s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.data(), (int)s.size(), nullptr, 0);
    std::wstring out(n, 0);
    if (n) MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.data(), (int)s.size(), out.data(), n);
    return out;
}

std::uint64_t ReadTime(const wchar_t* name) {
    ULONGLONG value = 0;
    DWORD size = sizeof(value);
    RegGetValueW(HKEY_CURRENT_USER, Cache, name, RRF_RT_REG_QWORD, nullptr, &value, &size);
    return value;
}

void SaveTime(const wchar_t* name, std::uint64_t value) {
    RegSetKeyValueW(HKEY_CURRENT_USER, Cache, name, REG_QWORD, &value, sizeof(value));
}

void SaveSchedule() {
    SaveTime(L"LastAttempt", schedule.lastAttempt);
    SaveTime(L"NextAutomatic", schedule.nextAutomatic);
    SaveTime(L"RetryAfter", schedule.retryAfter);
}

std::wstring Header(HINTERNET req, DWORD kind, const wchar_t* custom = nullptr) {
    DWORD bytes = 0;
    WinHttpQueryHeaders(req, kind, custom, nullptr, &bytes, WINHTTP_NO_HEADER_INDEX);
    if (!bytes || bytes > 8192) return {};
    std::wstring text(bytes / sizeof(wchar_t), 0);
    if (!WinHttpQueryHeaders(req, kind, custom, text.data(), &bytes, WINHTTP_NO_HEADER_INDEX)) return {};
    text.resize(bytes / sizeof(wchar_t));
    while (!text.empty() && text.back() == 0) text.pop_back();
    return text;
}

std::uint64_t Decimal(const std::wstring& text) {
    if (text.empty() || text.size() > 18) return 0;
    std::uint64_t out = 0;
    for (wchar_t c : text) { if (c < L'0' || c > L'9') return 0; out = out * 10 + c - L'0'; }
    return out;
}

// Synchronous handles belong exclusively to this worker. Cancellation is
// cooperative between finite-timeout calls; closing them concurrently is unsafe.
Result FetchRelease() {
    Result out;
    ULONGLONG deadline = GetTickCount64() + 30000;
    HINTERNET session = WinHttpOpen(L"WirelessBattery/" WB_VERSION_WSTRING, WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                                   WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session) { out.error = L"Could not initialize the Windows network connection."; return out; }
    if (!WinHttpSetTimeouts(session, 5000, 5000, 5000, 5000)) {
        out.error = L"Could not configure the Windows network connection.";
        WinHttpCloseHandle(session);
        return out;
    }
    HINTERNET connection = WinHttpConnect(session, L"api.github.com", INTERNET_DEFAULT_HTTPS_PORT, 0);
    HINTERNET req = connection ? WinHttpOpenRequest(connection, L"GET",
        L"/repos/symNistaken/TaskbarVitals/releases/latest", nullptr,
        WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE) : nullptr;
    DWORD redirect = WINHTTP_OPTION_REDIRECT_POLICY_NEVER;
    bool configured = req && WinHttpSetOption(req, WINHTTP_OPTION_REDIRECT_POLICY, &redirect, sizeof(redirect));
    const wchar_t* headers = L"Accept: application/vnd.github+json\r\nX-GitHub-Api-Version: 2022-11-28\r\n";
    if (configured && !cancelled && WinHttpSendRequest(req, headers, (DWORD)-1, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
        !cancelled && GetTickCount64() < deadline && WinHttpReceiveResponse(req, nullptr)) {
        DWORD status = 0, size = sizeof(status);
        WinHttpQueryHeaders(req, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, nullptr, &status, &size, nullptr);
        if (status == 404) out.success = true;
        else if (status == 403 || status == 429) {
            auto now = Now();
            auto delay = Decimal(Header(req, WINHTTP_QUERY_CUSTOM, L"Retry-After"));
            auto reset = Decimal(Header(req, WINHTTP_QUERY_CUSTOM, L"X-RateLimit-Reset"));
            out.retry = now + (delay && delay <= 86400 ? delay : 3600);
            if (reset > out.retry && reset - now <= 86400) out.retry = reset;
            out.error = L"GitHub temporarily limited update checks. Please try again later.";
        } else if (status == 200) {
            std::string body;
            char buffer[8192];
            DWORD bytes = 0;
            bool complete = false;
            while (!cancelled && GetTickCount64() < deadline) {
                if (!WinHttpReadData(req, buffer, sizeof(buffer), &bytes)) break;
                if (!bytes) { complete = true; break; }
                body.append(buffer, bytes);
                if (body.size() > 1024 * 1024) break;
            }
            Version current;
            ParseVersion(appversion::text, current);
            if (complete && ParseRelease(body, current, out.release) != ReleaseResult::Invalid) out.success = true;
            else out.error = L"GitHub returned invalid update information. Please try again later.";
        } else out.error = L"GitHub could not provide update information. Please try again later.";
    }
    if (!out.success && out.error.empty()) out.error = cancelled ? L"Update check cancelled or timed out." :
        L"Could not check for updates. Check your Internet connection and try again.";
    out.metadataValid = out.success;
    if (req) WinHttpCloseHandle(req);
    if (connection) WinHttpCloseHandle(connection);
    WinHttpCloseHandle(session);
    return out;
}

void StartCheck(bool requested) {
    if (Busy() || shuttingDown) return;
    manual = requested;
    cancelled = false;
    done = false;
    started = GetTickCount64();
    operation = Operation::Checking;
    worker = std::thread([] {
        Result fetched = FetchRelease();
        { std::lock_guard<std::mutex> lock(resultMutex); result = std::move(fetched); }
        done = true;
    });
}

void SaveKnown() {
    if (known.tag.empty()) { RegDeleteKeyValueW(HKEY_CURRENT_USER, Cache, L"AvailableVersion"); return; }
    // Cache only the version, never an executable URL or digest trusted for execution.
    auto tag = Widen(known.tag);
    RegSetKeyValueW(HKEY_CURRENT_USER, Cache, L"AvailableVersion", REG_SZ, tag.c_str(), (DWORD)((tag.size() + 1) * sizeof(wchar_t)));
}

void ShowAvailable(HWND owner);

// Removes this app's download folders (`prefix` + 32 hex digits) in `root` that are over a day old.
void CleanDownloadsIn(const std::wstring& root, const std::wstring& prefix) {
    if (root.empty()) return;
    WIN32_FIND_DATAW entry;
    HANDLE search = FindFirstFileW((root + prefix + L"*").c_str(), &entry);
    if (search == INVALID_HANDLE_VALUE) return;
    do {
        std::wstring name(entry.cFileName);
        if (!(entry.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || (entry.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) ||
            name.size() != prefix.size() + 32 || name.compare(0, prefix.size(), prefix) != 0) continue;
        bool hex = true;
        for (size_t i = prefix.size(); i < name.size(); ++i)
            hex &= (name[i] >= L'0' && name[i] <= L'9') || (name[i] >= L'a' && name[i] <= L'f');
        ULARGE_INTEGER written;
        written.LowPart = entry.ftLastWriteTime.dwLowDateTime;
        written.HighPart = entry.ftLastWriteTime.dwHighDateTime;
        auto seconds = written.QuadPart / 10000000 - 11644473600ULL;
        if (!hex || seconds > Now() || Now() - seconds < 86400) continue;
        auto directory = root + name;
        for (const wchar_t* file : { L"\\update-setup.part", L"\\update-setup.exe", L"\\update-helper.exe" })
            DeleteFileW((directory + file).c_str());
        RemoveDirectoryW(directory.c_str()); // Non-recursive; unknown files keep the directory intact.
    } while (FindNextFileW(search, &entry));
    FindClose(search);
}

void CleanOldDownloads() {
    auto root = DownloadRoot();
    if (!root.empty()) CleanDownloadsIn(root + L"\\", L"");
    // Version 1.1.0 downloaded to %TEMP%\WirelessBattery-update-<hex>; remove what it left behind.
    wchar_t temporary[32768];
    DWORD length = GetTempPathW(ARRAYSIZE(temporary), temporary);
    if (length && length < ARRAYSIZE(temporary)) CleanDownloadsIn(temporary, L"WirelessBattery-update-");
}
}

void Initialize() {
    CleanOldDownloads();
    started = GetTickCount64();
    DWORD enabled = 1, size = sizeof(enabled);
    RegGetValueW(HKEY_CURRENT_USER, Settings, L"AutoCheckUpdates", RRF_RT_REG_DWORD, nullptr, &enabled, &size);
    automatic = enabled != 0;
    if (!appversion::official) return;
    schedule = { ReadTime(L"LastAttempt"), ReadTime(L"NextAutomatic"), ReadTime(L"RetryAfter") };
    wchar_t tag[64]{};
    size = sizeof(tag);
    Version current, cached;
    ParseVersion(appversion::text, current);
    if (RegGetValueW(HKEY_CURRENT_USER, Cache, L"AvailableVersion", RRF_RT_REG_SZ, nullptr, tag, &size) == ERROR_SUCCESS) {
        std::string ascii;
        for (wchar_t c : tag) { if (!c) break; if (c > 127) { ascii.clear(); break; } ascii += (char)c; }
        if (ParseVersion(ascii, cached) && Newer(cached, current)) {
            known.version = cached;
            known.tag = ascii;
            known.page = std::string("https://github.com/") + Repository + "/releases/tag/" + ascii;
        }
    }
}

void Tick(HWND owner) {
    if (dialogOpen || shuttingDown) return;
    if (operation == Operation::Installing) {
        DWORD status = WaitForSingleObject(applyProcess, 0);
        if (status != WAIT_TIMEOUT) {
            CloseHandle(applyProcess);
            applyProcess = nullptr;
            operation = Operation::Idle;
            if (status == WAIT_FAILED) lastError = L"Could not monitor the update installer.";
        }
        return;
    }
    if (operation == Operation::Downloading) return; // The explicit progress dialog owns completion.
    if (operation != Operation::Idle && !done && GetTickCount64() - started >= 30000) Cancel();
    if (done.exchange(false)) {
        if (worker.joinable()) worker.join();
        Result completed;
        { std::lock_guard<std::mutex> lock(resultMutex); completed = std::move(result); }
        operation = Operation::Idle;
        schedule.Finished(Now(), completed.success, completed.retry);
        SaveSchedule();
        lastError = completed.error;
        if (completed.success) { known = completed.release; SaveKnown(); }
        if (manual) {
            manual = false;
            if (!completed.success) ShowMessage(owner, lastError.c_str());
            else if (!known.tag.empty()) ShowAvailable(owner);
            else ShowMessage(owner, L"Taskbar Vitals is up to date.");
        }
    }
    if (appversion::official && automatic && operation == Operation::Idle && GetTickCount64() - started >= 30000 && schedule.Due(Now()))
        StartCheck(false);
}

void Shutdown() {
    shuttingDown = true;
    Cancel();
    if (worker.joinable()) worker.join();
    if (applyProcess) { CloseHandle(applyProcess); applyProcess = nullptr; }
    operation = Operation::Idle;
}

bool Busy() { return dialogOpen || operation != Operation::Idle; }
bool AutomaticEnabled() { return automatic && appversion::official; }
bool AutomaticSetting() { return automatic; }
bool Supported() { return appversion::official; }
void ToggleAutomatic() {
    if (!appversion::official) return;
    automatic = !automatic;
    DWORD value = automatic;
    RegSetKeyValueW(HKEY_CURRENT_USER, Settings, L"AutoCheckUpdates", REG_DWORD, &value, sizeof(value));
}
void SetAutomaticSetting(bool on) {
    automatic = on;
    DWORD value = automatic ? 1 : 0;
    RegSetKeyValueW(HKEY_CURRENT_USER, Settings, L"AutoCheckUpdates", REG_DWORD, &value, sizeof(value));
}

std::wstring MenuLabel() {
    if (operation == Operation::Installing) return T(L"Installing update...");
    if (operation == Operation::Checking) return T(L"Checking for updates...");
    if (!known.tag.empty()) return TF(L"Update available: {version}...", { { L"version", Widen(known.tag) } });
    return lastError.empty() ? T(L"Check for updates...") : T(L"Update check failed - try again...");
}

void Activate(HWND owner) {
    if (Busy() || shuttingDown) return;
    if (!appversion::official) {
        ShowMessage(owner, L"This is a development build. Automatic release updates are disabled to preserve local changes.");
        return;
    }
    if (!known.tag.empty()) { ShowAvailable(owner); return; }
    if (!schedule.ManualAllowed(Now())) {
        ShowMessage(owner, L"Please wait before checking again. GitHub may temporarily limit update checks.");
        return;
    }
    StartCheck(true);
}

std::wstring DownloadRoot() {
    wchar_t local[32768];
    DWORD n = GetEnvironmentVariableW(L"LOCALAPPDATA", local, ARRAYSIZE(local));
    if (!n || n >= ARRAYSIZE(local)) return {};
    std::wstring folder = std::wstring(local) + L"\\WirelessBattery";
    for (const wchar_t* part : { L"", L"\\updates" }) {
        folder += part;
        if (!CreateDirectoryW(folder.c_str(), nullptr) && GetLastError() != ERROR_ALREADY_EXISTS) return {};
        DWORD attributes = GetFileAttributesW(folder.c_str());
        if (attributes == INVALID_FILE_ATTRIBUTES || !(attributes & FILE_ATTRIBUTE_DIRECTORY) ||
            (attributes & FILE_ATTRIBUTE_REPARSE_POINT)) return {};
    }
    return folder;
}

std::wstring ExecutablePath() {
    std::vector<wchar_t> path(32768);
    DWORD n = GetModuleFileNameW(nullptr, path.data(), (DWORD)path.size());
    return n && n < path.size() ? std::wstring(path.data(), n) : L"";
}

namespace {
std::wstring CanonicalDirectory(const std::wstring& directory) {
    HANDLE h = CreateFileW(directory.c_str(), 0, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    if (h == INVALID_HANDLE_VALUE) return {};
    std::vector<wchar_t> buffer(32768);
    DWORD size = GetFinalPathNameByHandleW(h, buffer.data(), (DWORD)buffer.size(), FILE_NAME_NORMALIZED);
    CloseHandle(h);
    if (!size || size >= buffer.size()) return {};
    std::wstring path(buffer.data(), size);
    while (path.size() > 4 && path.back() == L'\\') path.pop_back();
    return path;
}

std::wstring NewDownloadDirectory() {
    std::wstring root = DownloadRoot();
    if (root.empty()) return {};
    unsigned char bytes[16];
    if (BCryptGenRandom(nullptr, bytes, sizeof(bytes), BCRYPT_USE_SYSTEM_PREFERRED_RNG) < 0) return {};
    std::wstring directory = root + L"\\";
    for (auto b : bytes) { directory += L"0123456789abcdef"[b >> 4]; directory += L"0123456789abcdef"[b & 15]; }
    return CreateDirectoryW(directory.c_str(), nullptr) ? directory : L"";
}

// HTTPS GET of a release file: only allowed GitHub hosts, at most 5 redirects, each followed by
// hand so every hop is checked, and exactly `expected` bytes handed to `write`.
bool HttpGet(std::wstring url, std::uint64_t expected, ULONGLONG deadline, bool showProgress,
             const std::function<bool(const char*, DWORD)>& write) {
    HINTERNET session = WinHttpOpen(L"WirelessBattery/" WB_VERSION_WSTRING, WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
        WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (session && !WinHttpSetTimeouts(session, 5000, 5000, 5000, 5000)) {
        WinHttpCloseHandle(session);
        session = nullptr;
    }
    std::uint64_t total = 0;
    bool complete = false;
    for (unsigned redirects = 0; session && !cancelled && redirects <= 5; ++redirects) {
        if (!AllowedDownloadUrl(url) || GetTickCount64() >= deadline) break;
        URL_COMPONENTS parts{ sizeof(parts) };
        parts.dwHostNameLength = parts.dwUrlPathLength = parts.dwExtraInfoLength = (DWORD)-1;
        if (!WinHttpCrackUrl(url.c_str(), (DWORD)url.size(), 0, &parts) || parts.nScheme != INTERNET_SCHEME_HTTPS) break;
        std::wstring host(parts.lpszHostName, parts.dwHostNameLength);
        std::wstring path(parts.lpszUrlPath, parts.dwUrlPathLength);
        if (parts.dwExtraInfoLength) path.append(parts.lpszExtraInfo, parts.dwExtraInfoLength);
        HINTERNET connection = WinHttpConnect(session, host.c_str(), parts.nPort, 0);
        HINTERNET req = connection ? WinHttpOpenRequest(connection, L"GET", path.c_str(), nullptr,
            WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE) : nullptr;
        DWORD policy = WINHTTP_OPTION_REDIRECT_POLICY_NEVER;
        bool configured = req && WinHttpSetOption(req, WINHTTP_OPTION_REDIRECT_POLICY, &policy, sizeof(policy));
        DWORD status = 0, size = sizeof(status);
        bool sent = configured && !cancelled && WinHttpSendRequest(req, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
            WINHTTP_NO_REQUEST_DATA, 0, 0, 0) && !cancelled && GetTickCount64() < deadline && WinHttpReceiveResponse(req, nullptr);
        if (sent) WinHttpQueryHeaders(req, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, nullptr, &status, &size, nullptr);
        std::wstring next;
        if (sent && (status == 301 || status == 302 || status == 303 || status == 307 || status == 308)) {
            next = Header(req, WINHTTP_QUERY_LOCATION);
        } else if (sent && status == 200) {
            std::wstring length = Header(req, WINHTTP_QUERY_CONTENT_LENGTH);
            bool validLength = length.empty() || Decimal(length) == expected;
            char buffer[32768];
            DWORD read = 0;
            while (validLength && !cancelled && GetTickCount64() < deadline) {
                if (!WinHttpReadData(req, buffer, sizeof(buffer), &read)) break;
                if (!read) { complete = total == expected; break; }
                if (total + read > expected || total + read > MaxDownloadBytes) break;
                if (!write(buffer, read)) break;
                total += read;
                if (showProgress) progress = (unsigned)(total * 100 / expected);
            }
        }
        if (req) WinHttpCloseHandle(req);
        if (connection) WinHttpCloseHandle(connection);
        if (next.empty()) break;
        url = std::move(next);
    }
    if (session) WinHttpCloseHandle(session);
    return complete && !cancelled;
}

bool GetText(const std::string& url, std::uint64_t expected, ULONGLONG deadline, std::string& out) {
    out.clear();
    return HttpGet(Widen(url), expected, deadline, false, [&](const char* data, DWORD n) {
        out.append(data, n);
        return true;
    });
}

Result DownloadRelease() {
    Result out = FetchRelease(); // Never use cached download metadata for execution.
    if (!out.success) return out;
    out.success = false;
    if (out.release.tag.empty()) { out.error = L"This update is no longer available. Check for updates again."; return out; }
    if (!out.release.CanInstall()) { out.error = L"This release has no signed Windows installer yet. Please use Release notes to download it manually."; return out; }
    ULONGLONG deadline = GetTickCount64() + 600000;
    // First the maintainer's signature: the setup is only accepted if its hash is in a checksum list
    // signed with a key GitHub never sees, and the same hash GitHub lists for the file.
    std::string sums, signature, signedHash;
    std::string setupName = "TaskbarVitals-Setup-" + VersionText(out.release.version) + ".exe";
    if (!GetText(out.release.sumsUrl, out.release.sumsSize, deadline, sums) ||
        !GetText(out.release.signatureUrl, out.release.signatureSize, deadline, signature)) {
        out.error = cancelled ? L"Update download cancelled." : L"Could not download the update's signature. Please try again later.";
        return out;
    }
    if (!VerifyReleaseSignature(sums, signature) || !SignedHash(sums, setupName, signedHash) || signedHash != out.release.sha256) {
        out.error = L"The update's signature is not valid, so it was not downloaded. Your current version was kept.";
        return out;
    }
    std::wstring directory = NewDownloadDirectory();
    if (directory.empty()) { out.error = L"Could not create a temporary download folder."; return out; }
    std::wstring partial = directory + L"\\update-setup.part";
    std::wstring final = directory + L"\\update-setup.exe";
    HANDLE file = CreateFileW(partial.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    bool complete = file != INVALID_HANDLE_VALUE &&
        HttpGet(Widen(out.release.download), out.release.size, deadline, true, [&](const char* data, DWORD n) {
            DWORD written = 0;
            return WriteFile(file, data, n, &written, nullptr) && written == n;
        });
    if (file != INVALID_HANDLE_VALUE) CloseHandle(file);
    if (complete && !cancelled && VerifyFile(partial, out.release.sha256, out.release.size) &&
        MoveFileW(partial.c_str(), final.c_str())) {
        out.success = true;
        out.error.clear();
        out.file = final;
    } else {
        DeleteFileW(partial.c_str());
        RemoveDirectoryW(directory.c_str());
        out.error = cancelled ? L"Update download cancelled." :
            L"The update could not be downloaded or its integrity could not be verified. Your current version was kept.";
    }
    return out;
}

struct DownloadDialogState { bool automaticClose = false; };

HRESULT CALLBACK DownloadDialog(HWND dialog, UINT notification, WPARAM button, LPARAM, LONG_PTR context) {
    auto* state = reinterpret_cast<DownloadDialogState*>(context);
    if (notification == TDN_TIMER) {
        SendMessageW(dialog, TDM_SET_PROGRESS_BAR_POS, progress.load(), 0);
        if (done && state) {
            state->automaticClose = true;
            SendMessageW(dialog, TDM_CLICK_BUTTON, IDCANCEL, 0);
            state->automaticClose = false;
        }
    } else if (notification == TDN_BUTTON_CLICKED && button == IDCANCEL &&
               (!state || !state->automaticClose)) {
        // Cancel/Escape always vetoes installation, even if the worker has just finished.
        Cancel();
        if (!done) {
            SendMessageW(dialog, TDM_SET_ELEMENT_TEXT, TDE_CONTENT, (LPARAM)T(L"Cancelling download..."));
            return S_FALSE;
        }
    }
    return S_OK;
}

void DownloadAndInstall(HWND owner) {
    if (Busy() || shuttingDown) return;
    auto now = Now();
    if (schedule.retryAfter > now && schedule.retryAfter - now <= 86400) {
        ShowMessage(owner, L"Please wait before contacting GitHub again.");
        return;
    }
    operation = Operation::Downloading;
    cancelled = false;
    done = false;
    progress = 0;
    started = GetTickCount64();
    worker = std::thread([] {
        Result downloaded = DownloadRelease();
        { std::lock_guard<std::mutex> lock(resultMutex); result = std::move(downloaded); }
        done = true;
    });
    DownloadDialogState state;
    TASKDIALOGCONFIG config{ sizeof(config) };
    config.hwndParent = DialogOwner(owner);
    config.dwFlags = TDF_SHOW_PROGRESS_BAR | TDF_CALLBACK_TIMER;
    config.dwCommonButtons = TDCBF_CANCEL_BUTTON;
    config.pszWindowTitle = T(L"Taskbar Vitals update");
    config.pszMainInstruction = T(L"Downloading and verifying the update");
    config.pszContent = T(L"Taskbar Vitals will close briefly while the update is installed.");
    config.pfCallback = DownloadDialog;
    config.lpCallbackData = reinterpret_cast<LONG_PTR>(&state);
    HRESULT dialogResult = TaskDialogIndirect(&config, nullptr, nullptr, nullptr);
    if (FAILED(dialogResult)) Cancel();
    if (worker.joinable()) worker.join();
    done = false;
    Result downloaded;
    { std::lock_guard<std::mutex> lock(resultMutex); downloaded = std::move(result); }
    operation = Operation::Idle;
    schedule.Finished(Now(), downloaded.metadataValid, downloaded.retry);
    SaveSchedule();
    if (downloaded.metadataValid) { known = downloaded.release; SaveKnown(); }
    if (!downloaded.success || cancelled || shuttingDown) {
        if (!cancelled && !shuttingDown) ShowMessage(owner, downloaded.error.c_str());
        if (!downloaded.file.empty()) DeleteFileW(downloaded.file.c_str());
        return;
    }
    if (!StartApply(owner, downloaded.file, downloaded.release, &applyProcess)) DeleteFileW(downloaded.file.c_str());
    else operation = Operation::Installing;
}

void ShowAvailable(HWND owner) {
    if (Busy() || shuttingDown) return;
    Installation installed = FindInstallation(ExecutablePath());
    std::wstring content = TF(L"Installed version: {version}", { { L"version", appversion::wide } }) + L"\n" +
                           TF(L"Available version: {version}", { { L"version", Widen(known.tag) } });
    if (!installed.valid)
        content += std::wstring(L"\n\n") + T(L"This is a portable or unregistered copy. Download the portable ZIP from the release page.");
    TASKDIALOG_BUTTON buttons[3] = {
        { 100, T(L"Download and install") }, { 101, T(L"Release notes") }, { IDCANCEL, T(L"Later") }
    };
    TASKDIALOGCONFIG config{ sizeof(config) };
    config.hwndParent = DialogOwner(owner);
    config.pszWindowTitle = T(L"Taskbar Vitals update");
    config.pszMainInstruction = T(L"A new version is available");
    config.pszContent = content.c_str();
    config.pButtons = installed.valid ? buttons : buttons + 1;
    config.cButtons = installed.valid ? 3 : 2;
    config.nDefaultButton = IDCANCEL;
    int clicked = IDCANCEL;
    dialogOpen = true;
    HRESULT status = TaskDialogIndirect(&config, &clicked, nullptr, nullptr);
    dialogOpen = false;
    if (SUCCEEDED(status) && !shuttingDown) {
        if (clicked == 101) ShellExecuteW(owner, L"open", Widen(known.page).c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        if (clicked == 100) DownloadAndInstall(owner);
    }
}
}

Installation FindInstallation(const std::wstring& executable) {
    Installation out;
    size_t slash = executable.find_last_of(L"\\/");
    if (slash == std::wstring::npos) return out;
    std::wstring directory = executable.substr(0, slash);
    auto canonical = CanonicalDirectory(directory);
    if (canonical.empty()) return out;
    constexpr const wchar_t* key = L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\{7E0B5C2A-3F4D-4C8E-9B61-2D5A8F1C4E93}_is1";
    for (bool all : { false, true }) for (REGSAM view : { KEY_WOW64_64KEY, KEY_WOW64_32KEY }) {
        HKEY h;
        if (RegOpenKeyExW(all ? HKEY_LOCAL_MACHINE : HKEY_CURRENT_USER, key, 0, KEY_READ | view, &h) != ERROR_SUCCESS) continue;
        wchar_t location[32768]{};
        DWORD bytes = sizeof(location);
        LSTATUS status = RegGetValueW(h, nullptr, L"InstallLocation", RRF_RT_REG_SZ, nullptr, location, &bytes);
        RegCloseKey(h);
        if (status != ERROR_SUCCESS) continue;
        auto registered = CanonicalDirectory(location);
        if (!registered.empty() && _wcsicmp(canonical.c_str(), registered.c_str()) == 0) {
            out.directory = directory;
            out.allUsers = all;
            out.valid = true;
            return out;
        }
    }
    return out;
}

bool VerifyFile(const std::wstring& path, const std::string& hash, std::uint64_t expectedSize) {
    if (hash.size() != 64 || !expectedSize || expectedSize > MaxDownloadBytes) return false;
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER size;
    bool ok = GetFileSizeEx(file, &size) && (std::uint64_t)size.QuadPart == expectedSize;
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    BCRYPT_HASH_HANDLE state = nullptr;
    DWORD objectSize = 0, returned = 0;
    std::vector<unsigned char> object;
    if (ok) ok = BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) >= 0;
    if (ok) ok = BCryptGetProperty(algorithm, BCRYPT_OBJECT_LENGTH, (PUCHAR)&objectSize, sizeof(objectSize), &returned, 0) >= 0;
    if (ok) { object.resize(objectSize); ok = BCryptCreateHash(algorithm, &state, object.data(), objectSize, nullptr, 0, 0) >= 0; }
    unsigned char buffer[32768], digest[32];
    DWORD read = 0;
    while (ok) {
        if (!ReadFile(file, buffer, sizeof(buffer), &read, nullptr)) { ok = false; break; }
        if (!read) break;
        ok = BCryptHashData(state, buffer, read, 0) >= 0;
    }
    if (ok) ok = BCryptFinishHash(state, digest, sizeof(digest), 0) >= 0;
    std::string computed;
    if (ok) for (unsigned char b : digest) { computed += "0123456789abcdef"[b >> 4]; computed += "0123456789abcdef"[b & 15]; }
    if (state) BCryptDestroyHash(state);
    if (algorithm) BCryptCloseAlgorithmProvider(algorithm, 0);
    CloseHandle(file);
    return ok && computed == hash;
}
}
