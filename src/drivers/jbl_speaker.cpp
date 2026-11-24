// JBL Flip 7 (Harman 0057, PID 2107), BLE control service.
// Read-only speaker-info request AA 11 00; no EQ, modes or firmware writes.
// Protocol: https://github.com/ffrafat/JBLControl/blob/main/docs/PROTOCOL-FLIP7.md
// WinRT ABI follows Windows SDK IDL, as in airpods.cpp (MinGW has no GATT projections).
#include "drivers.h"
#include "jbl_speaker_protocol.h"
#include <setupapi.h>
#include <initguid.h>
#include <devpkey.h>
#include <eventtoken.h>
#include <inspectable.h>
#include <roapi.h>
#include <winstring.h>
#include <asyncinfo.h>
#include <atomic>
#include <mutex>
#include <map>
#include <cstdio>
#include <cwctype>

namespace jbl_speaker_abi {
const GUID IID_Watcher = { 0xa6ac336f, 0xf3d3, 0x4297, { 0x8d, 0x6c, 0xc8, 0x1e, 0xa6, 0x62, 0x3f, 0x40 } };
const GUID IID_ReceivedArgs = { 0x27987ddf, 0xe596, 0x41be, { 0x8d, 0x43, 0x9e, 0x67, 0x31, 0xd4, 0xa9, 0x13 } };
// ITypedEventHandler<BluetoothLEAdvertisementWatcher, BluetoothLEAdvertisementReceivedEventArgs>
const GUID IID_ReceivedHandler = { 0x90eb4eca, 0xd465, 0x5ea0, { 0xa6, 0x1c, 0x03, 0x3c, 0x8c, 0x5e, 0xce, 0xf2 } };
const GUID IID_BufferByteAccess = { 0x905a0fef, 0xbc53, 0x11df, { 0x8c, 0x49, 0x00, 0x1e, 0x4f, 0xc6, 0x86, 0xda } };
const GUID IID_Unknown = { 0x00000000, 0x0000, 0x0000, { 0xc0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46 } };
const GUID IID_Agile = { 0x94ea2b94, 0xe9cc, 0x49e0, { 0xc0, 0xff, 0xee, 0x64, 0xca, 0x8f, 0x5b, 0x90 } };

struct IBufferAbi : IInspectable {
    virtual HRESULT STDMETHODCALLTYPE get_Capacity(UINT32* v) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_Length(UINT32* v) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_Length(UINT32 v) = 0;
};

struct IBufferByteAccessAbi : IUnknown {
    virtual HRESULT STDMETHODCALLTYPE Buffer(BYTE** v) = 0;
};

struct IManufacturerData : IInspectable {
    virtual HRESULT STDMETHODCALLTYPE get_CompanyId(UINT16* v) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_CompanyId(UINT16 v) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_Data(IBufferAbi** v) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_Data(IBufferAbi* v) = 0;
};

struct IManufacturerDataView : IInspectable {  // IVectorView<BluetoothLEManufacturerData>
    virtual HRESULT STDMETHODCALLTYPE GetAt(UINT32 i, IManufacturerData** v) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_Size(UINT32* v) = 0;
};

struct IAdvertisement : IInspectable {
    virtual HRESULT STDMETHODCALLTYPE get_Flags(IInspectable** v) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_Flags(IInspectable* v) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_LocalName(HSTRING* v) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_LocalName(HSTRING v) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_ServiceUuids(IInspectable** v) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_ManufacturerData(IInspectable** v) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_DataSections(IInspectable** v) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetManufacturerDataByCompanyId(UINT16 company, IManufacturerDataView** v) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetSectionsByType(BYTE type, IInspectable** v) = 0;
};

struct IReceivedArgs : IInspectable {
    virtual HRESULT STDMETHODCALLTYPE get_RawSignalStrengthInDBm(INT16* v) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_BluetoothAddress(UINT64* v) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_AdvertisementType(INT32* v) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_Timestamp(INT64* v) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_Advertisement(IAdvertisement** v) = 0;
};

struct IWatcher : IInspectable {
    virtual HRESULT STDMETHODCALLTYPE get_MinSamplingInterval(INT64* v) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_MaxSamplingInterval(INT64* v) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_MinOutOfRangeTimeout(INT64* v) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_MaxOutOfRangeTimeout(INT64* v) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_Status(INT32* v) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_ScanningMode(INT32* v) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_ScanningMode(INT32 v) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_SignalStrengthFilter(IInspectable** v) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_SignalStrengthFilter(IInspectable* v) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_AdvertisementFilter(IInspectable** v) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_AdvertisementFilter(IInspectable* v) = 0;
    virtual HRESULT STDMETHODCALLTYPE Start() = 0;
    virtual HRESULT STDMETHODCALLTYPE Stop() = 0;
    virtual HRESULT STDMETHODCALLTYPE add_Received(IUnknown* handler, EventRegistrationToken* token) = 0;
    virtual HRESULT STDMETHODCALLTYPE remove_Received(EventRegistrationToken token) = 0;
};


template<class T> struct Async : IInspectable {
    virtual HRESULT STDMETHODCALLTYPE put_Completed(IUnknown*) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_Completed(IUnknown**) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetResults(T*) = 0;
};
struct IStatics : IInspectable {
    virtual HRESULT STDMETHODCALLTYPE FromIdAsync(HSTRING, Async<IInspectable*>**) = 0;
    virtual HRESULT STDMETHODCALLTYPE FromBluetoothAddressAsync(UINT64, Async<IInspectable*>**) = 0;
};
struct IStatics2 : IInspectable {
    virtual HRESULT STDMETHODCALLTYPE Pairing(BOOL,HSTRING*) = 0;
    virtual HRESULT STDMETHODCALLTYPE Connection(INT32,HSTRING*) = 0;
    virtual HRESULT STDMETHODCALLTYPE Name(HSTRING,HSTRING*) = 0;
    virtual HRESULT STDMETHODCALLTYPE Address(UINT64,HSTRING*) = 0;
    virtual HRESULT STDMETHODCALLTYPE AddressType(UINT64,INT32,HSTRING*) = 0;
    virtual HRESULT STDMETHODCALLTYPE Appearance(IInspectable*,HSTRING*) = 0;
    virtual HRESULT STDMETHODCALLTYPE FromAddressType(UINT64,INT32,Async<IInspectable*>**) = 0;
};
struct IReceivedArgs2 : IInspectable {
    virtual HRESULT STDMETHODCALLTYPE get_AddressType(INT32*) = 0;
};
struct IDevice3 : IInspectable {
    virtual HRESULT STDMETHODCALLTYPE AccessInfo(IInspectable**) = 0;
    virtual HRESULT STDMETHODCALLTYPE RequestAccess(Async<INT32>**) = 0;
    virtual HRESULT STDMETHODCALLTYPE Services(Async<IInspectable*>**) = 0;
    virtual HRESULT STDMETHODCALLTYPE ServicesCached(INT32,Async<IInspectable*>**) = 0;
    virtual HRESULT STDMETHODCALLTYPE ServicesUuid(GUID,Async<IInspectable*>**) = 0;
    virtual HRESULT STDMETHODCALLTYPE ServicesUuidCached(GUID,INT32,Async<IInspectable*>**) = 0;
};
struct IView : IInspectable {
    virtual HRESULT STDMETHODCALLTYPE GetAt(UINT32,IInspectable**) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_Size(UINT32*) = 0;
};
struct IResult : IInspectable {
    virtual HRESULT STDMETHODCALLTYPE get_Status(INT32*) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_Error(IInspectable**) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_Items(IView**) = 0;
};
struct IService3 : IInspectable {
    virtual HRESULT STDMETHODCALLTYPE AccessInfo(IInspectable**) = 0;
    virtual HRESULT STDMETHODCALLTYPE Session(IInspectable**) = 0;
    virtual HRESULT STDMETHODCALLTYPE Sharing(INT32*) = 0;
    virtual HRESULT STDMETHODCALLTYPE RequestAccess(Async<INT32>**) = 0;
    virtual HRESULT STDMETHODCALLTYPE Open(INT32,Async<INT32>**) = 0;
    virtual HRESULT STDMETHODCALLTYPE Characteristics(Async<IInspectable*>**) = 0;
    virtual HRESULT STDMETHODCALLTYPE CharacteristicsCached(INT32,Async<IInspectable*>**) = 0;
    virtual HRESULT STDMETHODCALLTYPE CharacteristicsUuid(GUID,Async<IInspectable*>**) = 0;
    virtual HRESULT STDMETHODCALLTYPE CharacteristicsUuidCached(GUID,INT32,Async<IInspectable*>**) = 0;
};
struct IChar : IInspectable {
    virtual HRESULT STDMETHODCALLTYPE Descriptors(GUID,IInspectable**) = 0;
    virtual HRESULT STDMETHODCALLTYPE Properties(INT32*) = 0;
    virtual HRESULT STDMETHODCALLTYPE Protection(INT32*) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetProtection(INT32) = 0;
    virtual HRESULT STDMETHODCALLTYPE Description(HSTRING*) = 0;
    virtual HRESULT STDMETHODCALLTYPE Uuid(GUID*) = 0;
    virtual HRESULT STDMETHODCALLTYPE Handle(UINT16*) = 0;
    virtual HRESULT STDMETHODCALLTYPE Formats(IInspectable**) = 0;
    virtual HRESULT STDMETHODCALLTYPE Read(Async<IInspectable*>**) = 0;
    virtual HRESULT STDMETHODCALLTYPE ReadCached(INT32,Async<IInspectable*>**) = 0;
    virtual HRESULT STDMETHODCALLTYPE Write(IBufferAbi*,Async<INT32>**) = 0;
    virtual HRESULT STDMETHODCALLTYPE WriteOption(IBufferAbi*,INT32,Async<INT32>**) = 0;
    virtual HRESULT STDMETHODCALLTYPE ReadCccd(Async<IInspectable*>**) = 0;
    virtual HRESULT STDMETHODCALLTYPE WriteCccd(INT32,Async<INT32>**) = 0;
    virtual HRESULT STDMETHODCALLTYPE AddChanged(IUnknown*,EventRegistrationToken*) = 0;
    virtual HRESULT STDMETHODCALLTYPE RemoveChanged(EventRegistrationToken) = 0;
};
struct IChanged : IInspectable {
    virtual HRESULT STDMETHODCALLTYPE Value(IBufferAbi**) = 0;
};
struct IBufferFactory : IInspectable {
    virtual HRESULT STDMETHODCALLTYPE Create(UINT32,IBufferAbi**) = 0;
};
}
namespace {
using namespace jbl_speaker_abi;
template<class T> struct Ptr {
    T* p = nullptr;
    Ptr() = default;
    Ptr(const Ptr&) = delete;
    Ptr& operator=(const Ptr&) = delete;
    ~Ptr(){ if(p) p->Release(); }
    T* operator->() const { return p; }
    T** out(){ return &p; }
    explicit operator bool() const { return p != nullptr; }
};
GUID Id(const wchar_t* str){ GUID g{}; IIDFromString(str,&g); return g; }
template<class T> bool As(IUnknown* from,const GUID& id,Ptr<T>& result){
    return from && SUCCEEDED(from->QueryInterface(id,reinterpret_cast<void**>(result.out()))) && result;
}
template<class T> bool Factory(const wchar_t* cls,const GUID& iid,Ptr<T>& result){
    HSTRING name=nullptr;
    if(FAILED(WindowsCreateString(cls,static_cast<UINT32>(wcslen(cls)),&name))) return false;
    HRESULT hr=RoGetActivationFactory(name,iid,reinterpret_cast<void**>(result.out()));
    WindowsDeleteString(name);
    return SUCCEEDED(hr)&&result;
}
template<class T> bool Await(Async<T>* op,T* result,ULONGLONG deadline){
    if(!op) return false;
    Ptr<IAsyncInfo> info;
    if(!As(op,Id(L"{00000036-0000-0000-C000-000000000046}"),info)) return false;
    AsyncStatus status=Started;
    while(GetTickCount64()<deadline){
        if(FAILED(info->get_Status(&status))) return false;
        if(status==Completed) return SUCCEEDED(op->GetResults(result));
        if(status!=Started) return false;
        Sleep(20);
    }
    info->Cancel();
    return false;
}
struct RadioInit {
    HRESULT hr=RoInitialize(RO_INIT_MULTITHREADED);
    ~RadioInit(){if(SUCCEEDED(hr)) RoUninitialize();}
};
const DEVPROPKEY kConnected={{0x83DA6326,0x97A6,0x4088,{0x94,0x53,0xA1,0x92,0x3F,0x57,0x3B,0x29}},15};
struct Speaker { std::wstring name,key; UINT64 address; };
std::vector<Speaker> Speakers(){
    std::vector<Speaker> out;
    HDEVINFO di=SetupDiGetClassDevsW(nullptr,L"BTHENUM",nullptr,DIGCF_ALLCLASSES|DIGCF_PRESENT);
    if(di==INVALID_HANDLE_VALUE) return out;
    SP_DEVINFO_DATA dd{sizeof(dd)};
    for(DWORD i=0;SetupDiEnumDeviceInfo(di,i,&dd);++i){
        wchar_t inst[512]{},name[256]{};
        DEVPROPTYPE t;
        if(!SetupDiGetDeviceInstanceIdW(di,&dd,inst,512,nullptr)||_wcsnicmp(inst,L"BTHENUM\\DEV_",12)) continue;
        DEVPROP_BOOLEAN connected=DEVPROP_FALSE;
        if(!SetupDiGetDevicePropertyW(di,&dd,&kConnected,&t,reinterpret_cast<PBYTE>(&connected),sizeof(connected),nullptr,0)||
            t!=DEVPROP_TYPE_BOOLEAN||connected==DEVPROP_FALSE) continue;
        if(!SetupDiGetDevicePropertyW(di,&dd,&DEVPKEY_Device_FriendlyName,&t,reinterpret_cast<PBYTE>(name),sizeof(name),nullptr,0)
            ||t!=DEVPROP_TYPE_STRING) continue;
        std::wstring n=name;for(auto& c:n)c=towlower(c);
        if(n.find(L"jbl flip 7")==std::wstring::npos) continue;
        std::wstring hex(inst+12,12); wchar_t* end=nullptr;
        UINT64 addr=wcstoull(hex.c_str(),&end,16);
        if(addr&&!*end) out.push_back({name,L"jbl-speaker:"+hex,addr});
    }
    SetupDiDestroyDeviceInfoList(di);
    return out;
}
struct Candidate { UINT64 addr; INT32 type; INT16 rssi; };
struct CallbackBase : IUnknown {
    std::atomic<ULONG> refs{1};
    GUID iid;
    explicit CallbackBase(GUID g):iid(g){}

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID q,void** out) override {
        if(!out)return E_POINTER;
        *out=nullptr;
        if(q==IID_Unknown||q==IID_Agile||q==iid){*out=this;AddRef();return S_OK;}
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override{return ++refs;}
    ULONG STDMETHODCALLTYPE Release() override{ULONG r=--refs;if(!r)delete this;return r;}
    virtual HRESULT STDMETHODCALLTYPE Invoke(IInspectable*,IInspectable*)=0;
    virtual ~CallbackBase()=default;
};
struct ScanHandler : CallbackBase {
    std::mutex lock;
    std::map<UINT64,Candidate> candidates;
    ScanHandler():CallbackBase(IID_ReceivedHandler){}
    HRESULT STDMETHODCALLTYPE Invoke(IInspectable*,IInspectable* args) override {
        try{
            Ptr<IReceivedArgs> a; Ptr<IAdvertisement> ad; Ptr<IManufacturerDataView> view;
            if(!As(args,IID_ReceivedArgs,a)||FAILED(a->get_Advertisement(ad.out()))||!ad||
              FAILED(ad->GetManufacturerDataByCompanyId(0x0057,view.out()))||!view)return S_OK;
            UINT32 count=0;view->get_Size(&count);
            for(UINT32 i=0;i<count;++i){
                Ptr<IManufacturerData> md; Ptr<IBufferAbi> buf; Ptr<IBufferByteAccessAbi> bytes;
                BYTE* p=nullptr;UINT32 len=0;
                if(FAILED(view->GetAt(i,md.out()))||!md||FAILED(md->get_Data(buf.out()))||!buf||
                   FAILED(buf->get_Length(&len))||len<2||!As(buf.p,IID_BufferByteAccess,bytes)||
                   FAILED(bytes->Buffer(&p))||!p||p[0]!=0x07||p[1]!=0x21)continue;
                Candidate c{};
                if(FAILED(a->get_BluetoothAddress(&c.addr)))continue;
                a->get_RawSignalStrengthInDBm(&c.rssi);c.type=-1;
                Ptr<IReceivedArgs2> a2;
                if(As(args,Id(L"{12D9C87B-0399-5F0E-A348-53B02B6B162E}"),a2))a2->get_AddressType(&c.type);
                std::lock_guard<std::mutex> guard(lock);candidates[c.addr]=c;
            }
        }catch(...){}
        return S_OK;
    }
};
struct NotifyHandler : CallbackBase {
    std::mutex lock;
    jbl_speaker::Info info;
    NotifyHandler():CallbackBase(Id(L"{C1F420F6-6292-5760-A2C9-9DDF98683CFC}")){}
    HRESULT STDMETHODCALLTYPE Invoke(IInspectable*,IInspectable* args) override {
        try{
            Ptr<IChanged> a;Ptr<IBufferAbi> buf;Ptr<IBufferByteAccessAbi> bytes;BYTE* p=nullptr;UINT32 len=0;
            if(As(args,Id(L"{D21BDB54-06E3-4ED8-A263-ACFAC8BA7313}"),a)&&
                SUCCEEDED(a->Value(buf.out()))&&buf&&SUCCEEDED(buf->get_Length(&len))&&
                As(buf.p,IID_BufferByteAccess,bytes)&&SUCCEEDED(bytes->Buffer(&p))&&p){
                std::lock_guard<std::mutex> guard(lock);info.Feed(p,len);
            }
        }catch(...){}
        return S_OK;
    }
};
std::vector<Candidate> Scan(){
    Ptr<IInspectable> obj;Ptr<IWatcher> watcher;
    const wchar_t* cls=L"Windows.Devices.Bluetooth.Advertisement.BluetoothLEAdvertisementWatcher";
    HSTRING str=nullptr;
    if(FAILED(WindowsCreateString(cls,static_cast<UINT32>(wcslen(cls)),&str)))return {};
    RoActivateInstance(str,obj.out());WindowsDeleteString(str);
    if(!obj||!As(obj.p,IID_Watcher,watcher))return {};
    Ptr<ScanHandler> handler;handler.p=new ScanHandler;
    EventRegistrationToken token{};
    watcher->put_ScanningMode(1);
    if(FAILED(watcher->add_Received(handler.p,&token)))return {};
    if(SUCCEEDED(watcher->Start())){Sleep(2500);watcher->Stop();}
    watcher->remove_Received(token);
    std::vector<Candidate> out;
    {std::lock_guard<std::mutex> lock(handler->lock);for(auto& kv:handler->candidates)out.push_back(kv.second);}
    std::sort(out.begin(),out.end(),[](const Candidate& a,const Candidate& b){return a.rssi>b.rssi;});
    return out;
}
bool First(Async<IInspectable*>* op,const GUID& iid,Ptr<IInspectable>& item,ULONGLONG deadline){
    Ptr<IInspectable> result;Ptr<IResult> r;Ptr<IView> view;INT32 status=-1;UINT32 size=0;
    return Await(op,result.out(),deadline)&&As(result.p,iid,r)&&SUCCEEDED(r->get_Status(&status))&&status==0&&
        SUCCEEDED(r->get_Items(view.out()))&&view&&SUCCEEDED(view->get_Size(&size))&&size==1&&
        SUCCEEDED(view->GetAt(0,item.out()))&&item;
}
bool Characteristic(IService3* svc,const wchar_t* uuid,Ptr<IChar>& ch,ULONGLONG deadline){
    Ptr<Async<IInspectable*>> op;Ptr<IInspectable> obj;
    return SUCCEEDED(svc->CharacteristicsUuidCached(Id(uuid),1,op.out()))&&
        First(op.p,Id(L"{1194945C-B257-4F3E-9DB7-F68BC9A9AEF2}"),obj,deadline)&&
        As(obj.p,Id(L"{59CB50C1-5934-4F68-A198-EB864FA44E6B}"),ch);
}
UINT64 ReverseAddress(UINT64 n){UINT64 out=0;for(int i=0;i<6;++i){out=(out<<8)|(n&255);n>>=8;}return out;}
bool Query(const Candidate& c,const Speaker& speaker,DeviceStatus& status,ULONGLONG deadline){
    const wchar_t* cls=L"Windows.Devices.Bluetooth.BluetoothLEDevice";
    Ptr<Async<IInspectable*>> start;Ptr<IInspectable> device;Ptr<IDevice3> dev;
    if(c.type==0||c.type==1){
        Ptr<IStatics2> f;
        if(!Factory(cls,Id(L"{5F12C06B-3BAC-43E8-AD16-563271BD41C2}"),f)||
           FAILED(f->FromAddressType(c.addr,c.type,start.out())))return false;
    }else{
        Ptr<IStatics> f;
        if(!Factory(cls,Id(L"{C8CF1A19-F0B6-4BF0-8689-41303DE2D9F4}"),f)||
           FAILED(f->FromBluetoothAddressAsync(c.addr,start.out())))return false;
    }
    if(!Await(start.p,device.out(),deadline)||!As(device.p,Id(L"{AEE9E493-44AC-40DC-AF33-B2C13C01CA46}"),dev))return false;
    Ptr<Async<IInspectable*>> svcOp;Ptr<IInspectable> service;Ptr<IService3> svc;
    if(FAILED(dev->ServicesUuidCached(Id(L"{65786365-6C70-6F69-6E74-2E636F6D0000}"),1,svcOp.out()))||
        !First(svcOp.p,Id(L"{171DD3EE-016D-419D-838A-576CF475A3D8}"),service,deadline)||
        !As(service.p,Id(L"{B293A950-0C53-437C-A9B3-5C3210C6E569}"),svc))return false;
    Ptr<IChar> rx,tx;
    if(!Characteristic(svc.p,L"{65786365-6C70-6F69-6E74-2E636F6D0001}",rx,deadline)||
       !Characteristic(svc.p,L"{65786365-6C70-6F69-6E74-2E636F6D0002}",tx,deadline))return false;
    INT32 rp=0,tp=0;
    if(FAILED(rx->Properties(&rp))||FAILED(tx->Properties(&tp))||!(rp&16)||!(tp&12))return false;
    Ptr<IBufferFactory> bf;Ptr<IBufferAbi> buf;Ptr<IBufferByteAccessAbi> bytes;BYTE* p=nullptr;
    if(!Factory(L"Windows.Storage.Streams.Buffer",Id(L"{71AF914D-C10F-484B-BC50-14BC623B3A27}"),bf)||
        FAILED(bf->Create(3,buf.out()))||!buf||FAILED(buf->put_Length(3))||
        !As(buf.p,IID_BufferByteAccess,bytes)||FAILED(bytes->Buffer(&p))||!p)return false;
    std::copy(std::begin(jbl_speaker::request),std::end(jbl_speaker::request),p);
    Ptr<NotifyHandler> handler;handler.p=new NotifyHandler;
    EventRegistrationToken token{};
    if(FAILED(rx->AddChanged(handler.p,&token)))return false;
    struct Subscription { IChar* ch;EventRegistrationToken tok;~Subscription(){ch->RemoveChanged(tok);} } sub{rx.p,token};
    Ptr<Async<INT32>> notify;INT32 result=-1;
    if(FAILED(rx->WriteCccd(1,notify.out()))||!Await(notify.p,&result,deadline)||result!=0)return false;
    Ptr<Async<INT32>> write; result=-1;
    if(FAILED(tx->WriteOption(buf.p,(tp&8)?0:1,write.out()))||!Await(write.p,&result,deadline)||result!=0)return false;
    while(GetTickCount64()<deadline){
        {
            std::lock_guard<std::mutex> lock(handler->lock);
            const auto& info=handler->info;
            if(info.pid==jbl_speaker::product&&info.percent>=0&&
                (c.addr==speaker.address||info.address==speaker.address||ReverseAddress(info.address)==speaker.address)){
                status.name=speaker.name;status.key=speaker.key;status.kind=DeviceKind::Speaker;
                status.percent=info.percent;status.charging=info.charging;status.via=L"Bluetooth LE";status.graceSec=120;
                return true;
            }
        }
        Sleep(20);
    }
    return false;
}
}
namespace {
// The original PR #274 poll, run off the poll thread (discovery can take up to 10 s).
void RefreshJblSpeakers(PollContext& ctx) {
    // Only the JBL worker (or the separate --list process) accesses this cache.
    static std::map<std::wstring, Candidate> known;
    auto speakers = Speakers();
    if (speakers.empty()) { known.clear(); return; }
    RadioInit init;
    if (FAILED(init.hr)) return;

    for (const auto& speaker : speakers) {
        DeviceStatus status;
        // A previously verified BLE address avoids the discovery scan on routine refreshes.
        auto old = known.find(speaker.key);
        if (old != known.end()) {
            if (Query(old->second, speaker, status, GetTickCount64() + 4000)) {
                ctx.out.push_back(status);
                continue;
            }
            known.erase(old);
        }
        // The first discovery keeps the original hardware-tested scan and fallback.
        auto list = Scan();
        if (std::none_of(list.begin(), list.end(), [&](const Candidate& c) { return c.addr == speaker.address; }))
            list.push_back({speaker.address, -1, -127});
        const auto deadline = GetTickCount64() + 10000;
        for (const auto& c : list) {
            if (GetTickCount64() >= deadline) break;
            if (Query(c, speaker, status, deadline)) {
                known[speaker.key] = c;
                ctx.out.push_back(status);
                break;
            }
        }
    }
}
}  // namespace

// Connecting to the speaker takes seconds (and up to 10 s for the first discovery), so a background thread
// refreshes it about once a minute and every poll reports the latest result, like marshall.cpp and
// gamesir_ble.cpp. A one-shot --list usually finishes before the first reading.
void PollJblSpeaker(PollContext& ctx) {
    static std::mutex lock;
    static std::vector<DeviceStatus> cached;
    static ULONGLONG cachedAt = 0;
    static bool started = false;
    {
        std::lock_guard<std::mutex> g(lock);
        if (!started) {
            started = true;
            HANDLE t = CreateThread(nullptr, 0, [](LPVOID) -> DWORD {
                for (;;) {
                    PollContext c;
                    RefreshJblSpeakers(c);
                    {
                        std::lock_guard<std::mutex> g2(lock);
                        cached = c.out;
                        cachedAt = GetTickCount64();
                    }
                    Sleep(60 * 1000);
                }
            }, nullptr, 0, nullptr);
            if (t) CloseHandle(t);
        }
        if (cachedAt && GetTickCount64() - cachedAt < 5 * 60 * 1000)
            for (const auto& s : cached) ctx.out.push_back(s);
    }
}
