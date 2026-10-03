#include "common.h"

namespace ttp::flac {
HMODULE module{};
StandardContent standard_content() {
    for (const auto name :
         {L"ttpctrl.dll", L"soundcore.dll", static_cast<const wchar_t *>(nullptr)}) {
        auto host = GetModuleHandleW(name);
        if (host)
            if (auto p = GetProcAddress(host, "CreateStdContent"))
                return reinterpret_cast<StandardContent>(p);
    }
    return nullptr;
}
class ReaderFactory final : public ReaderCreator {
    LONG references_{1};
    bool tta_;

  public:
    explicit ReaderFactory(bool tta) : tta_(tta) {}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void **out) override {
        if (!out)
            return E_POINTER;
        *out = nullptr;
        if (!same(iid, IID_IUnknown) && !same(iid, cat_reader))
            return E_NOINTERFACE;
        *out = static_cast<ReaderCreator *>(this);
        AddRef();
        return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return InterlockedIncrement(&references_); }
    ULONG STDMETHODCALLTYPE Release() override {
        const auto n = InterlockedDecrement(&references_);
        if (!n)
            delete this;
        return n;
    }
    HRESULT STDMETHODCALLTYPE Create(void **out) override { return make_reader(tta_, out); }
    HRESULT STDMETHODCALLTYPE Name(wchar_t **out) override {
        return text(tta_ ? L"TTA Reader" : L"FLAC Reader", out);
    }
    HRESULT STDMETHODCALLTYPE Extensions(wchar_t **out) override {
        return text(tta_ ? L"TTA 音频文件(*.tta)" : L"FLAC 音频文件(*.flac;*.fla)", out);
    }
};
class SoundAddIn final : public AddIn {
    LONG references_{1};

  public:
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void **out) override {
        if (!out)
            return E_POINTER;
        *out = nullptr;
        if (!same(iid, IID_IUnknown) && !same(iid, iid_addin))
            return E_NOINTERFACE;
        *out = static_cast<AddIn *>(this);
        AddRef();
        return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return InterlockedIncrement(&references_); }
    ULONG STDMETHODCALLTYPE Release() override {
        const auto n = InterlockedDecrement(&references_);
        if (!n)
            delete this;
        return n;
    }
    HRESULT STDMETHODCALLTYPE Enum(DWORD index, GUID *category, void **out) override {
        if (!out)
            return E_POINTER;
        *out = nullptr;
        if (!category)
            return E_POINTER;
        if (index > 1)
            return E_INVALIDARG;
        return protect([&]() -> HRESULT {
            *out = static_cast<ReaderCreator *>(new ReaderFactory(index == 1));
            *category = cat_reader;
            return S_OK;
        });
    }
};
} // namespace ttp::flac
extern "C" HRESULT WINAPI ttpGetSoundAddIn(void **out) {
    if (!out)
        return E_POINTER;
    *out = nullptr;
    return ttp::flac::protect([&]() -> HRESULT {
        *out = static_cast<ttp::flac::AddIn *>(new ttp::flac::SoundAddIn);
        return S_OK;
    });
}
BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        ttp::flac::module = instance;
        DisableThreadLibraryCalls(instance);
    }
    return TRUE;
}
