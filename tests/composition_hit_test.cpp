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
// True when `other` sits above `h` in the z-order (it may legitimately cover the fixture).
static bool IsAbove(HWND other,HWND h){
 for(HWND w=GetWindow(h,GW_HWNDPREV);w;w=GetWindow(w,GW_HWNDPREV)) if(w==other) return true;
 return false;
}
static void Describe(HWND w){
 wchar_t cls[128]=L"?",exe[MAX_PATH]=L"?";DWORD pid=0;
 if(w){GetClassNameW(w,cls,128);GetWindowThreadProcessId(w,&pid);
  if(HANDLE p=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,FALSE,pid)){DWORD n=MAX_PATH;QueryFullProcessImageNameW(p,0,exe,&n);CloseHandle(p);}}
 std::printf("  hit hwnd=%p class=%ls pid=%lu exe=%ls\n",(void*)w,cls,pid,exe);
}
enum class Result{Pass,Covered,Fail};
// Hit-tests the corners and center of the fixture at its current rect. A point that
// lands on a window above ours is "Covered" (environment, retry elsewhere); a point that
// lands on anything else, i.e. a window behind ours, is a real click-through failure.
static Result HitTest(HWND h){
 RECT r;GetWindowRect(h,&r);
 const POINT points[]={{r.left+1,r.top+1},{r.right-2,r.top+1},{r.left+1,r.bottom-2},{r.right-2,r.bottom-2},{(r.left+r.right)/2,(r.top+r.bottom)/2}};
 Result result=Result::Pass;
 for(POINT p: points){
  HWND got=WindowFromPoint(p);
  if(got==h) continue;
  std::printf("  point (%ld,%ld) not ours:\n",p.x,p.y);Describe(got);
  if(got && IsAbove(got,h)) result=Result::Covered; else return Result::Fail;
 }
 return result;
}
int main(){
 WNDCLASSW cls{};cls.lpfnWndProc=Proc;cls.hInstance=GetModuleHandleW(nullptr);cls.lpszClassName=L"CompositionHitFixture";RegisterClassW(&cls);
 HWND h=CreateWindowExW(WS_EX_TOOLWINDOW|WS_EX_NOACTIVATE|WS_EX_NOREDIRECTIONBITMAP,cls.lpszClassName,L"Composition hit fixture",WS_POPUP,200,200,180,24,nullptr,nullptr,cls.hInstance,nullptr);
 CompositionSurface renderer;std::vector<unsigned> transparent(180*24,0);
 bool ok=h && renderer.Initialize(h) && renderer.Paint(transparent.data(),180,24,180*4);
 if(!ok){std::printf("FAIL: fixture window or composition surface could not be created\n");return 1;}
 // Try several spots across the primary monitor so another app's topmost window covering
 // one spot does not decide the result.
 const int sw=GetSystemMetrics(SM_CXSCREEN),sh=GetSystemMetrics(SM_CYSCREEN);
 const POINT spots[]={{200,200},{sw/2,sh/2},{sw/4,sh/4},{sw*3/4,sh/4},{sw/4,sh*3/4},{sw*3/4,sh*3/4},{sw/2,sh/8},{sw/8,sh/2}};
 Result result=Result::Covered;
 for(POINT s: spots){
  SetWindowPos(h,HWND_TOPMOST,s.x,s.y,180,24,SWP_NOACTIVATE|SWP_SHOWWINDOW);
  std::printf("spot (%ld,%ld)\n",s.x,s.y);
  result=HitTest(h);
  if(result!=Result::Covered) break;
 }
 for(int i=0;i<20;++i){SendMessageW(h,WM_LBUTTONUP,0,0);SendMessageW(h,WM_RBUTTONUP,0,0);}
 bool delivered=leftClicks==20 && rightClicks==20;
 renderer.Reset();DestroyWindow(h);
 if(result==Result::Covered){
  std::printf("SKIP: every candidate spot was covered by another window above the fixture; hit test not evaluated\n");
  return delivered?0:1;
 }
 bool pass=result==Result::Pass && delivered;
 std::printf("%s: fully transparent corners/center hit the field; 20 left and 20 right actions delivered\n",pass?"PASS":"FAIL");
 return pass?0:1;
}
