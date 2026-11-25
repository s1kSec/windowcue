#pragma once

#include <d2d1.h>
#include <dwrite.h>
#include <wincodec.h>
#include <wrl/client.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <map>
#include <string>
#include <vector>

#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "dwrite.lib")
#pragma comment(lib, "windowscodecs.lib")

namespace windowcue {

using Microsoft::WRL::ComPtr;

enum class VectorIcon {
    Settings,
    Sun,
    Moon,
    GitHub,
    Minimize,
    Maximize,
    Restore,
    Close,
    Keyboard,
    WindowFallback,
    ExternalArrow
};

enum class TextRole {
    Brand,
    CardTitle,
    CardMeta,
    Badge,
    DockTitle,
    DockPath,
    Body,
    BodyMedium,
    Small,
    Keycap
};

// Shared device-independent graphics factories so opening secondary popovers
// never pays cold Direct2D / DirectWrite / WIC initialization latency.
struct SharedGraphicsFactories {
    ComPtr<ID2D1Factory> d2d;
    ComPtr<IDWriteFactory> dwrite;
    ComPtr<IWICImagingFactory> wic;
    ComPtr<ID2D1StrokeStyle> roundStroke;
    ComPtr<ID2D1StrokeStyle> flatStroke;
    std::map<VectorIcon, ComPtr<ID2D1PathGeometry>> paths;

    bool Ensure() {
        if (!d2d && FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED,
                                             d2d.GetAddressOf()))) {
            return false;
        }
        if (!dwrite && FAILED(DWriteCreateFactory(
                DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                reinterpret_cast<IUnknown**>(dwrite.GetAddressOf())))) {
            return false;
        }
        if (!roundStroke) {
            D2D1_STROKE_STYLE_PROPERTIES sp = D2D1::StrokeStyleProperties();
            sp.startCap = sp.endCap = sp.dashCap = D2D1_CAP_STYLE_ROUND;
            sp.lineJoin = D2D1_LINE_JOIN_ROUND;
            d2d->CreateStrokeStyle(sp, nullptr, 0, &roundStroke);
        }
        if (!flatStroke) {
            D2D1_STROKE_STYLE_PROPERTIES sp = D2D1::StrokeStyleProperties();
            sp.startCap = sp.endCap = D2D1_CAP_STYLE_FLAT;
            sp.lineJoin = D2D1_LINE_JOIN_MITER;
            d2d->CreateStrokeStyle(sp, nullptr, 0, &flatStroke);
        }
        return true;
    }

    bool EnsureWic() {
        if (wic) return true;
        return SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory, nullptr,
                                          CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wic)));
    }

    void Reset() {
        paths.clear();
        roundStroke.Reset();
        flatStroke.Reset();
        wic.Reset();
        dwrite.Reset();
        d2d.Reset();
    }
};

inline SharedGraphicsFactories& SharedFactories() {
    static SharedGraphicsFactories instance;
    return instance;
}

// Pure GPU Direct2D + DirectWrite per-HWND renderer.
// Coordinates enter in caller physical pixels at the Win32 boundary and are
// rendered in floating-point DIPs with physical-pixel-snapped hairline strokes.
class RenderContext {
public:
    RenderContext() = default;
    RenderContext(const RenderContext&) = delete;
    RenderContext& operator=(const RenderContext&) = delete;
    ~RenderContext() { Shutdown(); }

    void Shutdown() {
        Discard();
        formats_.clear();
        roleFormats_.clear();
        layouts_.clear();
        hwnd_ = nullptr;
    }

    bool Begin(HWND hwnd, UINT dpi, const RECT& dirty, bool software = false) {
        if (hwnd_ != hwnd || software_ != software) {
            Discard();
            hwnd_ = hwnd;
            software_ = software;
        }
        dpi_ = dpi ? dpi : 96;
        factor_ = static_cast<float>(dpi_) / 96.f;
        auto& shared = SharedFactories();
        if (!shared.Ensure()) return false;

        RECT client{};
        GetClientRect(hwnd, &client);
        D2D1_SIZE_U size = D2D1::SizeU(std::max(1L, client.right), std::max(1L, client.bottom));
        bool fresh = !hwndTarget_;
        if (fresh) {
            auto props = D2D1::RenderTargetProperties(
                software ? D2D1_RENDER_TARGET_TYPE_SOFTWARE : D2D1_RENDER_TARGET_TYPE_DEFAULT,
                D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE),
                static_cast<float>(dpi_), static_cast<float>(dpi_));
            auto window = D2D1::HwndRenderTargetProperties(
                hwnd, size, D2D1_PRESENT_OPTIONS_RETAIN_CONTENTS);
            HRESULT hr = shared.d2d->CreateHwndRenderTarget(props, window, &hwndTarget_);
            if (FAILED(hr) && !software) {
                props.type = D2D1_RENDER_TARGET_TYPE_SOFTWARE;
                hr = shared.d2d->CreateHwndRenderTarget(props, window, &hwndTarget_);
            }
            if (FAILED(hr)) {
                lastError_ = hr;
                return false;
            }
            ++generation_;
        } else {
            auto old = hwndTarget_->GetPixelSize();
            if (old.width != size.width || old.height != size.height) {
                if (FAILED(hwndTarget_->Resize(size))) {
                    Discard();
                    return false;
                }
                fresh = true;
            }
        }
        target_ = hwndTarget_.Get();
        target_->SetDpi(static_cast<float>(dpi_), static_cast<float>(dpi_));
        target_->SetTransform(D2D1::Matrix3x2F::Identity());
        target_->SetAntialiasMode(D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
        target_->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_CLEARTYPE);
        target_->BeginDraw();
        target_->PushAxisAlignedClip(Rect(fresh ? client : dirty), D2D1_ANTIALIAS_MODE_ALIASED);
        freshFrame_ = fresh;
        drawing_ = true;
        return true;
    }

    // Direct2D DC render target path for WM_PRINTCLIENT / PrintWindow callers,
    // using the exact same vector drawing commands as live HWND rendering.
    bool BeginDC(HDC hdc, UINT dpi, const RECT& bounds) {
        if (!hdc || bounds.right <= bounds.left || bounds.bottom <= bounds.top) return false;
        Discard();
        dpi_ = dpi ? dpi : 96;
        factor_ = static_cast<float>(dpi_) / 96.f;
        auto& shared = SharedFactories();
        if (!shared.Ensure()) return false;
        auto props = D2D1::RenderTargetProperties(
            D2D1_RENDER_TARGET_TYPE_DEFAULT,
            D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE),
            static_cast<float>(dpi_), static_cast<float>(dpi_),
            D2D1_RENDER_TARGET_USAGE_NONE, D2D1_FEATURE_LEVEL_DEFAULT);
        HRESULT hr = shared.d2d->CreateDCRenderTarget(&props, &dcTarget_);
        if (FAILED(hr)) {
            props.type = D2D1_RENDER_TARGET_TYPE_SOFTWARE;
            hr = shared.d2d->CreateDCRenderTarget(&props, &dcTarget_);
        }
        if (FAILED(hr)) {
            lastError_ = hr;
            return false;
        }
        hr = dcTarget_->BindDC(hdc, &bounds);
        if (FAILED(hr)) {
            dcTarget_.Reset();
            props.type = D2D1_RENDER_TARGET_TYPE_SOFTWARE;
            hr = shared.d2d->CreateDCRenderTarget(&props, &dcTarget_);
            if (SUCCEEDED(hr)) hr = dcTarget_->BindDC(hdc, &bounds);
        }
        if (FAILED(hr)) {
            lastError_ = hr;
            dcTarget_.Reset();
            return false;
        }
        target_ = dcTarget_.Get();
        target_->SetDpi(static_cast<float>(dpi_), static_cast<float>(dpi_));
        target_->SetTransform(D2D1::Matrix3x2F::Identity());
        target_->SetAntialiasMode(D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
        target_->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_CLEARTYPE);
        target_->BeginDraw();
        target_->PushAxisAlignedClip(Rect(bounds), D2D1_ANTIALIAS_MODE_ALIASED);
        freshFrame_ = true;
        drawing_ = true;
        return true;
    }

    HRESULT End() {
        if (!drawing_) return lastError_;
        target_->PopAxisAlignedClip();
        drawing_ = false;
        lastError_ = target_->EndDraw();
        if (dcTarget_) {
            brush_.Reset();
            regionLayer_.Reset();
            regionGeometry_.Reset();
            images_.clear();
            dcTarget_.Reset();
            target_ = nullptr;
        } else if (lastError_ == D2DERR_RECREATE_TARGET) {
            Discard();
        }
        return lastError_;
    }

    void Discard() {
        brush_.Reset();
        regionLayer_.Reset();
        regionGeometry_.Reset();
        hwndTarget_.Reset();
        dcTarget_.Reset();
        target_ = nullptr;
        images_.clear();
        drawing_ = false;
    }

    unsigned Generation() const { return generation_; }
    HRESULT LastError() const { return lastError_; }
    bool FreshFrame() const { return freshFrame_; }

    bool ValidateGeometry() {
        auto& shared = SharedFactories();
        if (!shared.Ensure()) return false;
        for (const char* data : {SettingsPath(), MoonPath(), GitHubPath()}) {
            ComPtr<ID2D1PathGeometry> geometry;
            if (!BuildPath(shared.d2d.Get(), data, &geometry)) return false;
            D2D1_RECT_F bounds{};
            float area{};
            if (FAILED(geometry->GetBounds(nullptr, &bounds)) ||
                FAILED(geometry->ComputeArea(nullptr, &area)) || !std::isfinite(area) || area < 1 ||
                bounds.left < -2 || bounds.top < -2 || bounds.right > 26 || bounds.bottom > 26) {
                return false;
            }
        }
        return true;
    }

    D2D1_RECT_F Rect(const RECT& r, float insetPixels = 0.f) const {
        return D2D1::RectF((r.left + insetPixels) / factor_, (r.top + insetPixels) / factor_,
                           (r.right - insetPixels) / factor_, (r.bottom - insetPixels) / factor_);
    }

    float Dip(float pixels) const { return pixels / factor_; }
    float SnapPixels(float dip) const { return std::max(1.f, std::round(dip * factor_)); }

    ID2D1SolidColorBrush* Brush(COLORREF color, float opacity = 1.f) {
        auto value = D2D1::ColorF(GetRValue(color) / 255.f, GetGValue(color) / 255.f,
                                  GetBValue(color) / 255.f, opacity);
        if (!brush_) target_->CreateSolidColorBrush(value, &brush_);
        else brush_->SetColor(value);
        return brush_.Get();
    }

    void Fill(const RECT& r, COLORREF color, float opacity = 1.f) {
        target_->FillRectangle(Rect(r), Brush(color, opacity));
    }

    void Rounded(const RECT& r, float radiusPixels, COLORREF color, float opacity = 1.f) {
        float rad = Dip(radiusPixels);
        auto rect = D2D1::RoundedRect(Rect(r), rad, rad);
        target_->FillRoundedRectangle(rect, Brush(color, opacity));
    }

    void RoundedDip(const RECT& r, float radiusDip, COLORREF color, float opacity = 1.f) {
        auto rect = D2D1::RoundedRect(Rect(r), radiusDip, radiusDip);
        target_->FillRoundedRectangle(rect, Brush(color, opacity));
    }

    void Stroke(const RECT& r, float radiusPixels, float strokePixels, COLORREF color,
                float opacity = 1.f) {
        // Snap stroke to integer physical pixels and inset by half the stroke
        // width so every edge aligns cleanly at 100%-200% DPI without blur.
        float snapped = std::max(1.f, std::round(strokePixels));
        float rad = std::max(0.f, Dip(radiusPixels) - Dip(snapped) * 0.5f);
        auto rect = D2D1::RoundedRect(Rect(r, snapped * 0.5f), rad, rad);
        target_->DrawRoundedRectangle(rect, Brush(color, opacity), Dip(snapped));
    }

    void StrokeDip(const RECT& r, float radiusDip, float strokeDip, COLORREF color,
                   float opacity = 1.f) {
        float snapped = SnapPixels(strokeDip);
        float strokeD = Dip(snapped);
        float rad = std::max(0.f, radiusDip - strokeD * 0.5f);
        auto rect = D2D1::RoundedRect(Rect(r, snapped * 0.5f), rad, rad);
        target_->DrawRoundedRectangle(rect, Brush(color, opacity), strokeD);
    }

    void Line(float x1Px, float y1Px, float x2Px, float y2Px, float strokePx, COLORREF color,
              float opacity = 1.f) {
        float snapped = std::max(1.f, std::round(strokePx));
        target_->DrawLine({Dip(x1Px), Dip(y1Px)}, {Dip(x2Px), Dip(y2Px)},
                          Brush(color, opacity), Dip(snapped));
    }

    void Circle(float cxPx, float cyPx, float radiusPx, COLORREF color, float opacity = 1.f) {
        target_->FillEllipse(D2D1::Ellipse({Dip(cxPx), Dip(cyPx)}, Dip(radiusPx), Dip(radiusPx)),
                             Brush(color, opacity));
    }

    void PushClip(const RECT& r) {
        target_->PushAxisAlignedClip(Rect(r), D2D1_ANTIALIAS_MODE_ALIASED);
    }
    void PopClip() { target_->PopAxisAlignedClip(); }

    bool PushRegion(const std::vector<RECT>& rectangles) {
        if (freshFrame_ || rectangles.size() < 2) return false;
        auto& shared = SharedFactories();
        regionGeometry_.Reset();
        if (FAILED(shared.d2d->CreatePathGeometry(&regionGeometry_))) return false;
        ComPtr<ID2D1GeometrySink> sink;
        if (FAILED(regionGeometry_->Open(&sink))) return false;
        sink->SetFillMode(D2D1_FILL_MODE_WINDING);
        for (const RECT& rect : rectangles) {
            auto r = Rect(rect);
            sink->BeginFigure({r.left, r.top}, D2D1_FIGURE_BEGIN_FILLED);
            D2D1_POINT_2F points[] = {{r.right, r.top}, {r.right, r.bottom}, {r.left, r.bottom}};
            sink->AddLines(points, 3);
            sink->EndFigure(D2D1_FIGURE_END_CLOSED);
        }
        if (FAILED(sink->Close())) return false;
        if (!regionLayer_ && FAILED(target_->CreateLayer(nullptr, &regionLayer_))) return false;
        auto parameters = D2D1::LayerParameters(
            D2D1::InfiniteRect(), regionGeometry_.Get(), D2D1_ANTIALIAS_MODE_ALIASED,
            D2D1::Matrix3x2F::Identity(), 1.f, nullptr,
            D2D1_LAYER_OPTIONS_INITIALIZE_FOR_CLEARTYPE);
        target_->PushLayer(parameters, regionLayer_.Get());
        return true;
    }
    void PopRegion() { target_->PopLayer(); }

    // Typed DirectWrite text styles (no GDI HFONT creation/GetObjectW overhead).
    IDWriteTextFormat* RoleFormat(TextRole role) {
        auto& shared = SharedFactories();
        if (!shared.Ensure()) return nullptr;
        auto& format = roleFormats_[role];
        if (format) return format.Get();
        float sizeDip = 12.f;
        DWRITE_FONT_WEIGHT weight = DWRITE_FONT_WEIGHT_NORMAL;
        switch (role) {
        case TextRole::Brand:      sizeDip = 12.5f; weight = DWRITE_FONT_WEIGHT_SEMI_BOLD; break;
        case TextRole::CardTitle:  sizeDip = 12.0f; weight = DWRITE_FONT_WEIGHT_SEMI_BOLD; break;
        case TextRole::CardMeta:   sizeDip = 10.5f; weight = DWRITE_FONT_WEIGHT_NORMAL; break;
        case TextRole::Badge:      sizeDip = 10.2f; weight = DWRITE_FONT_WEIGHT_SEMI_BOLD; break;
        case TextRole::DockTitle:  sizeDip = 12.5f; weight = DWRITE_FONT_WEIGHT_SEMI_BOLD; break;
        case TextRole::DockPath:   sizeDip = 10.5f; weight = DWRITE_FONT_WEIGHT_NORMAL; break;
        case TextRole::Body:       sizeDip = 12.0f; weight = DWRITE_FONT_WEIGHT_NORMAL; break;
        case TextRole::BodyMedium: sizeDip = 12.0f; weight = DWRITE_FONT_WEIGHT_SEMI_BOLD; break;
        case TextRole::Small:      sizeDip = 11.0f; weight = DWRITE_FONT_WEIGHT_NORMAL; break;
        case TextRole::Keycap:     sizeDip = 11.5f; weight = DWRITE_FONT_WEIGHT_SEMI_BOLD; break;
        }
        const wchar_t* family =
#ifdef LANG_EN
            L"Segoe UI";
#else
            L"Microsoft YaHei UI";
#endif
        shared.dwrite->CreateTextFormat(
            family, nullptr, weight, DWRITE_FONT_STYLE_NORMAL,
            DWRITE_FONT_STRETCH_NORMAL, sizeDip, L"", &format);
        return format.Get();
    }

    int MeasureRoleWidth(const std::wstring& text, TextRole role, UINT dpi) {
        if (text.empty()) return 0;
        auto& shared = SharedFactories();
        auto* format = RoleFormat(role);
        if (!format) return 0;
        ComPtr<IDWriteTextLayout> layout;
        if (FAILED(shared.dwrite->CreateTextLayout(
                text.c_str(), static_cast<UINT32>(text.size()), format,
                4096.f, 128.f, &layout))) {
            return 0;
        }
        layout->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
        DWRITE_TEXT_METRICS metrics{};
        layout->GetMetrics(&metrics);
        float scale = static_cast<float>(dpi ? dpi : 96) / 96.f;
        return static_cast<int>(std::ceil(metrics.widthIncludingTrailingWhitespace * scale));
    }

    int MeasureRoleHeight(const std::wstring& text, TextRole role, int widthPixels, UINT dpi) {
        if (text.empty()) return 0;
        auto& shared = SharedFactories();
        auto* format = RoleFormat(role);
        if (!format) return 0;
        float scale = static_cast<float>(dpi ? dpi : 96) / 96.f;
        ComPtr<IDWriteTextLayout> layout;
        if (FAILED(shared.dwrite->CreateTextLayout(
                text.c_str(), static_cast<UINT32>(text.size()), format,
                std::max(1.f, widthPixels / scale), 10000.f, &layout))) {
            return 0;
        }
        layout->SetWordWrapping(DWRITE_WORD_WRAPPING_WRAP);
        DWRITE_TEXT_METRICS metrics{};
        layout->GetMetrics(&metrics);
        return static_cast<int>(std::ceil(metrics.height * scale));
    }

    void TextStyled(const std::wstring& text, const RECT& rect, TextRole role, COLORREF color,
                    UINT flags) {
        if (text.empty()) return;
        auto& shared = SharedFactories();
        auto* format = RoleFormat(role);
        if (!format) return;
        auto bounds = Rect(rect);
        const float w = std::max(1.f, bounds.right - bounds.left);
        const float h = std::max(1.f, bounds.bottom - bounds.top);
        const auto layoutKey = std::to_wstring(static_cast<int>(role)) + L":" +
            std::to_wstring(static_cast<int>(std::round(w * 2.f))) + L":" +
            std::to_wstring(static_cast<int>(std::round(h * 2.f))) + L":" +
            std::to_wstring(flags) + L":" + text;
        if (layouts_.size() >= 384) layouts_.clear();
        auto& layout = layouts_[layoutKey];
        if (!layout) {
            if (FAILED(shared.dwrite->CreateTextLayout(
                    text.c_str(), static_cast<UINT32>(text.size()), format, w, h, &layout))) {
                return;
            }
            layout->SetTextAlignment(flags & DT_CENTER ? DWRITE_TEXT_ALIGNMENT_CENTER :
                flags & DT_RIGHT ? DWRITE_TEXT_ALIGNMENT_TRAILING : DWRITE_TEXT_ALIGNMENT_LEADING);
            layout->SetParagraphAlignment(flags & DT_VCENTER ? DWRITE_PARAGRAPH_ALIGNMENT_CENTER :
                                          DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
            layout->SetWordWrapping(flags & DT_SINGLELINE ? DWRITE_WORD_WRAPPING_NO_WRAP :
                                    DWRITE_WORD_WRAPPING_WRAP);
            if (flags & DT_END_ELLIPSIS) {
                ComPtr<IDWriteInlineObject> ellipsis;
                shared.dwrite->CreateEllipsisTrimmingSign(format, &ellipsis);
                DWRITE_TRIMMING trim{DWRITE_TRIMMING_GRANULARITY_CHARACTER, 0, 0};
                layout->SetTrimming(&trim, ellipsis.Get());
            }
        }
        target_->DrawTextLayout(D2D1::Point2F(bounds.left, bounds.top), layout.Get(),
                                Brush(color), D2D1_DRAW_TEXT_OPTIONS_CLIP);
    }

    // Legacy HFONT bridge kept only for test callers that inspect LOGFONT-driven metrics.
    void Text(const std::wstring& text, const RECT& rect, HFONT font, COLORREF color, UINT flags) {
        if (text.empty()) return;
        auto& shared = SharedFactories();
        LOGFONTW logical{};
        if (!GetObjectW(font, sizeof(logical), &logical)) return;
        const float fontSize = Dip(static_cast<float>(std::abs(logical.lfHeight)));
        const auto key = std::wstring(logical.lfFaceName) + L":" + std::to_wstring(fontSize) +
            L":" + std::to_wstring(logical.lfWeight);
        auto& format = formats_[key];
        if (!format && FAILED(shared.dwrite->CreateTextFormat(
                logical.lfFaceName, nullptr, static_cast<DWRITE_FONT_WEIGHT>(logical.lfWeight),
                DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, fontSize, L"", &format))) {
            return;
        }
        auto bounds = Rect(rect);
        ComPtr<IDWriteTextLayout> layout;
        if (FAILED(shared.dwrite->CreateTextLayout(
                text.c_str(), static_cast<UINT32>(text.size()), format.Get(),
                std::max(1.f, bounds.right - bounds.left),
                std::max(1.f, bounds.bottom - bounds.top), &layout))) {
            return;
        }
        layout->SetTextAlignment(flags & DT_CENTER ? DWRITE_TEXT_ALIGNMENT_CENTER :
            flags & DT_RIGHT ? DWRITE_TEXT_ALIGNMENT_TRAILING : DWRITE_TEXT_ALIGNMENT_LEADING);
        layout->SetParagraphAlignment(flags & DT_VCENTER ? DWRITE_PARAGRAPH_ALIGNMENT_CENTER :
                                      DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
        layout->SetWordWrapping(flags & DT_SINGLELINE ? DWRITE_WORD_WRAPPING_NO_WRAP :
                                DWRITE_WORD_WRAPPING_WRAP);
        if (flags & DT_END_ELLIPSIS) {
            ComPtr<IDWriteInlineObject> ellipsis;
            shared.dwrite->CreateEllipsisTrimmingSign(format.Get(), &ellipsis);
            DWRITE_TRIMMING trim{DWRITE_TRIMMING_GRANULARITY_CHARACTER, 0, 0};
            layout->SetTrimming(&trim, ellipsis.Get());
        }
        target_->DrawTextLayout(D2D1::Point2F(bounds.left, bounds.top), layout.Get(),
                                Brush(color), D2D1_DRAW_TEXT_OPTIONS_CLIP);
    }

    void AppIcon(HICON icon, const RECT& rect) {
        if (!icon) return;
        auto& shared = SharedFactories();
        // Application icons are content, not UI skins. Decode native icons once.
        auto& image = images_[icon];
        if (!image) {
            if (!shared.EnsureWic()) return;
            ComPtr<IWICBitmap> bitmap;
            ComPtr<IWICFormatConverter> converted;
            if (FAILED(shared.wic->CreateBitmapFromHICON(icon, &bitmap)) ||
                FAILED(shared.wic->CreateFormatConverter(&converted)) ||
                FAILED(converted->Initialize(bitmap.Get(), GUID_WICPixelFormat32bppPBGRA,
                                             WICBitmapDitherTypeNone, nullptr, 0,
                                             WICBitmapPaletteTypeCustom)) ||
                FAILED(target_->CreateBitmapFromWicBitmap(converted.Get(), nullptr, &image))) {
                return;
            }
        }
        target_->DrawBitmap(image.Get(), Rect(rect), 1.f, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
    }

    void ForgetImages() { images_.clear(); }

    void Icon(VectorIcon icon, const RECT& box, COLORREF color, float sizeDip = 16.f,
              float offsetDip = 0.f) {
        auto& shared = SharedFactories();
        auto rect = Rect(box);
        const float left = (rect.left + rect.right - sizeDip) / 2.f;
        const float top = (rect.top + rect.bottom - sizeDip) / 2.f + offsetDip;
        auto transform = D2D1::Matrix3x2F::Scale(sizeDip / 24.f, sizeDip / 24.f) *
                         D2D1::Matrix3x2F::Translation(left, top);
        D2D1_MATRIX_3X2_F old{};
        target_->GetTransform(&old);
        target_->SetTransform(transform * old);
        auto* brush = Brush(color);
        auto* round = shared.roundStroke.Get();

        if (icon == VectorIcon::Sun) {
            target_->DrawEllipse(D2D1::Ellipse({12, 12}, 3.6f, 3.6f), brush, 1.85f, round);
            for (int i = 0; i < 8; ++i) {
                float a = 3.14159265359f * i / 4.f;
                target_->DrawLine({12 + 7.4f * std::cos(a), 12 + 7.4f * std::sin(a)},
                                  {12 + 9.4f * std::cos(a), 12 + 9.4f * std::sin(a)},
                                  brush, 1.85f, round);
            }
        } else if (icon == VectorIcon::Minimize) {
            // Crisp Windows 11 / Antigravity optical proportions
            target_->DrawLine({4, 12}, {20, 12}, brush, 2.0f, round);
        } else if (icon == VectorIcon::Close) {
            target_->DrawLine({5, 5}, {19, 19}, brush, 2.0f, round);
            target_->DrawLine({19, 5}, {5, 19}, brush, 2.0f, round);
        } else if (icon == VectorIcon::Maximize) {
            target_->DrawRoundedRectangle(
                D2D1::RoundedRect(D2D1::RectF(4.5f, 4.5f, 19.5f, 19.5f), 2.4f, 2.4f),
                brush, 2.0f, round);
        } else if (icon == VectorIcon::Restore) {
            // Back window frame (top-right offset) + front rounded frame
            target_->DrawLine({8.5f, 4.0f}, {18.5f, 4.0f}, brush, 2.0f, round);
            target_->DrawLine({20.0f, 5.5f}, {20.0f, 15.5f}, brush, 2.0f, round);
            target_->DrawRoundedRectangle(
                D2D1::RoundedRect(D2D1::RectF(4.0f, 7.5f, 16.5f, 20.0f), 2.2f, 2.2f),
                brush, 2.0f, round);
        } else if (icon == VectorIcon::Keyboard) {
            target_->DrawRoundedRectangle(
                D2D1::RoundedRect(D2D1::RectF(2.5f, 5.5f, 21.5f, 18.5f), 2.6f, 2.6f),
                brush, 1.75f, round);
            for (float x : {6.5f, 10.2f, 13.8f, 17.5f}) {
                target_->FillEllipse(D2D1::Ellipse({x, 10.0f}, 0.9f, 0.9f), brush);
            }
            target_->DrawLine({7.5f, 14.5f}, {16.5f, 14.5f}, brush, 1.75f, round);
        } else if (icon == VectorIcon::WindowFallback) {
            target_->DrawRoundedRectangle(
                D2D1::RoundedRect(D2D1::RectF(3.0f, 4.0f, 21.0f, 20.0f), 2.8f, 2.8f),
                brush, 1.8f, round);
            target_->DrawLine({3.0f, 9.0f}, {21.0f, 9.0f}, brush, 1.6f, round);
        } else if (icon == VectorIcon::ExternalArrow) {
            target_->DrawLine({7.0f, 17.0f}, {17.0f, 7.0f}, brush, 2.0f, round);
            target_->DrawLine({9.5f, 7.0f}, {17.0f, 7.0f}, brush, 2.0f, round);
            target_->DrawLine({17.0f, 7.0f}, {17.0f, 14.5f}, brush, 2.0f, round);
        } else {
            auto found = shared.paths.find(icon);
            if (found == shared.paths.end()) {
                const char* data = icon == VectorIcon::Settings ? SettingsPath() :
                                   icon == VectorIcon::Moon ? MoonPath() : GitHubPath();
                ComPtr<ID2D1PathGeometry> geometry;
                if (BuildPath(shared.d2d.Get(), data, &geometry)) {
                    found = shared.paths.emplace(icon, geometry).first;
                }
            }
            if (found != shared.paths.end()) {
                if (icon == VectorIcon::GitHub) {
                    target_->SetTransform(D2D1::Matrix3x2F::Translation(2, 2) *
                        D2D1::Matrix3x2F::Scale(24.f / 28.f, 24.f / 28.f) * transform * old);
                    target_->FillGeometry(found->second.Get(), brush);
                } else {
                    if (icon == VectorIcon::Moon) {
                        target_->SetTransform(
                            D2D1::Matrix3x2F::Translation(1.3f, -1.3f) * transform * old);
                    }
                    target_->DrawGeometry(found->second.Get(), brush,
                        icon == VectorIcon::Settings ? 1.75f : 1.85f, round);
                    if (icon == VectorIcon::Settings) {
                        target_->DrawEllipse(D2D1::Ellipse({12, 12}, 3.15f, 3.15f),
                                             brush, 1.75f, round);
                    }
                }
            }
        }
        target_->SetTransform(old);
    }

private:
    static bool BuildPath(ID2D1Factory* factory, const char* data, ID2D1PathGeometry** result) {
        ComPtr<ID2D1PathGeometry> path;
        if (FAILED(factory->CreatePathGeometry(&path))) return false;
        ComPtr<ID2D1GeometrySink> sink;
        if (FAILED(path->Open(&sink))) return false;
        sink->SetFillMode(D2D1_FILL_MODE_WINDING);
        D2D1_POINT_2F point{}, start{};
        bool open = false;
        bool validPath = true;
        char command = 0;
        auto skip = [&] { while (*data == ' ' || *data == ',' || *data == '\n') ++data; };
        auto number = [&] {
            skip();
            char* end = nullptr;
            float v = std::strtof(data, &end);
            if (end == data) { validPath = false; return 0.f; }
            data = end;
            return v;
        };
        while (*data && validPath) {
            skip();
            if (!*data) break;
            if ((*data >= 'A' && *data <= 'Z') || (*data >= 'a' && *data <= 'z')) command = *data++;
            const bool relative = command >= 'a' && command <= 'z';
            char op = relative ? command - 'a' + 'A' : command;
            D2D1_POINT_2F base = relative ? point : D2D1::Point2F();
            auto xy = [&] { float x = number(), y = number(); return D2D1::Point2F(base.x + x, base.y + y); };
            if (op == 'Z') {
                if (open) sink->EndFigure(D2D1_FIGURE_END_CLOSED);
                open = false; point = start; command = 0;
            } else if (op == 'M') {
                auto p = xy();
                if (open) sink->EndFigure(D2D1_FIGURE_END_OPEN);
                sink->BeginFigure(p, D2D1_FIGURE_BEGIN_FILLED);
                open = true; point = start = p; command = relative ? 'l' : 'L';
            } else if (op == 'L') { point = xy(); sink->AddLine(point); }
            else if (op == 'H') { point.x = base.x + number(); sink->AddLine(point); }
            else if (op == 'V') { point.y = base.y + number(); sink->AddLine(point); }
            else if (op == 'C') { auto a = xy(), b = xy(), c = xy(); sink->AddBezier({a, b, c}); point = c; }
            else if (op == 'Q') { auto a = xy(), b = xy(); sink->AddQuadraticBezier({a, b}); point = b; }
            else if (op == 'A') {
                float rx = number(), ry = number(), angle = number(), large = number(), sweep = number();
                auto end = xy();
                sink->AddArc({end, {rx, ry}, angle,
                    sweep ? D2D1_SWEEP_DIRECTION_CLOCKWISE : D2D1_SWEEP_DIRECTION_COUNTER_CLOCKWISE,
                    large ? D2D1_ARC_SIZE_LARGE : D2D1_ARC_SIZE_SMALL});
                point = end;
            } else {
                validPath = false;
            }
        }
        if (open) sink->EndFigure(D2D1_FIGURE_END_OPEN);
        if (FAILED(sink->Close()) || !validPath) return false;
        *result = path.Detach();
        return true;
    }

    static const char* SettingsPath() {
        return "M9.3 4.85 C9.88 4.66 10.28 4.42 10.4 3.97 L10.68 2.92 Q10.8 2.5 11.22 2.5 L12.78 2.5 Q13.2 2.5 13.32 2.92 L13.6 3.97 C13.72 4.42 14.12 4.66 14.7 4.85 A7.643 7.643 0 0 1 16.842 6.087 C17.297 6.494 17.704 6.72 18.154 6.599 L19.204 6.317 Q19.627 6.211 19.837 6.575 L20.617 7.925 Q20.827 8.289 20.524 8.603 L19.754 9.371 C19.424 9.7 19.417 10.166 19.542 10.763 A7.643 7.643 0 0 1 19.542 13.237 C19.417 13.834 19.424 14.3 19.754 14.629 L20.524 15.397 Q20.827 15.711 20.617 16.075 L19.837 17.425 Q19.627 17.789 19.204 17.683 L18.154 17.401 C17.704 17.28 17.297 17.506 16.842 17.913 A7.643 7.643 0 0 1 14.7 19.15 C14.12 19.34 13.72 19.58 13.6 20.03 L13.32 21.08 Q13.2 21.5 12.78 21.5 L11.22 21.5 Q10.8 21.5 10.68 21.08 L10.4 20.03 C10.28 19.58 9.88 19.34 9.3 19.15 A7.643 7.643 0 0 1 7.158 17.913 C6.703 17.506 6.296 17.28 5.846 17.401 L4.796 17.683 Q4.373 17.789 4.163 17.425 L3.383 16.075 Q3.173 15.711 3.383 15.397 L4.246 14.629 C4.576 14.3 4.583 13.834 4.458 13.237 A7.643 7.643 0 0 1 4.458 10.763 C4.583 10.166 4.576 9.7 4.246 9.371 L3.476 8.603 Q3.173 8.289 3.383 7.925 L4.163 6.575 Q4.373 6.211 4.796 6.317 L5.846 6.599 C6.296 6.72 6.703 6.494 7.158 6.087 A7.643 7.643 0 0 1 9.3 4.85 Z";
    }
    static const char* MoonPath() { return "M19.2 15.4A8.3 8.3 0 0 1 8.6 4.8 8.4 8.4 0 1 0 19.2 15.4Z"; }
    static const char* GitHubPath() {
        return "M12 .8a11.2 11.2 0 0 0-3.54 21.83c.56.1.77-.24.77-.54v-2.08c-3.13.68-3.79-1.33-3.79-1.33-.51-1.3-1.25-1.65-1.25-1.65-1.02-.7.08-.69.08-.69 1.13.08 1.72 1.15 1.72 1.15 1 1.72 2.62 1.22 3.26.94.1-.73.39-1.22.71-1.5-2.5-.29-5.13-1.25-5.13-5.57 0-1.23.44-2.23 1.16-3.02-.11-.28-.5-1.43.11-2.97 0 0 .95-.3 3.08 1.15a10.7 10.7 0 0 1 5.61 0c2.13-1.45 3.08-1.15 3.08-1.15.61 1.54.22 2.69.11 2.97.72.79 1.16 1.79 1.16 3.02 0 4.33-2.64 5.28-5.15 5.56.4.35.76 1.03.76 2.08v3.09c0 .3.21.65.77.54A11.2 11.2 0 0 0 12 .8Z";
    }

    HWND hwnd_{};
    UINT dpi_{96};
    float factor_{1.f};
    bool software_{};
    bool drawing_{};
    bool freshFrame_{};
    unsigned generation_{};
    HRESULT lastError_{S_OK};
    ComPtr<ID2D1HwndRenderTarget> hwndTarget_;
    ComPtr<ID2D1DCRenderTarget> dcTarget_;
    ID2D1RenderTarget* target_{};
    ComPtr<ID2D1SolidColorBrush> brush_;
    ComPtr<ID2D1Layer> regionLayer_;
    ComPtr<ID2D1PathGeometry> regionGeometry_;
    std::map<std::wstring, ComPtr<IDWriteTextFormat>> formats_;
    std::map<TextRole, ComPtr<IDWriteTextFormat>> roleFormats_;
    std::map<std::wstring, ComPtr<IDWriteTextLayout>> layouts_;
    std::map<HICON, ComPtr<ID2D1Bitmap>> images_;
};

inline std::vector<RECT> WindowPaintRegion(HWND hwnd) {
    // Capture the actual update region before BeginPaint validates it. D2D does
    // not inherit an HDC's complex clip, so the renderer applies this explicitly.
    std::vector<RECT> rectangles;
    HRGN region = CreateRectRgn(0, 0, 0, 0);
    if (!region) return rectangles;
    if (GetUpdateRgn(hwnd, region, FALSE) != ERROR) {
        DWORD size = GetRegionData(region, 0, nullptr);
        std::vector<BYTE> data(size);
        if (size && GetRegionData(region, size, reinterpret_cast<RGNDATA*>(data.data()))) {
            auto* header = reinterpret_cast<RGNDATA*>(data.data());
            auto* rects = reinterpret_cast<RECT*>(header->Buffer);
            rectangles.assign(rects, rects + header->rdh.nCount);
        }
    }
    DeleteObject(region);
    return rectangles;
}

} // namespace windowcue
