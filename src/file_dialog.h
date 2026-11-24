#pragma once
// Native Open/Save file dialogs via delay-loaded comdlg32.dll so neither build.bat nor Makefile
// requires -lcomdlg32 or -luuid.
#include <windows.h>
#include <commdlg.h>
#include <objbase.h>

#include <cwchar>
#include <string>
#include <vector>

namespace file_dialog {

enum class Mode { Open, Save };

inline std::wstring BuildFilter(const wchar_t* label, const wchar_t* pattern, const wchar_t* allLabel) {
    std::wstring f;
    f.append(label);
    f.push_back(L'\0');
    f.append(pattern);
    f.push_back(L'\0');
    f.append(allLabel);
    f.push_back(L'\0');
    f.append(L"*.*");
    f.push_back(L'\0');
    f.push_back(L'\0');
    return f;
}

inline bool PickFile(HWND owner, Mode mode, const wchar_t* title, const std::wstring& filter,
                     const wchar_t* defaultExt, const std::wstring& initialName, std::wstring& outPath) {
    HMODULE lib = LoadLibraryExW(L"comdlg32.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!lib) return false;
    using DialogFn = BOOL(WINAPI*)(LPOPENFILENAMEW);
    const char* procName = mode == Mode::Save ? "GetSaveFileNameW" : "GetOpenFileNameW";
    auto fn = reinterpret_cast<DialogFn>(reinterpret_cast<void*>(GetProcAddress(lib, procName)));
    if (!fn) {
        FreeLibrary(lib);
        return false;
    }

    std::vector<wchar_t> pathBuf(4096, 0);
    if (!initialName.empty() && initialName.size() < pathBuf.size()) {
        wcsncpy(pathBuf.data(), initialName.c_str(), pathBuf.size() - 1);
    }

    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = owner;
    ofn.lpstrFilter = filter.c_str();
    ofn.nFilterIndex = 1;
    ofn.lpstrFile = pathBuf.data();
    ofn.nMaxFile = static_cast<DWORD>(pathBuf.size());
    ofn.lpstrTitle = title;
    ofn.lpstrDefExt = defaultExt;
    ofn.Flags = OFN_EXPLORER | OFN_NOCHANGEDIR | OFN_PATHMUSTEXIST |
                (mode == Mode::Save ? OFN_OVERWRITEPROMPT : OFN_FILEMUSTEXIST);

    const HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    const BOOL ok = fn(&ofn);
    if (SUCCEEDED(com)) CoUninitialize();
    FreeLibrary(lib);

    if (!ok || !pathBuf[0]) return false;
    outPath = pathBuf.data();
    return true;
}

inline bool WriteUtf8File(const std::wstring& path, const std::string& data) {
    HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    size_t offset = 0;
    bool ok = true;
    while (offset < data.size()) {
        DWORD written = 0;
        const DWORD chunk = static_cast<DWORD>(data.size() - offset > (1u << 20) ? (1u << 20) : (data.size() - offset));
        if (!WriteFile(file, data.data() + offset, chunk, &written, nullptr) || written == 0) {
            ok = false;
            break;
        }
        offset += written;
    }
    if (ok) ok = FlushFileBuffers(file) != FALSE;
    CloseHandle(file);
    return ok;
}

inline bool ReadBoundedFile(const std::wstring& path, size_t maxBytes, std::string& out, bool& tooLarge) {
    out.clear();
    tooLarge = false;
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER size{};
    if (!GetFileSizeEx(file, &size) || size.QuadPart < 0) {
        CloseHandle(file);
        return false;
    }
    if (static_cast<unsigned long long>(size.QuadPart) > maxBytes) {
        CloseHandle(file);
        tooLarge = true;
        return false;
    }
    out.resize(static_cast<size_t>(size.QuadPart));
    size_t offset = 0;
    bool ok = true;
    while (offset < out.size()) {
        DWORD got = 0;
        const DWORD chunk = static_cast<DWORD>(out.size() - offset > (1u << 20) ? (1u << 20) : (out.size() - offset));
        if (!ReadFile(file, &out[offset], chunk, &got, nullptr) || got == 0) {
            ok = false;
            break;
        }
        offset += got;
    }
    CloseHandle(file);
    if (!ok) out.clear();
    return ok;
}

}  // namespace file_dialog
