#pragma once
#include <windows.h>
#include <initializer_list>

// Independent of modal menu loops on the application GUI thread. The guard is
// active at high resolution only over MyFinder and briefly after leaving it.
class FinderOrderGuard {
    struct Context { HWND field,host; HANDLE stop; };
    HANDLE thread=nullptr,stop=nullptr;
    static void CALLBACK WakeOnFinderEvent(HWINEVENTHOOK, DWORD, HWND, LONG, LONG, DWORD, DWORD) {
        // Delivery wakes the worker's message wait. The loop checks actual order.
    }
    static DWORD WINAPI Run(void* value) {
        auto context=static_cast<Context*>(value);
        SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
        SetThreadPriority(GetCurrentThread(),THREAD_PRIORITY_ABOVE_NORMAL);
        HANDLE timer=CreateWaitableTimerExW(nullptr,nullptr,2,TIMER_ALL_ACCESS);
        MSG message{};
        PeekMessageW(&message, nullptr, 0, 0, PM_NOREMOVE);
        DWORD finderProcess = 0;
        GetWindowThreadProcessId(context->host, &finderProcess);
        HWINEVENTHOOK hook = finderProcess ? SetWinEventHook(
            EVENT_OBJECT_SHOW, EVENT_OBJECT_LOCATIONCHANGE, nullptr,
            WakeOnFinderEvent, finderProcess, 0, WINEVENT_OUTOFCONTEXT) : nullptr;
        // The main thread can be busy processing menu messages when the shell
        // switches foreground windows. Wake this worker independently as well.
        HWINEVENTHOOK foregroundHook = SetWinEventHook(
            EVENT_SYSTEM_FOREGROUND, EVENT_SYSTEM_FOREGROUND, nullptr,
            WakeOnFinderEvent, 0, 0, WINEVENT_OUTOFCONTEXT);
        HWINEVENTHOOK cloakHook = finderProcess ? SetWinEventHook(
            EVENT_OBJECT_CLOAKED, EVENT_OBJECT_UNCLOAKED, nullptr,
            WakeOnFinderEvent, finderProcess, 0, WINEVENT_OUTOFCONTEXT) : nullptr;
        ULONGLONG activeUntil=0;
        bool cursorWasOnBar = false;
        for(;;) {
            if(WaitForSingleObject(context->stop,0)==WAIT_OBJECT_0)break;
            RECT bar{},field{};POINT mouse{};
            bool valid=IsWindow(context->field)&&IsWindow(context->host)&&IsWindowVisible(context->field)&&
                       IsWindowVisible(context->host)&&GetWindowRect(context->host,&bar)&&GetWindowRect(context->field,&field);
            ULONGLONG now=GetTickCount64();
            const bool cursorOnBar = valid && GetCursorPos(&mouse) && PtInRect(&bar, mouse);
            if (cursorOnBar && !cursorWasOnBar &&
                (GetWindowLongPtrW(context->host, GWL_EXSTYLE) & WS_EX_TOPMOST) &&
                WindowFromPoint(POINT{(field.left + field.right) / 2, (field.top + field.bottom) / 2}) == context->field) {
                // Give the visible field a head start before the first click.
                // Skip promotion if a popup already covers the field's center.
                SetWindowPos(context->field, HWND_TOPMOST, 0, 0, 0, 0,
                             SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOOWNERZORDER | SWP_NOSENDCHANGING);
            }
            cursorWasOnBar = cursorOnBar;
            bool interacting = cursorOnBar;
            if(valid && !interacting) {
                for(HWND source:{context->host,context->field}) {
                    GUITHREADINFO info{sizeof(info)};
                    if(GetGUIThreadInfo(GetWindowThreadProcessId(source,nullptr),&info) &&
                       (info.hwndCapture||info.hwndMenuOwner||(info.flags&(GUI_INMENUMODE|GUI_POPUPMENUMODE))))interacting=true;
                }
                HWND foreground=GetForegroundWindow();DWORD pid=0;
                GetWindowThreadProcessId(foreground,&pid);
                if(pid==GetCurrentProcessId()&&IsWindowVisible(foreground))interacting=true;
            }
            if(interacting)activeUntil=now+1000;
            bool active=valid&&now<activeUntil;
            if(valid) {
                RECT overlap{};
                HWND coveringBar = nullptr;
                if (IntersectRect(&overlap, &field, &bar)) {
                    // Scan every window above the field, including hit-transparent bars.
                    HWND current = GetWindow(context->field, GW_HWNDPREV);
                    for (int i = 0; current && i < 256; ++i) {
                        if (current == context->host) {
                            if (!coveringBar) coveringBar = current;
                        } else if (finderProcess && IsWindowVisible(current)) {
                            DWORD process = 0;
                            GetWindowThreadProcessId(current, &process);
                            RECT candidate{};
                            if (process == finderProcess && GetWindowRect(current, &candidate) &&
                                candidate.top == bar.top && candidate.bottom == bar.bottom &&
                                candidate.right - candidate.left >= (bar.right - bar.left) / 2 &&
                                IntersectRect(&overlap, &field, &candidate)) {
                                coveringBar = current;
                            }
                        }
                        current = GetWindow(current, GW_HWNDPREV);
                    }
                }
                if (coveringBar) {
                    HWND above=GetWindow(coveringBar,GW_HWNDPREV);
                    bool top=(GetWindowLongPtrW(coveringBar,GWL_EXSTYLE)&WS_EX_TOPMOST)!=0;
                    if(above!=context->field) {
                        if(!above||((GetWindowLongPtrW(above,GWL_EXSTYLE)&WS_EX_TOPMOST)!=0)!=top)
                            above=top?HWND_TOPMOST:HWND_TOP;
                        SetWindowPos(context->field,above,0,0,0,0,
                                     SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE|SWP_NOOWNERZORDER|SWP_NOSENDCHANGING);
                    }
                }
            }
            DWORD delay = active ? 1 : valid ? 16 : 50;
            if(timer) {
                LARGE_INTEGER due;due.QuadPart=-10000LL*delay;
                SetWaitableTimer(timer,&due,0,nullptr,nullptr,FALSE);
            }
            HANDLE handles[]{context->stop, timer};
            const DWORD count = timer ? 2 : 1;
            DWORD ready = MsgWaitForMultipleObjectsEx(count, handles, timer ? INFINITE : delay,
                                                      QS_ALLINPUT, MWMO_INPUTAVAILABLE);
            if (ready == WAIT_OBJECT_0 || ready == WAIT_FAILED) break;
            if (ready == WAIT_OBJECT_0 + count) {
                for (int i = 0; i < 32 && PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE); ++i)
                    DispatchMessageW(&message);
            }
        }
        if (cloakHook) UnhookWinEvent(cloakHook);
        if (foregroundHook) UnhookWinEvent(foregroundHook);
        if (hook) UnhookWinEvent(hook);
        if(timer)CloseHandle(timer);
        CloseHandle(context->stop);delete context;return 0;
    }
public:
    ~FinderOrderGuard(){Stop();}
    void Start(HWND field,HWND host) {
        Stop();
        stop=CreateEventW(nullptr,TRUE,FALSE,nullptr);
        if(!stop)return;
        auto context=new Context{field,host,stop};
        thread=CreateThread(nullptr,0,Run,context,0,nullptr);
        if(!thread){CloseHandle(stop);stop=nullptr;delete context;}
    }
    void Stop() {
        if(!thread)return;
        HANDLE ending=thread,signal=stop;thread=nullptr;stop=nullptr;
        SetEvent(signal);
        // A final cross-thread SetWindowPos may be waiting for a sent message.
        // Service those messages while joining instead of blocking the GUI.
        auto deadline=GetTickCount64()+1000;
        while(WaitForSingleObject(ending,0)==WAIT_TIMEOUT&&GetTickCount64()<deadline) {
            DWORD ready=MsgWaitForMultipleObjects(1,&ending,FALSE,20,QS_SENDMESSAGE);
            if(ready==WAIT_OBJECT_0)break;
            if(ready==WAIT_OBJECT_0+1){MSG message;PeekMessageW(&message,nullptr,0,0,PM_NOREMOVE);}
        }
        CloseHandle(ending);
    }
};
