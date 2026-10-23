#pragma once
#include <uiautomation.h>
#include <atomic>
#pragma comment(lib, "uiautomationcore.lib")
#pragma comment(lib, "oleaut32.lib")

namespace windowcue {
// The actual native HWND continues to own focus, tab order and lifetime.
// This augments its accessible contract without drawing a second themed button.
class ControlAutomation final : public IRawElementProviderSimple,
                                public IToggleProvider, public IInvokeProvider {
public:
    ControlAutomation(HWND hwnd, bool toggle) : hwnd_(hwnd), toggle_(toggle) {}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id, void** out) override {
        if(!out) return E_POINTER; *out=nullptr;
        if(id==__uuidof(IUnknown) || id==__uuidof(IRawElementProviderSimple))
            *out=static_cast<IRawElementProviderSimple*>(this);
        else if(id==__uuidof(IToggleProvider) && toggle_) *out=static_cast<IToggleProvider*>(this);
        else if(id==__uuidof(IInvokeProvider)) *out=static_cast<IInvokeProvider*>(this);
        else return E_NOINTERFACE;
        AddRef();return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++refs_; }
    ULONG STDMETHODCALLTYPE Release() override { ULONG n=--refs_;if(!n) delete this;return n; }
    HRESULT STDMETHODCALLTYPE get_ProviderOptions(ProviderOptions* value) override {
        if(!value) return E_POINTER;*value=ProviderOptions_ServerSideProvider;return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetPatternProvider(PATTERNID id,IUnknown** value) override {
        if(!value) return E_POINTER;*value=nullptr;
        if(id==UIA_TogglePatternId && toggle_) *value=static_cast<IToggleProvider*>(this);
        else if(id==UIA_InvokePatternId) *value=static_cast<IInvokeProvider*>(this);
        if(*value) AddRef();return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetPropertyValue(PROPERTYID id,VARIANT* value) override {
        if(!value) return E_POINTER;VariantInit(value);
        if(!IsWindow(hwnd_)) return UIA_E_ELEMENTNOTAVAILABLE;
        if(id==UIA_NamePropertyId) {
            wchar_t name[512]{};GetWindowTextW(hwnd_,name,512);
            value->vt=VT_BSTR;value->bstrVal=SysAllocString(name);
            if(!value->bstrVal) return E_OUTOFMEMORY;
        } else if(id==UIA_ControlTypePropertyId) {
            value->vt=VT_I4;value->lVal=toggle_ ? UIA_CheckBoxControlTypeId : UIA_HyperlinkControlTypeId;
        } else if(id==UIA_IsKeyboardFocusablePropertyId || id==UIA_IsEnabledPropertyId) {
            value->vt=VT_BOOL;value->boolVal=IsWindowEnabled(hwnd_) ? VARIANT_TRUE : VARIANT_FALSE;
        } else if(id==UIA_AutomationIdPropertyId) {
            value->vt=VT_BSTR;value->bstrVal=SysAllocString(toggle_?L"StartupSwitch":L"GitHubProject");
        }
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_HostRawElementProvider(IRawElementProviderSimple** value) override {
        return UiaHostProviderFromHwnd(hwnd_,value);
    }
    HRESULT STDMETHODCALLTYPE Toggle() override { return Invoke(); }
    HRESULT STDMETHODCALLTYPE get_ToggleState(ToggleState* state) override {
        if(!state) return E_POINTER;if(!IsWindow(hwnd_)) return UIA_E_ELEMENTNOTAVAILABLE;
        auto value=reinterpret_cast<INT_PTR>(GetPropW(hwnd_,L"WindowCue.ToggleState"));
        *state=value==2 ? ToggleState_On : value==1 ? ToggleState_Off : ToggleState_Indeterminate;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE Invoke() override {
        if(!IsWindow(hwnd_)) return UIA_E_ELEMENTNOTAVAILABLE;
        if(!IsWindowEnabled(hwnd_)) return UIA_E_ELEMENTNOTENABLED;
        return PostMessageW(hwnd_,BM_CLICK,0,0) ? S_OK : E_FAIL;
    }
private:
    std::atomic<ULONG> refs_{1};
    HWND hwnd_{}; bool toggle_{};
};
inline LRESULT AutomationObject(HWND hwnd,WPARAM wp,LPARAM lp,bool toggle) {
    auto* provider=new ControlAutomation(hwnd,toggle);
    LRESULT result=UiaReturnRawElementProvider(hwnd,wp,lp,provider);
    provider->Release();return result;
}
inline void NotifyToggle(HWND hwnd,bool on) {
    INT_PTR old=reinterpret_cast<INT_PTR>(GetPropW(hwnd,L"WindowCue.ToggleState"));
    INT_PTR next=on ? 2 : 1;
    SetPropW(hwnd,L"WindowCue.ToggleState",reinterpret_cast<HANDLE>(next));
    if(old && old!=next && UiaClientsAreListening()) {
        auto* provider=new ControlAutomation(hwnd,true);
        VARIANT before{},after{};before.vt=after.vt=VT_I4;
        before.lVal=old==2 ? ToggleState_On : ToggleState_Off;
        after.lVal=on ? ToggleState_On : ToggleState_Off;
        UiaRaiseAutomationPropertyChangedEvent(provider,UIA_ToggleToggleStatePropertyId,before,after);
        provider->Release();
    }
}
} // namespace windowcue
