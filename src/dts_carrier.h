#pragma once
#include "common.h"

namespace ttp::flac {
// A byte view of losslessly unpacked FLAC. This is not a temporary file and
// never exposes the outer file's name or metadata writer to the DTS reader.
struct CarrierBytes {
    virtual HRESULT ReadAt(uint64_t, void *, ULONG, ULONG *) = 0;
    virtual uint64_t Size() const = 0;
protected:
    ~CarrierBytes() = default;
};
class CarrierStream final : public IStream {
    LONG refs_{1};
    CarrierBytes &source_;
    uint64_t position_{};
public:
    explicit CarrierStream(CarrierBytes &source) : source_(source) {}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id, void **out) override {
        if (!out) return E_POINTER;
        *out = nullptr;
        if (!IsEqualIID(id,IID_IUnknown) && !IsEqualIID(id,IID_IStream) &&
            !IsEqualIID(id,IID_ISequentialStream)) return E_NOINTERFACE;
        *out=static_cast<IStream*>(this);AddRef();return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return InterlockedIncrement(&refs_); }
    ULONG STDMETHODCALLTYPE Release() override { auto n=InterlockedDecrement(&refs_);if(!n)delete this;return n; }
    HRESULT STDMETHODCALLTYPE Read(void *out, ULONG n, ULONG *read) override {
        if(read)*read=0;
        if(n&&!out)return STG_E_INVALIDPOINTER;
        ULONG got{};auto hr=source_.ReadAt(position_,out,n,&got);
        if(FAILED(hr))return hr;
        position_+=got;if(read)*read=got;return got==n?S_OK:S_FALSE;
    }
    HRESULT STDMETHODCALLTYPE Seek(LARGE_INTEGER offset,DWORD origin,ULARGE_INTEGER *out) override {
        uint64_t base{};
        if(origin==STREAM_SEEK_CUR)base=position_;
        else if(origin==STREAM_SEEK_END)base=source_.Size();
        else if(origin!=STREAM_SEEK_SET)return STG_E_INVALIDFUNCTION;
        uint64_t target{};
        if(offset.QuadPart<0){
            const auto distance=uint64_t(-(offset.QuadPart+1))+1;
            if(distance>base)return STG_E_INVALIDFUNCTION;target=base-distance;
        }else{
            if(uint64_t(offset.QuadPart)>UINT64_MAX-base)return STG_E_INVALIDFUNCTION;
            target=base+uint64_t(offset.QuadPart);
        }
        if(target>source_.Size())return STG_E_INVALIDFUNCTION;
        position_=target;if(out)out->QuadPart=target;return S_OK;
    }
    HRESULT STDMETHODCALLTYPE Stat(STATSTG *s,DWORD flags) override {
        if(!s)return STG_E_INVALIDPOINTER;
        *s={};s->type=STGTY_STREAM;s->grfMode=STGM_READ;s->cbSize.QuadPart=source_.Size();
        return flags&STATFLAG_NONAME?S_OK:text(L"FLAC-DTS\\<decoded>.dts",&s->pwcsName);
    }
    HRESULT STDMETHODCALLTYPE Write(const void*,ULONG,ULONG *n) override {if(n)*n=0;return STG_E_ACCESSDENIED;}
    HRESULT STDMETHODCALLTYPE SetSize(ULARGE_INTEGER) override {return STG_E_ACCESSDENIED;}
    HRESULT STDMETHODCALLTYPE CopyTo(IStream*,ULARGE_INTEGER,ULARGE_INTEGER*,ULARGE_INTEGER*) override {return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE Commit(DWORD) override {return STG_E_ACCESSDENIED;}
    HRESULT STDMETHODCALLTYPE Revert() override {return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE LockRegion(ULARGE_INTEGER,ULARGE_INTEGER,DWORD) override {return STG_E_INVALIDFUNCTION;}
    HRESULT STDMETHODCALLTYPE UnlockRegion(ULARGE_INTEGER,ULARGE_INTEGER,DWORD) override {return STG_E_INVALIDFUNCTION;}
    HRESULT STDMETHODCALLTYPE Clone(IStream **out) override {if(!out)return E_POINTER;*out=nullptr;return E_NOTIMPL;}
};

class CarrierDecoder {
    HMODULE module_{};
public:
    ComPtr<Reader> reader;
    ~CarrierDecoder(){Close();}
    void Close(){reader.reset();if(module_){FreeLibrary(module_);module_=nullptr;}}
    void Open(HMODULE owner,CarrierBytes &bytes,DWORD flags) {
        wchar_t name[32768]{};
        const auto n=GetModuleFileNameW(owner,name,DWORD(std::size(name)));
        require(n&&n<std::size(name),HRESULT_FROM_WIN32(ERROR_BAD_PATHNAME));
        std::wstring path(name,n);const auto slash=path.find_last_of(L"\\/");
        require(slash!=std::wstring::npos,E_UNEXPECTED);
        path.resize(slash+1);path+=L"ttp_ac3dts.dll";
        module_=LoadLibraryExW(path.c_str(),nullptr,LOAD_WITH_ALTERED_SEARCH_PATH);
        if(!module_)throw Failure{HRESULT_FROM_WIN32(GetLastError())};
        using Get=HRESULT(WINAPI*)(void**);
        auto get=reinterpret_cast<Get>(GetProcAddress(module_,"ttpGetSoundAddIn"));
        require(get,HRESULT_FROM_WIN32(ERROR_PROC_NOT_FOUND));
        ComPtr<AddIn> add;check(get(reinterpret_cast<void**>(add.put())));require(bool(add));
        for(DWORD i=0;i<32;++i){
            GUID category{};ComPtr<ReaderCreator> factory;
            const auto hr=add->Enum(i,&category,reinterpret_cast<void**>(factory.put()));
            if(FAILED(hr))break;
            if(!factory||!IsEqualIID(category,cat_reader))continue;
            wchar_t *extensions{};const auto e=factory->Extensions(&extensions);
            const bool dts=SUCCEEDED(e)&&extensions&&wcsstr(extensions,L"*.dts");
            CoTaskMemFree(extensions);if(!dts)continue;
            check(factory->Create(reinterpret_cast<void**>(reader.put())));require(bool(reader));
            ComPtr<IStream> stream(new CarrierStream(bytes));
            check(reader->Open(stream.p,flags&3));
            WAVEFORMATEX *format{};check(reader->Format(&format));
            bool pcm=false;
            if(format){
                WORD tag=format->wFormatTag;
                if(tag==WAVE_FORMAT_EXTENSIBLE&&format->cbSize==22){
                    const auto &id=reinterpret_cast<WAVEFORMATEXTENSIBLE*>(format)->SubFormat;
                    const GUID integer{1,0,0x10,{0x80,0,0,0xaa,0,0x38,0x9b,0x71}};
                    const GUID floating{3,0,0x10,{0x80,0,0,0xaa,0,0x38,0x9b,0x71}};
                    tag=IsEqualIID(id,integer)?1:IsEqualIID(id,floating)?3:0;
                }
                pcm=(tag==1||tag==3)&&format->cbSize<=22&&format->nChannels&&
                    format->nChannels<=32&&format->nSamplesPerSec&&format->nBlockAlign&&
                    format->nAvgBytesPerSec;
            }
            CoTaskMemFree(format);
            // An old sibling can return compressed DTS instead of final PCM.
            // Reject it rather than recursively passing an undecoded carrier.
            require(pcm,HRESULT_FROM_WIN32(ERROR_NOT_SUPPORTED));return;
        }
        throw Failure{HRESULT_FROM_WIN32(ERROR_NOT_SUPPORTED)};
    }
};
} // namespace ttp::flac
