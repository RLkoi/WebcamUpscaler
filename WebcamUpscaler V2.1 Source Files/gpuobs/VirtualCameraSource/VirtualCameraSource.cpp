#include <windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mferror.h>
#include <wrl/client.h>
#include <atomic>
#include <vector>
#include <string>
#include "../Shared/SharedFrame.h"

#pragma comment(lib,"mfplat.lib")
#pragma comment(lib,"mfuuid.lib")
#pragma comment(lib,"ole32.lib")

using Microsoft::WRL::ComPtr;
extern "C" IMAGE_DOS_HEADER __ImageBase;
// {8A091617-43A6-4CC5-A29F-53F64FBB58F3}
const GUID CLSID_Fsr3VirtualCameraSource = { 0x8a091617, 0x43a6, 0x4cc5, { 0xa2, 0x9f, 0x53, 0xf6, 0x4f, 0xbb, 0x58, 0xf3 } };
static std::atomic<long> gObjects{0},gLocks{0};

class SharedMediaSource;

class SharedMediaStream final : public IMFMediaStream {
    std::atomic<ULONG> refs_{1}; ComPtr<IMFMediaEventQueue> events_; ComPtr<IMFStreamDescriptor> desc_; SharedMediaSource* source_{};
public:
    SharedMediaStream(SharedMediaSource* s, IMFStreamDescriptor* d):desc_(d),source_(s){MFCreateEventQueue(&events_);gObjects++;}
    ~SharedMediaStream(){if(events_)events_->Shutdown();gObjects--;}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid,void**ppv) override {if(!ppv)return E_POINTER;*ppv=nullptr;if(riid==IID_IUnknown||riid==__uuidof(IMFMediaEventGenerator)||riid==__uuidof(IMFMediaStream)){*ppv=static_cast<IMFMediaStream*>(this);AddRef();return S_OK;}return E_NOINTERFACE;}
    ULONG STDMETHODCALLTYPE AddRef() override{return ++refs_;} ULONG STDMETHODCALLTYPE Release() override{auto r=--refs_;if(!r)delete this;return r;}
    HRESULT STDMETHODCALLTYPE GetEvent(DWORD f,IMFMediaEvent**e) override{return events_->GetEvent(f,e);} HRESULT STDMETHODCALLTYPE BeginGetEvent(IMFAsyncCallback*c,IUnknown*s) override{return events_->BeginGetEvent(c,s);} HRESULT STDMETHODCALLTYPE EndGetEvent(IMFAsyncResult*r,IMFMediaEvent**e) override{return events_->EndGetEvent(r,e);} HRESULT STDMETHODCALLTYPE QueueEvent(MediaEventType t,REFGUID g,HRESULT h,const PROPVARIANT*v) override{return events_->QueueEventParamVar(t,g,h,v);}
    HRESULT STDMETHODCALLTYPE GetMediaSource(IMFMediaSource** pp) override;
    HRESULT STDMETHODCALLTYPE GetStreamDescriptor(IMFStreamDescriptor**pp) override{if(!pp)return E_POINTER;*pp=desc_.Get();(*pp)->AddRef();return S_OK;}
    HRESULT STDMETHODCALLTYPE RequestSample(IUnknown* token) override;
    HRESULT Start(){return events_->QueueEventParamVar(MEStreamStarted,GUID_NULL,S_OK,nullptr);} HRESULT Stop(){return events_->QueueEventParamVar(MEStreamStopped,GUID_NULL,S_OK,nullptr);}
};

class SharedMediaSource final : public IMFMediaSource {
    std::atomic<ULONG> refs_{1}; ComPtr<IMFMediaEventQueue> events_; ComPtr<IMFPresentationDescriptor> pd_; SharedMediaStream* stream_{}; fvc::SharedFrameReader reader_; bool shutdown_=false; uint32_t width_=1920,height_=1080,fps_=60;
public:
    SharedMediaSource(){gObjects++;MFCreateEventQueue(&events_);reader_.Open();CreateDescriptors();}
    ~SharedMediaSource(){Shutdown();gObjects--;}
    HRESULT CreateDescriptors(){
        fvc::SharedFrameHeader h{};std::vector<uint8_t> tmp;if(reader_.Read(tmp,h)){width_=h.width;height_=h.height;fps_=h.fpsNumerator?h.fpsNumerator:60;}
        ComPtr<IMFMediaType> mt; HRESULT hr=MFCreateMediaType(&mt);if(FAILED(hr))return hr;mt->SetGUID(MF_MT_MAJOR_TYPE,MFMediaType_Video);mt->SetGUID(MF_MT_SUBTYPE,MFVideoFormat_RGB32);MFSetAttributeSize(mt.Get(),MF_MT_FRAME_SIZE,width_,height_);MFSetAttributeRatio(mt.Get(),MF_MT_FRAME_RATE,fps_,1);MFSetAttributeRatio(mt.Get(),MF_MT_PIXEL_ASPECT_RATIO,1,1);mt->SetUINT32(MF_MT_INTERLACE_MODE,MFVideoInterlace_Progressive);mt->SetUINT32(MF_MT_DEFAULT_STRIDE,width_*4);
        IMFMediaType* mts[]={mt.Get()};ComPtr<IMFStreamDescriptor> sd;hr=MFCreateStreamDescriptor(1,1,mts,&sd);if(FAILED(hr))return hr;stream_=new SharedMediaStream(this,sd.Get());IMFStreamDescriptor* sds[]={sd.Get()};hr=MFCreatePresentationDescriptor(1,sds,&pd_);if(SUCCEEDED(hr))pd_->SelectStream(0);return hr;
    }
    HRESULT ReadFrame(std::vector<uint8_t>& data,fvc::SharedFrameHeader& h){return reader_.Read(data,h)?S_OK:MF_E_NOTACCEPTING;}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid,void**ppv) override{if(!ppv)return E_POINTER;*ppv=nullptr;if(riid==IID_IUnknown||riid==__uuidof(IMFMediaEventGenerator)||riid==__uuidof(IMFMediaSource)){*ppv=static_cast<IMFMediaSource*>(this);AddRef();return S_OK;}return E_NOINTERFACE;}
    ULONG STDMETHODCALLTYPE AddRef() override{return ++refs_;} ULONG STDMETHODCALLTYPE Release() override{auto r=--refs_;if(!r)delete this;return r;}
    HRESULT STDMETHODCALLTYPE GetEvent(DWORD f,IMFMediaEvent**e) override{return shutdown_?MF_E_SHUTDOWN:events_->GetEvent(f,e);} HRESULT STDMETHODCALLTYPE BeginGetEvent(IMFAsyncCallback*c,IUnknown*s) override{return shutdown_?MF_E_SHUTDOWN:events_->BeginGetEvent(c,s);} HRESULT STDMETHODCALLTYPE EndGetEvent(IMFAsyncResult*r,IMFMediaEvent**e) override{return shutdown_?MF_E_SHUTDOWN:events_->EndGetEvent(r,e);} HRESULT STDMETHODCALLTYPE QueueEvent(MediaEventType t,REFGUID g,HRESULT h,const PROPVARIANT*v) override{return shutdown_?MF_E_SHUTDOWN:events_->QueueEventParamVar(t,g,h,v);}
    HRESULT STDMETHODCALLTYPE GetCharacteristics(DWORD*p) override{if(!p)return E_POINTER;*p=MFMEDIASOURCE_IS_LIVE;return S_OK;}
    HRESULT STDMETHODCALLTYPE CreatePresentationDescriptor(IMFPresentationDescriptor**pp) override{if(!pp)return E_POINTER;if(shutdown_)return MF_E_SHUTDOWN;return pd_->Clone(pp);}
    HRESULT STDMETHODCALLTYPE Start(IMFPresentationDescriptor*,const GUID*,const PROPVARIANT*) override{if(shutdown_)return MF_E_SHUTDOWN;PROPVARIANT v;PropVariantInit(&v);events_->QueueEventParamUnk(MENewStream,GUID_NULL,S_OK,stream_);stream_->Start();return events_->QueueEventParamVar(MESourceStarted,GUID_NULL,S_OK,&v);}
    HRESULT STDMETHODCALLTYPE Stop() override{if(shutdown_)return MF_E_SHUTDOWN;stream_->Stop();return events_->QueueEventParamVar(MESourceStopped,GUID_NULL,S_OK,nullptr);} HRESULT STDMETHODCALLTYPE Pause() override{return MF_E_INVALID_STATE_TRANSITION;}
    HRESULT STDMETHODCALLTYPE Shutdown() override{if(shutdown_)return S_OK;shutdown_=true;if(stream_){stream_->Release();stream_=nullptr;}if(events_)events_->Shutdown();reader_.Close();return S_OK;}
};

HRESULT SharedMediaStream::GetMediaSource(IMFMediaSource**pp){if(!pp)return E_POINTER;*pp=source_;source_->AddRef();return S_OK;}
HRESULT SharedMediaStream::RequestSample(IUnknown* token){
    std::vector<uint8_t> data;fvc::SharedFrameHeader h{};HRESULT hr=source_->ReadFrame(data,h);if(FAILED(hr))return hr;
    ComPtr<IMFMediaBuffer> b;hr=MFCreateMemoryBuffer((DWORD)data.size(),&b);if(FAILED(hr))return hr;BYTE*p=nullptr;DWORD max=0,cur=0;b->Lock(&p,&max,&cur);memcpy(p,data.data(),data.size());b->Unlock();b->SetCurrentLength((DWORD)data.size());
    ComPtr<IMFSample> s;MFCreateSample(&s);s->AddBuffer(b.Get());if(token)s->SetUnknown(MFSampleExtension_Token,token);LONGLONG dur=10000000LL/(h.fpsNumerator?h.fpsNumerator:60);s->SetSampleDuration(dur);s->SetSampleTime((LONGLONG)h.frameNumber*dur);return events_->QueueEventParamUnk(MEMediaSample,GUID_NULL,S_OK,s.Get());
}

class Factory final:public IClassFactory{std::atomic<ULONG>refs_{1};public:HRESULT STDMETHODCALLTYPE QueryInterface(REFIID r,void**p)override{if(!p)return E_POINTER;*p=nullptr;if(r==IID_IUnknown||r==IID_IClassFactory){*p=this;AddRef();return S_OK;}return E_NOINTERFACE;}ULONG STDMETHODCALLTYPE AddRef()override{return ++refs_;}ULONG STDMETHODCALLTYPE Release()override{auto x=--refs_;if(!x)delete this;return x;}HRESULT STDMETHODCALLTYPE CreateInstance(IUnknown*outer,REFIID riid,void**ppv)override{if(outer)return CLASS_E_NOAGGREGATION;auto*s=new SharedMediaSource();HRESULT hr=s->QueryInterface(riid,ppv);s->Release();return hr;}HRESULT STDMETHODCALLTYPE LockServer(BOOL b)override{b?++gLocks:--gLocks;return S_OK;}};

extern "C" BOOL WINAPI DllMain(HINSTANCE, DWORD, LPVOID){return TRUE;}
extern "C" HRESULT __stdcall DllGetClassObject(REFCLSID c,REFIID r,void**pp){if(c!=CLSID_Fsr3VirtualCameraSource)return CLASS_E_CLASSNOTAVAILABLE;auto*f=new Factory();HRESULT hr=f->QueryInterface(r,pp);f->Release();return hr;}
extern "C" HRESULT __stdcall DllCanUnloadNow(){return(gObjects.load()==0&&gLocks.load()==0)?S_OK:S_FALSE;}

static std::wstring GuidString(){wchar_t b[64]{};StringFromGUID2(CLSID_Fsr3VirtualCameraSource,b,64);return b;}
extern "C" HRESULT __stdcall DllRegisterServer(){wchar_t path[MAX_PATH]{};GetModuleFileNameW((HMODULE)&__ImageBase,path,MAX_PATH);std::wstring key=L"Software\\Classes\\CLSID\\"+GuidString()+L"\\InProcServer32";HKEY h{};if(RegCreateKeyExW(HKEY_CURRENT_USER,key.c_str(),0,nullptr,0,KEY_WRITE,nullptr,&h,nullptr)!=ERROR_SUCCESS)return E_FAIL;RegSetValueExW(h,nullptr,0,REG_SZ,(BYTE*)path,DWORD((wcslen(path)+1)*sizeof(wchar_t)));const wchar_t both[]=L"Both";RegSetValueExW(h,L"ThreadingModel",0,REG_SZ,(BYTE*)both,sizeof(both));RegCloseKey(h);return S_OK;}
extern "C" HRESULT __stdcall DllUnregisterServer(){std::wstring key=L"Software\\Classes\\CLSID\\"+GuidString();RegDeleteTreeW(HKEY_CURRENT_USER,key.c_str());return S_OK;}
