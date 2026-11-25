#pragma once

#include <windows.h>
#include <uiautomation.h>
#include <atomic>
#include <string>

#pragma comment(lib, "uiautomationcore.lib")
#pragma comment(lib, "oleaut32.lib")

namespace windowcue {

// Single-HWND settings popover accessibility contract. Exposes the startup
// toggle switch and GitHub link to UI Automation without creating extra child
// HWNDs or extra Direct2D swap-chain render targets.
struct SettingsAccessibilityHost {
    virtual ~SettingsAccessibilityHost() = default;
    virtual HWND PopoverHwnd() const = 0;
    virtual std::wstring ItemName(int id) const = 0;
    virtual RECT ItemScreenRect(int id) const = 0;
    virtual bool ItemEnabled(int id) const = 0;
    virtual int FocusedItem() const = 0;
    virtual ToggleState StartupToggleState() const = 0;
    virtual void FocusItem(int id) = 0;
    virtual void InvokeItem(int id) = 0;
};

class SettingsItemAutomation;

class SettingsPopoverAutomation final : public IRawElementProviderSimple,
                                        public IRawElementProviderFragmentRoot,
                                        public IRawElementProviderFragment {
public:
    explicit SettingsPopoverAutomation(SettingsAccessibilityHost* host) : host_(host) {}

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id, void** out) override {
        if (!out) return E_POINTER;
        *out = nullptr;
        if (id == __uuidof(IUnknown) || id == __uuidof(IRawElementProviderSimple)) {
            *out = static_cast<IRawElementProviderSimple*>(this);
        } else if (id == __uuidof(IRawElementProviderFragmentRoot)) {
            *out = static_cast<IRawElementProviderFragmentRoot*>(this);
        } else if (id == __uuidof(IRawElementProviderFragment)) {
            *out = static_cast<IRawElementProviderFragment*>(this);
        } else {
            return E_NOINTERFACE;
        }
        AddRef();
        return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++refs_; }
    ULONG STDMETHODCALLTYPE Release() override {
        ULONG n = --refs_;
        if (!n) delete this;
        return n;
    }

    HRESULT STDMETHODCALLTYPE get_ProviderOptions(ProviderOptions* value) override {
        if (!value) return E_POINTER;
        *value = ProviderOptions_ServerSideProvider;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetPatternProvider(PATTERNID, IUnknown** value) override {
        if (!value) return E_POINTER;
        *value = nullptr;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetPropertyValue(PROPERTYID id, VARIANT* value) override {
        if (!value) return E_POINTER;
        VariantInit(value);
        HWND hwnd = host_ ? host_->PopoverHwnd() : nullptr;
        if (!hwnd || !IsWindow(hwnd)) return UIA_E_ELEMENTNOTAVAILABLE;
        if (id == UIA_NamePropertyId) {
            wchar_t name[256]{};
            GetWindowTextW(hwnd, name, 256);
            value->vt = VT_BSTR;
            value->bstrVal = SysAllocString(name);
            return value->bstrVal ? S_OK : E_OUTOFMEMORY;
        }
        if (id == UIA_ControlTypePropertyId) {
            value->vt = VT_I4;
            value->lVal = UIA_PaneControlTypeId;
        } else if (id == UIA_AutomationIdPropertyId) {
            value->vt = VT_BSTR;
            value->bstrVal = SysAllocString(L"SettingsPopover");
            return value->bstrVal ? S_OK : E_OUTOFMEMORY;
        } else if (id == UIA_IsControlElementPropertyId || id == UIA_IsContentElementPropertyId) {
            value->vt = VT_BOOL;
            value->boolVal = VARIANT_TRUE;
        }
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_HostRawElementProvider(IRawElementProviderSimple** value) override {
        HWND hwnd = host_ ? host_->PopoverHwnd() : nullptr;
        return hwnd ? UiaHostProviderFromHwnd(hwnd, value) : UIA_E_ELEMENTNOTAVAILABLE;
    }

    HRESULT STDMETHODCALLTYPE Navigate(NavigateDirection direction,
                                       IRawElementProviderFragment** out) override;
    HRESULT STDMETHODCALLTYPE GetRuntimeId(SAFEARRAY** out) override {
        if (!out) return E_POINTER;
        *out = nullptr;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_BoundingRectangle(UiaRect* out) override {
        if (!out) return E_POINTER;
        HWND hwnd = host_ ? host_->PopoverHwnd() : nullptr;
        if (!hwnd || !IsWindow(hwnd)) return UIA_E_ELEMENTNOTAVAILABLE;
        RECT r{};
        GetWindowRect(hwnd, &r);
        *out = {static_cast<double>(r.left), static_cast<double>(r.top),
                static_cast<double>(r.right - r.left), static_cast<double>(r.bottom - r.top)};
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetEmbeddedFragmentRoots(SAFEARRAY** out) override {
        if (!out) return E_POINTER;
        *out = nullptr;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE SetFocus() override {
        HWND hwnd = host_ ? host_->PopoverHwnd() : nullptr;
        if (!hwnd || !IsWindow(hwnd)) return UIA_E_ELEMENTNOTAVAILABLE;
        ::SetFocus(hwnd);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_FragmentRoot(IRawElementProviderFragmentRoot** out) override {
        if (!out) return E_POINTER;
        *out = this;
        AddRef();
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE ElementProviderFromPoint(double x, double y,
                                                       IRawElementProviderFragment** out) override;
    HRESULT STDMETHODCALLTYPE GetFocus(IRawElementProviderFragment** out) override;

private:
    std::atomic<ULONG> refs_{1};
    SettingsAccessibilityHost* host_{};
};

class SettingsItemAutomation final : public IRawElementProviderSimple,
                                     public IRawElementProviderFragment,
                                     public IToggleProvider,
                                     public IInvokeProvider {
public:
    SettingsItemAutomation(SettingsAccessibilityHost* host, int itemId)
        : host_(host), itemId_(itemId) {}

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id, void** out) override {
        if (!out) return E_POINTER;
        *out = nullptr;
        if (id == __uuidof(IUnknown) || id == __uuidof(IRawElementProviderSimple)) {
            *out = static_cast<IRawElementProviderSimple*>(this);
        } else if (id == __uuidof(IRawElementProviderFragment)) {
            *out = static_cast<IRawElementProviderFragment*>(this);
        } else if (id == __uuidof(IToggleProvider) && itemId_ == 0) {
            *out = static_cast<IToggleProvider*>(this);
        } else if (id == __uuidof(IInvokeProvider)) {
            *out = static_cast<IInvokeProvider*>(this);
        } else {
            return E_NOINTERFACE;
        }
        AddRef();
        return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++refs_; }
    ULONG STDMETHODCALLTYPE Release() override {
        ULONG n = --refs_;
        if (!n) delete this;
        return n;
    }

    HRESULT STDMETHODCALLTYPE get_ProviderOptions(ProviderOptions* value) override {
        if (!value) return E_POINTER;
        *value = ProviderOptions_ServerSideProvider;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetPatternProvider(PATTERNID id, IUnknown** value) override {
        if (!value) return E_POINTER;
        *value = nullptr;
        if (id == UIA_TogglePatternId && itemId_ == 0) {
            *value = static_cast<IToggleProvider*>(this);
        } else if (id == UIA_InvokePatternId) {
            *value = static_cast<IInvokeProvider*>(this);
        }
        if (*value) AddRef();
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetPropertyValue(PROPERTYID id, VARIANT* value) override {
        if (!value) return E_POINTER;
        VariantInit(value);
        HWND hwnd = host_ ? host_->PopoverHwnd() : nullptr;
        if (!hwnd || !IsWindow(hwnd)) return UIA_E_ELEMENTNOTAVAILABLE;
        if (id == UIA_NamePropertyId) {
            std::wstring name = host_->ItemName(itemId_);
            value->vt = VT_BSTR;
            value->bstrVal = SysAllocString(name.c_str());
            return value->bstrVal ? S_OK : E_OUTOFMEMORY;
        }
        if (id == UIA_ControlTypePropertyId) {
            value->vt = VT_I4;
            value->lVal = itemId_ == 0 ? UIA_CheckBoxControlTypeId : UIA_HyperlinkControlTypeId;
        } else if (id == UIA_AutomationIdPropertyId) {
            value->vt = VT_BSTR;
            value->bstrVal = SysAllocString(itemId_ == 0 ? L"StartupSwitch" : L"GitHubProject");
            return value->bstrVal ? S_OK : E_OUTOFMEMORY;
        } else if (id == UIA_IsEnabledPropertyId || id == UIA_IsKeyboardFocusablePropertyId) {
            value->vt = VT_BOOL;
            value->boolVal = host_->ItemEnabled(itemId_) ? VARIANT_TRUE : VARIANT_FALSE;
        } else if (id == UIA_HasKeyboardFocusPropertyId) {
            value->vt = VT_BOOL;
            value->boolVal = (GetFocus() == hwnd && host_->FocusedItem() == itemId_)
                ? VARIANT_TRUE : VARIANT_FALSE;
        } else if (id == UIA_IsControlElementPropertyId || id == UIA_IsContentElementPropertyId) {
            value->vt = VT_BOOL;
            value->boolVal = VARIANT_TRUE;
        }
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_HostRawElementProvider(IRawElementProviderSimple** value) override {
        if (!value) return E_POINTER;
        *value = nullptr;
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE Navigate(NavigateDirection direction,
                                       IRawElementProviderFragment** out) override {
        if (!out) return E_POINTER;
        *out = nullptr;
        if (!host_) return S_OK;
        if (direction == NavigateDirection_Parent) {
            *out = new SettingsPopoverAutomation(host_);
        } else if (direction == NavigateDirection_NextSibling && itemId_ == 0) {
            *out = new SettingsItemAutomation(host_, 1);
        } else if (direction == NavigateDirection_PreviousSibling && itemId_ == 1) {
            *out = new SettingsItemAutomation(host_, 0);
        }
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetRuntimeId(SAFEARRAY** out) override {
        if (!out) return E_POINTER;
        SAFEARRAY* array = SafeArrayCreateVector(VT_I4, 0, 2);
        if (!array) return E_OUTOFMEMORY;
        int ids[2] = {UiaAppendRuntimeId, 100 + itemId_};
        for (LONG i = 0; i < 2; ++i) SafeArrayPutElement(array, &i, &ids[i]);
        *out = array;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_BoundingRectangle(UiaRect* out) override {
        if (!out) return E_POINTER;
        if (!host_ || !IsWindow(host_->PopoverHwnd())) return UIA_E_ELEMENTNOTAVAILABLE;
        RECT r = host_->ItemScreenRect(itemId_);
        *out = {static_cast<double>(r.left), static_cast<double>(r.top),
                static_cast<double>(r.right - r.left), static_cast<double>(r.bottom - r.top)};
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetEmbeddedFragmentRoots(SAFEARRAY** out) override {
        if (!out) return E_POINTER;
        *out = nullptr;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE SetFocus() override {
        if (!host_ || !IsWindow(host_->PopoverHwnd())) return UIA_E_ELEMENTNOTAVAILABLE;
        host_->FocusItem(itemId_);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_FragmentRoot(IRawElementProviderFragmentRoot** out) override {
        if (!out) return E_POINTER;
        *out = new SettingsPopoverAutomation(host_);
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE Toggle() override { return Invoke(); }
    HRESULT STDMETHODCALLTYPE get_ToggleState(ToggleState* state) override {
        if (!state) return E_POINTER;
        if (!host_ || !IsWindow(host_->PopoverHwnd())) return UIA_E_ELEMENTNOTAVAILABLE;
        *state = host_->StartupToggleState();
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE Invoke() override {
        if (!host_ || !IsWindow(host_->PopoverHwnd())) return UIA_E_ELEMENTNOTAVAILABLE;
        if (!host_->ItemEnabled(itemId_)) return UIA_E_ELEMENTNOTENABLED;
        host_->InvokeItem(itemId_);
        return S_OK;
    }

private:
    std::atomic<ULONG> refs_{1};
    SettingsAccessibilityHost* host_{};
    int itemId_{};
};

inline HRESULT STDMETHODCALLTYPE SettingsPopoverAutomation::Navigate(
    NavigateDirection direction, IRawElementProviderFragment** out) {
    if (!out) return E_POINTER;
    *out = nullptr;
    if (!host_) return S_OK;
    if (direction == NavigateDirection_FirstChild) *out = new SettingsItemAutomation(host_, 0);
    else if (direction == NavigateDirection_LastChild) *out = new SettingsItemAutomation(host_, 1);
    return S_OK;
}

inline HRESULT STDMETHODCALLTYPE SettingsPopoverAutomation::ElementProviderFromPoint(
    double x, double y, IRawElementProviderFragment** out) {
    if (!out) return E_POINTER;
    *out = nullptr;
    if (!host_) return S_OK;
    POINT pt{static_cast<LONG>(x), static_cast<LONG>(y)};
    for (int id : {0, 1}) {
        RECT r = host_->ItemScreenRect(id);
        if (PtInRect(&r, pt)) {
            *out = new SettingsItemAutomation(host_, id);
            return S_OK;
        }
    }
    *out = this;
    AddRef();
    return S_OK;
}

inline HRESULT STDMETHODCALLTYPE SettingsPopoverAutomation::GetFocus(
    IRawElementProviderFragment** out) {
    if (!out) return E_POINTER;
    *out = nullptr;
    if (!host_) return S_OK;
    int focused = host_->FocusedItem();
    if (focused == 0 || focused == 1) {
        *out = new SettingsItemAutomation(host_, focused);
    }
    return S_OK;
}

inline LRESULT SettingsPopoverAutomationObject(HWND hwnd, WPARAM wp, LPARAM lp,
                                               SettingsAccessibilityHost* host) {
    auto* provider = new SettingsPopoverAutomation(host);
    LRESULT result = UiaReturnRawElementProvider(hwnd, wp, lp, provider);
    provider->Release();
    return result;
}

inline void NotifySettingsToggleState(SettingsAccessibilityHost* host,
                                      ToggleState oldState, ToggleState newState) {
    if (!host || oldState == newState || !UiaClientsAreListening()) return;
    auto* item = new SettingsItemAutomation(host, 0);
    VARIANT before{}, after{};
    before.vt = after.vt = VT_I4;
    before.lVal = oldState;
    after.lVal = newState;
    UiaRaiseAutomationPropertyChangedEvent(item, UIA_ToggleToggleStatePropertyId, before, after);
    item->Release();
}

} // namespace windowcue
