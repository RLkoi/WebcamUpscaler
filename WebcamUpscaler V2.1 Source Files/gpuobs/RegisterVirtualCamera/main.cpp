#include <windows.h>
#include <mfapi.h>
#include <mfvirtualcamera.h>
#include <wrl/client.h>
#include <string>
#pragma comment(lib,"mfplat.lib")
#pragma comment(lib,"mfsensorgroup.lib")
using Microsoft::WRL::ComPtr;
static constexpr wchar_t kSourceClsid[]=L"{8A091617-43A6-4CC5-A29F-53F64FBB58F3}";
int wmain(int argc,wchar_t**argv){
    CoInitializeEx(nullptr,COINIT_MULTITHREADED);MFStartup(MF_VERSION);ComPtr<IMFVirtualCamera> cam;
    HRESULT hr=MFCreateVirtualCamera(MFVirtualCameraType_SoftwareCameraSource,MFVirtualCameraLifetime_System,MFVirtualCameraAccess_CurrentUser,L"FSR3 Upscaled",kSourceClsid,nullptr,0,&cam);
    if(SUCCEEDED(hr)){if(argc>1 && std::wstring(argv[1])==L"remove")hr=cam->Remove();else hr=cam->Start(nullptr);}
    if(FAILED(hr)){wchar_t msg[128];swprintf_s(msg,L"Virtual camera operation failed: 0x%08X\n",(unsigned)hr);OutputDebugStringW(msg);}
    MFShutdown();CoUninitialize();return FAILED(hr)?1:0;
}
