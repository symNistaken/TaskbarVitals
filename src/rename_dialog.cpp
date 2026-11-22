#include "rename_dialog.h"
#include "device_aliases.h"
#include "i18n.h"
#include "memory_trim.h"

#include <algorithm>
#include <vector>

namespace {
constexpr wchar_t kWindowClass[] = L"WirelessBatteryRenameDialog";

struct EditorState {
    std::wstring initial;
    std::wstring result;
    HWND edit = nullptr;
    bool accepted = false;
};

EditorState* State(HWND hwnd) {
    return reinterpret_cast<EditorState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
}

void SetControlFont(HWND control) {
    if (control) SendMessageW(control, WM_SETFONT, (WPARAM)GetStockObject(DEFAULT_GUI_FONT), TRUE);
}

LRESULT CALLBACK EditorProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_NCCREATE) {
        auto* create = reinterpret_cast<CREATESTRUCTW*>(lp);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)create->lpCreateParams);
        return TRUE;
    }
    EditorState* state = State(hwnd);
    if (!state) return DefWindowProcW(hwnd, msg, wp, lp);

    switch (msg) {
        case WM_CREATE: {
            HINSTANCE inst = GetModuleHandleW(nullptr);
            const int dpi = GetDpiForWindow(hwnd);
            auto scale = [dpi](int v) { return MulDiv(v, dpi, 96); };
            HWND label = CreateWindowExW(0, L"STATIC", T(L"Device display name:"),
                                         WS_CHILD | WS_VISIBLE, scale(16), scale(18), scale(382), scale(22),
                                         hwnd, nullptr, inst, nullptr);
            state->edit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", state->initial.c_str(),
                                          WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
                                          scale(16), scale(45), scale(382), scale(25),
                                          hwnd, (HMENU)(INT_PTR)100, inst, nullptr);
            HWND save = CreateWindowExW(0, L"BUTTON", T(L"Save"),
                                        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
                                        scale(220), scale(85), scale(85), scale(29),
                                        hwnd, (HMENU)(INT_PTR)IDOK, inst, nullptr);
            HWND cancel = CreateWindowExW(0, L"BUTTON", T(L"Cancel"),
                                          WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                                          scale(313), scale(85), scale(85), scale(29),
                                          hwnd, (HMENU)(INT_PTR)IDCANCEL, inst, nullptr);
            SetControlFont(label);
            SetControlFont(state->edit);
            SetControlFont(save);
            SetControlFont(cancel);
            SendMessageW(state->edit, EM_SETLIMITTEXT, aliases::kMaxNameLength, 0);
            SetFocus(state->edit);
            SendMessageW(state->edit, EM_SETSEL, 0, -1);
            return 0;
        }
        case WM_COMMAND:
            if (LOWORD(wp) == IDOK) {
                const int length = GetWindowTextLengthW(state->edit);
                std::vector<wchar_t> buffer(static_cast<size_t>(length) + 1, 0);
                GetWindowTextW(state->edit, buffer.data(), (int)buffer.size());
                std::wstring name = aliases::Trim(buffer.data());
                if (!aliases::ValidName(name)) {
                    MessageBoxW(hwnd, T(L"Enter a name (1-64 characters)."), T(L"Rename device"), MB_OK | MB_ICONINFORMATION);
                    SetFocus(state->edit);
                    return 0;
                }
                state->result = std::move(name);
                state->accepted = true;
                DestroyWindow(hwnd);
                return 0;
            }
            if (LOWORD(wp) == IDCANCEL) { DestroyWindow(hwnd); return 0; }
            break;
        case WM_CLOSE: DestroyWindow(hwnd); return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}
}  // namespace

bool PromptDeviceName(HWND owner, const std::wstring& currentName, std::wstring& result) {
    HINSTANCE inst = GetModuleHandleW(nullptr);
    WNDCLASSEXW wc{ sizeof(wc) };
    wc.hInstance = inst;
    wc.lpfnWndProc = EditorProc;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.lpszClassName = kWindowClass;
    if (!RegisterClassExW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return false;

    const int dpi = owner ? GetDpiForWindow(owner) : 96;
    const int width = MulDiv(420, dpi, 96), height = MulDiv(157, dpi, 96);
    POINT p{};
    GetCursorPos(&p);
    HMONITOR monitor = MonitorFromPoint(p, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi{ sizeof(mi) };
    RECT area{};
    if (GetMonitorInfoW(monitor, &mi)) area = mi.rcWork;
    else SystemParametersInfoW(SPI_GETWORKAREA, 0, &area, 0);
    int x = area.left + ((area.right - area.left - width) / 2);
    int y = area.top + ((area.bottom - area.top - height) / 2);
    x = std::max(x, static_cast<int>(area.left));
    y = std::max(y, static_cast<int>(area.top));

    EditorState state;
    state.initial = currentName;
    HWND dialog = CreateWindowExW(WS_EX_DLGMODALFRAME | WS_EX_TOOLWINDOW, kWindowClass,
                                   T(L"Rename device"), WS_CAPTION | WS_SYSMENU | WS_POPUP,
                                   x, y, width, height, owner, nullptr, inst, &state);
    if (!dialog) return false;

    const bool ownerEnabled = owner && IsWindowEnabled(owner);
    if (ownerEnabled) EnableWindow(owner, FALSE);
    ShowWindow(dialog, SW_SHOW);
    UpdateWindow(dialog);
    SetForegroundWindow(dialog);
    if (state.edit) {
        SetFocus(state.edit);
        SendMessageW(state.edit, EM_SETSEL, 0, -1);
    }
    MSG msg{};
    while (IsWindow(dialog)) {
        BOOL code = GetMessageW(&msg, nullptr, 0, 0);
        if (code <= 0) {
            if (code == 0) PostQuitMessage((int)msg.wParam);
            break;
        }
        if (!IsDialogMessageW(dialog, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
    if (IsWindow(dialog)) DestroyWindow(dialog);
    if (ownerEnabled) EnableWindow(owner, TRUE);
    memory_trim::Soon();
    if (state.accepted) result = std::move(state.result);
    return state.accepted;
}
