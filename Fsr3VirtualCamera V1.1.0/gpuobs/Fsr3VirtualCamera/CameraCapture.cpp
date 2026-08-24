#include "CameraCapture.h"
#include <mferror.h>
#include <mfobjects.h>
#include <algorithm>
using Microsoft::WRL::ComPtr;

std::vector<CameraInfo> CameraCapture::Enumerate(){
    std::vector<CameraInfo> out; ComPtr<IMFAttributes> attr; IMFActivate** devs=nullptr; UINT32 count=0;
    if(FAILED(MFCreateAttributes(&attr,1))) return out;
    attr->SetGUID(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE,MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID);
    if(FAILED(MFEnumDeviceSources(attr.Get(),&devs,&count))) return out;
    for(UINT32 i=0;i<count;++i){ WCHAR* name=nullptr; UINT32 n=0; WCHAR* link=nullptr; UINT32 l=0;
        devs[i]->GetAllocatedString(MF_DEVSOURCE_ATTRIBUTE_FRIENDLY_NAME,&name,&n);
        devs[i]->GetAllocatedString(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_SYMBOLIC_LINK,&link,&l);
        out.push_back({name?name:L"Camera",link?link:L""}); CoTaskMemFree(name); CoTaskMemFree(link); devs[i]->Release(); }
    CoTaskMemFree(devs); return out;
}

bool CameraCapture::Open(const CameraInfo& info,std::wstring& error){
    Close(); ComPtr<IMFAttributes> attr; ComPtr<IMFMediaSource> source;
    if(FAILED(MFCreateAttributes(&attr,2))){error=L"MFCreateAttributes failed";return false;}
    attr->SetGUID(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE,MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID);
    attr->SetString(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_SYMBOLIC_LINK,info.symbolicLink.c_str());
    HRESULT hr=MFCreateDeviceSource(attr.Get(),&source); if(FAILED(hr)){error=L"Could not open camera";return false;}

    ComPtr<IMFAttributes> readerAttr;
    hr=MFCreateAttributes(&readerAttr,4);
    if(FAILED(hr)){error=L"Could not create source-reader attributes";return false;}
    readerAttr->SetUINT32(MF_SOURCE_READER_ENABLE_ADVANCED_VIDEO_PROCESSING, TRUE);
    readerAttr->SetUINT32(MF_READWRITE_DISABLE_CONVERTERS, FALSE);
    readerAttr->SetUINT32(MF_READWRITE_ENABLE_HARDWARE_TRANSFORMS, TRUE);
    readerAttr->SetUINT32(MF_SOURCE_READER_DISABLE_DXVA, FALSE);

    hr=MFCreateSourceReaderFromMediaSource(source.Get(),readerAttr.Get(),&reader_);
    if(FAILED(hr)){error=L"Could not create source reader";return false;}

    ComPtr<IMFMediaType> type;
    MFCreateMediaType(&type);
    type->SetGUID(MF_MT_MAJOR_TYPE,MFMediaType_Video);
    type->SetGUID(MF_MT_SUBTYPE,MFVideoFormat_RGB32);
    hr=reader_->SetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM,nullptr,type.Get());
    if(FAILED(hr)){
        error=L"Media Foundation could not convert this camera to RGB32";
        Close();
        return false;
    }
    ComPtr<IMFMediaType> actual; reader_->GetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM,&actual);
    UINT32 a=0,b=0; MFGetAttributeSize(actual.Get(),MF_MT_FRAME_SIZE,&a,&b); w_=a;h_=b;
    return w_&&h_;
}

bool CameraCapture::ReadFrame(std::vector<uint8_t>& bgra,uint32_t& w,uint32_t& h,uint32_t& stride,std::wstring& error){
    if(!reader_){error=L"Camera not open";return false;}
    DWORD si=0,flags=0; LONGLONG ts=0; ComPtr<IMFSample> sample;
    HRESULT hr=reader_->ReadSample(MF_SOURCE_READER_FIRST_VIDEO_STREAM,0,&si,&flags,&ts,&sample);
    if(FAILED(hr)){ error=L"Camera ReadSample failed"; return false; }
    if(flags & MF_SOURCE_READERF_CURRENTMEDIATYPECHANGED){
        ComPtr<IMFMediaType> actual;
        if(SUCCEEDED(reader_->GetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM,&actual))){
            UINT32 a=0,b=0;
            if(SUCCEEDED(MFGetAttributeSize(actual.Get(),MF_MT_FRAME_SIZE,&a,&b)) && a && b){w_=a;h_=b;}
        }
    }
    if(!sample) return false;

    ComPtr<IMFMediaBuffer> buf;
    if(FAILED(sample->ConvertToContiguousBuffer(&buf))){error=L"Could not access camera buffer";return false;}

    const size_t rowBytes = static_cast<size_t>(w_) * 4;
    bgra.resize(rowBytes * h_);

    // Prefer IMF2DBuffer so we respect the real scanline pitch instead of
    // assuming Canon/Media Foundation handed us a tightly packed RGB32 image.
    ComPtr<IMF2DBuffer> twoD;
    if(SUCCEEDED(buf.As(&twoD))){
        BYTE* scan0=nullptr; LONG pitch=0;
        if(SUCCEEDED(twoD->Lock2D(&scan0,&pitch))){
            BYTE* src = scan0;
            if(pitch < 0) src = scan0 + static_cast<ptrdiff_t>(h_-1) * pitch;
            for(uint32_t y=0;y<h_;++y){
                const BYTE* row = (pitch >= 0) ? (scan0 + static_cast<ptrdiff_t>(y)*pitch)
                                               : (src - static_cast<ptrdiff_t>(y)*pitch);
                memcpy(bgra.data()+static_cast<size_t>(y)*rowBytes,row,rowBytes);
            }
            twoD->Unlock2D();
            w=w_; h=h_; stride=static_cast<uint32_t>(rowBytes);
            return true;
        }
    }

    BYTE* p=nullptr; DWORD max=0,len=0;
    if(FAILED(buf->Lock(&p,&max,&len))){error=L"Could not lock camera buffer";return false;}
    const size_t need=rowBytes*h_;
    if(len<need){buf->Unlock();error=L"Short camera frame";return false;}
    memcpy(bgra.data(),p,need);
    buf->Unlock();
    w=w_; h=h_; stride=static_cast<uint32_t>(rowBytes);
    return true;
}

void CameraCapture::Close(){reader_.Reset();w_=h_=0;}
