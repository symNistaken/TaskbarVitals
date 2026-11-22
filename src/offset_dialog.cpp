#include "offset_dialog.h"
#include "i18n.h"

#include <algorithm>
#include <cwchar>
#include <string>

// Same shape as the rename dialog: one label, one edit box, Save and Cancel.
namespace {
constexpr wchar_t kWindowClass[] = L"WirelessBatteryOffsetDialog";

struct OffsetState {
    int initial = 0;
    int result = 0;
    HWND edit = nullptr;
    bool accepted = false;
};

OffsetState* State(HWND hwnd) {
    return reinterpret_cast<OffsetState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
}

void SetControlFont(HWND control) {
    if (control) SendMessageW(control, WM_SETFONT, (WPARAM)GetStockObject(DEFAULT_GUI_FONT), TRUE);
}

// Whole number in range, optional leading minus; anything else is rejected.
bool ParseOffset(const wchar_t* text, int& out) {
    while (*text == L' ') text++;
    if (!*text) return false;
    wchar_t* end = nullptr;
    long v = wcstol(text, &end, 10);
    while (end && *end == L' ') end++;
    if (!end || *end || v < kMinTaskbarOffset || v > kMaxTaskbarOffset) return false;
    out = (int)v;
    return true;
}

LRESULT CALLBACK OffsetProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_NCCREATE) {
        auto* create = reinterpret_cast<CREATESTRUCTW*>(lp);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)create->lpCreateParams);
        return TRUE;
    }
    OffsetState* state = State(hwnd);
    if (!state) return DefWindowProcW(hwnd, msg, wp, lp);

    switch (msg) {
        case WM_CREATE: {
            HINSTANCE inst = GetModuleHandleW(nullptr);
            const int dpi = GetDpiForWindow(hwnd);
            auto scale = [dpi](int v) { return MulDiv(v, dpi, 96); };
            HWND label = CreateWindowExW(0, L"STATIC",
                                         T(L"Pixels from the notification area (negative moves it closer):"),
                                         WS_CHILD | WS_VISIBLE, scale(16), scale(18), scale(382), scale(22),
                                         hwnd, nullptr, inst, nullptr);
            state->edit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", std::to_wstring(state->initial).c_str(),
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
            SendMessageW(state->edit, EM_SETLIMITTEXT, 6, 0);
            SetFocus(state->edit);
            SendMessageW(state->edit, EM_SETSEL, 0, -1);
            return 0;
        }
        case WM_COMMAND:
            if (LOWORD(wp) == IDOK) {
                wchar_t buffer[16] = {};
                GetWindowTextW(state->edit, buffer, ARRAYSIZE(buffer));
                int value = 0;
                if (!ParseOffset(buffer, value)) {
                    std::wstring text = TF(L"Enter a whole number from {min} to {max}.",
                                           { { L"min", std::to_wstring(kMinTaskbarOffset) },
                                             { L"max", std::to_wstring(kMaxTaskbarOffset) } });
                    MessageBoxW(hwnd, text.c_str(), T(L"Position on the taskbar"), MB_OK | MB_ICONINFORMATION);
                    SetFocus(state->edit);
                    return 0;
                }
                state->result = value;
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

bool PromptTaskbarOffset(HWND owner, int current, int& result) {
    HINSTANCE inst = GetModuleHandleW(nullptr);
    WNDCLASSEXW wc{ sizeof(wc) };
    wc.hInstance = inst;
    wc.lpfnWndProc = OffsetProc;
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
    int x = std::max(area.left + ((area.right - area.left - width) / 2), area.left);
    int y = std::max(area.top + ((area.bottom - area.top - height) / 2), area.top);

    OffsetState state;
    state.initial = current;
    HWND dialog = CreateWindowExW(WS_EX_DLGMODALFRAME | WS_EX_TOOLWINDOW, kWindowClass,
                                  T(L"Position on the taskbar"), WS_CAPTION | WS_SYSMENU | WS_POPUP,
                                  x, y, width, height, owner, nullptr, inst, &state);
    if (!dialog) return false;

    const bool ownerEnabled = owner && IsWindowEnabled(owner);
    if (ownerEnabled) EnableWindow(owner, FALSE);
    ShowWindow(dialog, SW_SHOW);
    UpdateWindow(dialog);
    SetForegroundWindow(dialog);
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
    if (state.accepted) result = state.result;
    return state.accepted;
}
