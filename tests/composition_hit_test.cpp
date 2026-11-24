#include <windows.h>
#include <cstdio>
#include <vector>
#include "../native/composition_surface.h"
int leftClicks=0,rightClicks=0;
LRESULT CALLBACK Proc(HWND h,UINT msg,WPARAM w,LPARAM l){
 if(msg==WM_LBUTTONUP){++leftClicks;return 0;}if(msg==WM_RBUTTONUP){++rightClicks;return 0;}
 if(msg==WM_MOUSEACTIVATE)return MA_NOACTIVATE;
 if(msg==WM_ERASEBKGND)return 1;
 return DefWindowProcW(h,msg,w,l);
}
int main(){
 WNDCLASSW cls{};cls.lpfnWndProc=Proc;cls.hInstance=GetModuleHandleW(nullptr);cls.lpszClassName=L"CompositionHitFixture";RegisterClassW(&cls);
 HWND h=CreateWindowExW(WS_EX_TOOLWINDOW|WS_EX_NOACTIVATE|WS_EX_NOREDIRECTIONBITMAP,cls.lpszClassName,L"Composition hit fixture",WS_POPUP,200,200,180,24,nullptr,nullptr,cls.hInstance,nullptr);
 CompositionSurface renderer;std::vector<unsigned> transparent(180*24,0);
 bool ok=h && renderer.Initialize(h) && renderer.Paint(transparent.data(),180,24,180*4);
 SetWindowPos(h,HWND_TOPMOST,200,200,180,24,SWP_NOACTIVATE|SWP_SHOWWINDOW);
 for(POINT point: {POINT{201,201},POINT{378,201},POINT{201,222},POINT{378,222},POINT{290,212}})
     ok=ok && WindowFromPoint(point)==h;
 for(int i=0;i<20;++i){SendMessageW(h,WM_LBUTTONUP,0,0);SendMessageW(h,WM_RBUTTONUP,0,0);}
 ok=ok && leftClicks==20 && rightClicks==20;
 renderer.Reset();DestroyWindow(h);
 std::printf("%s: fully transparent corners/center hit the field; 20 left and 20 right actions delivered\n",ok?"PASS":"FAIL");return ok?0:1;
}
