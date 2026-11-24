#pragma once
#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>
#include <d2d1_1.h>
#include <dcomp.h>
#include <dwmapi.h>
#include <cstdint>
#include <cstring>
#include <vector>

template<class T> struct CompositionRef {
    T* p=nullptr;
    ~CompositionRef(){reset();}
    void reset(){if(p){p->Release();p=nullptr;}}
    T** put(){reset();return &p;}
    T* operator->() const{return p;}
};

// GPU-composited transparency leaves User32 hit testing rectangular. Unlike
// alpha-zero WS_EX_LAYERED pixels, transparent padding still receives clicks.
class CompositionSurface {
    CompositionRef<ID3D11Device> d3d;
    CompositionRef<IDXGIDevice> dxgi;
    CompositionRef<ID2D1Device> d2d;
    CompositionRef<IDCompositionDevice> composition;
    CompositionRef<IDCompositionTarget> target;
    CompositionRef<IDCompositionVisual> visual;
    CompositionRef<IDCompositionSurface> surface;
    UINT width=0,height=0;
    std::vector<std::uint8_t> committedPixels;
    UINT committedStride=0;
public:
    void Reset(){surface.reset();visual.reset();target.reset();composition.reset();d2d.reset();dxgi.reset();d3d.reset();width=height=0;committedPixels.clear();committedStride=0;}
    bool Initialize(HWND window){
        Reset();
        BOOL disabled=TRUE;
        DwmSetWindowAttribute(window,DWMWA_TRANSITIONS_FORCEDISABLED,&disabled,sizeof(disabled));
        int nonClient=1,backdrop=1,corner=1;
        DwmSetWindowAttribute(window,DWMWA_NCRENDERING_POLICY,&nonClient,sizeof(nonClient));
        DwmSetWindowAttribute(window,38,&backdrop,sizeof(backdrop));
        DwmSetWindowAttribute(window,33,&corner,sizeof(corner));
        HRESULT hr=D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_HARDWARE,nullptr,D3D11_CREATE_DEVICE_BGRA_SUPPORT,
                                    nullptr,0,D3D11_SDK_VERSION,d3d.put(),nullptr,nullptr);
        if(FAILED(hr)) hr=D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,D3D11_CREATE_DEVICE_BGRA_SUPPORT,
                                    nullptr,0,D3D11_SDK_VERSION,d3d.put(),nullptr,nullptr);
        if(FAILED(hr) || FAILED(d3d->QueryInterface(__uuidof(IDXGIDevice),reinterpret_cast<void**>(dxgi.put())))) return false;
        CompositionRef<ID2D1Factory1> factory;
        if(FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED,__uuidof(ID2D1Factory1),nullptr,
                                  reinterpret_cast<void**>(factory.put()))) ||
           FAILED(factory->CreateDevice(dxgi.p,d2d.put())) ||
           FAILED(DCompositionCreateDevice(dxgi.p,__uuidof(IDCompositionDevice),reinterpret_cast<void**>(composition.put()))) ||
           FAILED(composition->CreateTargetForHwnd(window,TRUE,target.put())) ||
           FAILED(composition->CreateVisual(visual.put())) || FAILED(target->SetRoot(visual.p))) return false;
        return SUCCEEDED(composition->Commit());
    }
    bool Paint(const void* pixels,UINT w,UINT h,UINT stride){
        if(!composition.p || !w || !h) return false;
        const auto* bytes = static_cast<const std::uint8_t*>(pixels);
        const size_t byteCount = static_cast<size_t>(stride) * h;
        if (surface.p && width == w && height == h && committedStride == stride &&
            committedPixels.size() == byteCount && SUCCEEDED(d3d->GetDeviceRemovedReason()) &&
            std::memcmp(committedPixels.data(), bytes, byteCount) == 0) return true;
        committedPixels.clear();
        if(width!=w || height!=h || !surface.p){
            if(FAILED(composition->CreateSurface(w,h,DXGI_FORMAT_B8G8R8A8_UNORM,DXGI_ALPHA_MODE_PREMULTIPLIED,surface.put())) ||
               FAILED(visual->SetContent(surface.p)))return false;
            width=w;height=h;
        }
        CompositionRef<IDXGISurface> output;
        POINT offset{};
        if(FAILED(surface->BeginDraw(nullptr,__uuidof(IDXGISurface),reinterpret_cast<void**>(output.put()),&offset)))return false;
        CompositionRef<ID2D1DeviceContext> context;
        CompositionRef<ID2D1Bitmap1> targetBitmap,sourceBitmap;
        HRESULT hr=d2d->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE,context.put());
        D2D1_BITMAP_PROPERTIES1 props{};
        props.pixelFormat={DXGI_FORMAT_B8G8R8A8_UNORM,D2D1_ALPHA_MODE_PREMULTIPLIED};props.dpiX=props.dpiY=96;
        props.bitmapOptions=D2D1_BITMAP_OPTIONS_TARGET|D2D1_BITMAP_OPTIONS_CANNOT_DRAW;
        if(SUCCEEDED(hr)) hr=context->CreateBitmapFromDxgiSurface(output.p,&props,targetBitmap.put());
        props.bitmapOptions=D2D1_BITMAP_OPTIONS_NONE;
        D2D1_SIZE_U size{w,h};
        if(SUCCEEDED(hr)) hr=context->CreateBitmap(size,pixels,stride,&props,sourceBitmap.put());
        if(SUCCEEDED(hr)){
            context->SetTarget(targetBitmap.p);
            context->BeginDraw();
            D2D1_RECT_F destination{float(offset.x),float(offset.y),float(offset.x+w),float(offset.y+h)};
            context->PushAxisAlignedClip(&destination,D2D1_ANTIALIAS_MODE_ALIASED);
            context->Clear(D2D1::ColorF(0,0,0,0));
            context->DrawBitmap(sourceBitmap.p,&destination,1,D2D1_INTERPOLATION_MODE_NEAREST_NEIGHBOR,nullptr,nullptr);
            context->PopAxisAlignedClip();
            hr=context->EndDraw();
        }
        HRESULT ended=surface->EndDraw();
        if (FAILED(hr) || FAILED(ended) || FAILED(composition->Commit())) return false;
        committedPixels.assign(bytes, bytes + byteCount);
        committedStride = stride;
        return true;
    }
};
