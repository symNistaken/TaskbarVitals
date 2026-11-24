// Reports mice and keyboards that no driver produced a battery level for, so the menu can say
// "seen, but not supported (VID:PID)" instead of the device silently missing. Runs after every
// other driver; the results carry `unsupported` and never reach the taskbar or tray.
#include "drivers.h"

#include <cwctype>

void PollUnsupported(PollContext& ctx) {
    std::set<std::wstring> reported;  // physical devices already shown (e.g. Windows' Bluetooth level)
    for (const auto& c : ctx.colls)
        if (ctx.IsClaimed(c.vid, c.pid)) reported.insert(HidContainer(c.path));

    std::set<std::pair<USHORT, USHORT>> done;
    for (const auto& c : ctx.colls) {
        if (ctx.IsClaimed(c.vid, c.pid) || done.count({ c.vid, c.pid })) continue;
        if (ctx.btContainers.count(HidContainer(c.path)) || reported.count(HidContainer(c.path))) continue;
        DeviceKind kind = KindFromCollections(ctx.colls, c.vid, c.pid);
        if (kind != DeviceKind::Mouse && kind != DeviceKind::Keyboard) continue;
        done.insert({ c.vid, c.pid });
        DeviceStatus s;
        s.key = L"unsupported:" + Hex4(c.vid) + L":" + Hex4(c.pid);
        for (const auto& o : ctx.colls)  // not every collection carries the product string
            if (s.name.empty() && o.vid == c.vid && o.pid == c.pid) s.name = HidProduct(o);
        // Windows has no wired / wireless flag, so go by the product string ("2.4G Wireless Receiver").
        std::wstring low = s.name;
        for (auto& ch : low) ch = (wchar_t)towlower(ch);
        bool wireless = false;
        for (const wchar_t* w : { L"wireless", L"receiver", L"dongle", L"2.4g", L"bluetooth" }) wireless |= low.find(w) != low.npos;
        if (!wireless) continue;
        s.name += L" (" + Hex4(c.vid) + L":" + Hex4(c.pid) + L")";
        s.kind = kind;
        s.unsupported = true;
        ctx.out.push_back(s);
    }
}
