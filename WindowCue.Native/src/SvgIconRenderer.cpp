#include "WindowCueNative.h"

#include <d2d1_3.h>
#include <d2d1svg.h>
#include <d3d11_1.h>
#include <dxgi1_2.h>
#include <shlwapi.h>
#include <wrl/client.h>

#include <cstring>
#include <iterator>
#include <cmath>

namespace windowcue {
namespace {

using Microsoft::WRL::ComPtr;

class SvgRenderer {
public:
    bool EnsureDevice() {
        if (context_) return true;

        UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
        D3D_FEATURE_LEVEL levels[] = {
            D3D_FEATURE_LEVEL_11_1,
            D3D_FEATURE_LEVEL_11_0,
            D3D_FEATURE_LEVEL_10_1,
            D3D_FEATURE_LEVEL_10_0
        };
        D3D_FEATURE_LEVEL createdLevel{};
        HRESULT result = D3D11CreateDevice(
            nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags,
            levels, static_cast<UINT>(std::size(levels)), D3D11_SDK_VERSION,
            &d3dDevice_, &createdLevel, nullptr);
        if (FAILED(result)) {
            result = D3D11CreateDevice(
                nullptr, D3D_DRIVER_TYPE_WARP, nullptr, flags,
                levels, static_cast<UINT>(std::size(levels)), D3D11_SDK_VERSION,
                &d3dDevice_, &createdLevel, nullptr);
        }
        if (FAILED(result)) return false;

        ComPtr<IDXGIDevice> dxgiDevice;
        if (FAILED(d3dDevice_.As(&dxgiDevice))) return false;

        ComPtr<ID2D1Factory1> factory;
        D2D1_FACTORY_OPTIONS options{};
        if (FAILED(D2D1CreateFactory(
                D2D1_FACTORY_TYPE_SINGLE_THREADED, __uuidof(ID2D1Factory1),
                &options, reinterpret_cast<void**>(factory.GetAddressOf())))) {
            return false;
        }

        ComPtr<ID2D1Device> device;
        if (FAILED(factory->CreateDevice(dxgiDevice.Get(), &device))) return false;

        ComPtr<ID2D1DeviceContext> baseContext;
        if (FAILED(device->CreateDeviceContext(
                D2D1_DEVICE_CONTEXT_OPTIONS_NONE, &baseContext))) {
            return false;
        }
        return SUCCEEDED(baseContext.As(&context_));
    }

    HBITMAP Render(const void* data, DWORD size, int pixelSize) {
        if (!data || !size || pixelSize <= 0 || !EnsureDevice()) return nullptr;

        ComPtr<IStream> stream;
        stream.Attach(SHCreateMemStream(
            static_cast<const BYTE*>(data), static_cast<UINT>(size)));
        if (!stream) return nullptr;

        ComPtr<ID2D1SvgDocument> document;
        if (FAILED(context_->CreateSvgDocument(
                stream.Get(), D2D1::SizeF(static_cast<float>(pixelSize),
                                         static_cast<float>(pixelSize)),
                &document))) {
            return nullptr;
        }

        // The initial viewport does not override a root's fixed width/height.
        // Fit the viewBox to the requested size: no small-size cropping or
        // undersized glyphs at high DPI.
        ComPtr<ID2D1SvgElement> root;
        document->GetRoot(&root);
        const D2D1_SVG_LENGTH outputLength{
            static_cast<float>(pixelSize), D2D1_SVG_LENGTH_UNITS_NUMBER};
        if (!root || FAILED(root->SetAttributeValue(L"width", outputLength)) ||
            FAILED(root->SetAttributeValue(L"height", outputLength))) {
            return nullptr;
        }

        D2D1_BITMAP_PROPERTIES1 targetProperties = D2D1::BitmapProperties1(
            D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW,
            D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,
                              D2D1_ALPHA_MODE_PREMULTIPLIED),
            96.0f, 96.0f);
        ComPtr<ID2D1Bitmap1> target;
        if (FAILED(context_->CreateBitmap(
                D2D1::SizeU(static_cast<UINT32>(pixelSize),
                            static_cast<UINT32>(pixelSize)),
                nullptr, 0, &targetProperties, &target))) {
            return nullptr;
        }

        context_->SetTarget(target.Get());
        context_->BeginDraw();
        context_->Clear(D2D1::ColorF(0, 0.0f));
        context_->DrawSvgDocument(document.Get());
        HRESULT drawResult = context_->EndDraw();
        context_->SetTarget(nullptr);
        if (FAILED(drawResult)) return nullptr;

        D2D1_BITMAP_PROPERTIES1 readProperties = D2D1::BitmapProperties1(
            D2D1_BITMAP_OPTIONS_CPU_READ | D2D1_BITMAP_OPTIONS_CANNOT_DRAW,
            D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,
                              D2D1_ALPHA_MODE_PREMULTIPLIED),
            96.0f, 96.0f);
        ComPtr<ID2D1Bitmap1> readback;
        if (FAILED(context_->CreateBitmap(
                D2D1::SizeU(static_cast<UINT32>(pixelSize),
                            static_cast<UINT32>(pixelSize)),
                nullptr, 0, &readProperties, &readback)) ||
            FAILED(readback->CopyFromBitmap(nullptr, target.Get(), nullptr))) {
            return nullptr;
        }

        D2D1_MAPPED_RECT mapped{};
        if (FAILED(readback->Map(D2D1_MAP_OPTIONS_READ, &mapped))) return nullptr;

        BITMAPINFO info{};
        info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth = pixelSize;
        info.bmiHeader.biHeight = -pixelSize;
        info.bmiHeader.biPlanes = 1;
        info.bmiHeader.biBitCount = 32;
        info.bmiHeader.biCompression = BI_RGB;
        void* pixels = nullptr;
        HBITMAP result = CreateDIBSection(
            nullptr, &info, DIB_RGB_COLORS, &pixels, nullptr, 0);
        if (result && pixels) {
            const size_t rowBytes = static_cast<size_t>(pixelSize) * 4;
            auto* destination = static_cast<BYTE*>(pixels);
            for (int row = 0; row < pixelSize; ++row) {
                std::memcpy(destination + static_cast<size_t>(row) * rowBytes,
                            mapped.bits + static_cast<size_t>(row) * mapped.pitch,
                            rowBytes);
            }
        }
        readback->Unmap();
        return result;
    }

private:
    ComPtr<ID3D11Device> d3dDevice_;
    ComPtr<ID2D1DeviceContext5> context_;
};

SvgRenderer g_renderer;

} // namespace

HBITMAP RenderSvgResource(HINSTANCE instance, int resourceId, int pixelSize) {
    HRSRC resource = FindResourceW(
        instance, MAKEINTRESOURCEW(resourceId), RT_RCDATA);
    if (!resource) return nullptr;
    HGLOBAL loaded = LoadResource(instance, resource);
    const void* data = loaded ? LockResource(loaded) : nullptr;
    DWORD size = SizeofResource(instance, resource);
    return g_renderer.Render(data, size, pixelSize);
}

bool ValidateSvgResourceLayout(HINSTANCE instance, int resourceId, int pixelSize,
                               bool requireCentered) {
    HBITMAP bitmap = RenderSvgResource(instance, resourceId, pixelSize);
    if (!bitmap) return false;
    DIBSECTION section{};
    bool valid = GetObjectW(bitmap, sizeof(section), &section) == sizeof(section) &&
                 section.dsBm.bmBits && section.dsBm.bmBitsPixel == 32 &&
                 section.dsBm.bmWidth == pixelSize && section.dsBm.bmHeight == pixelSize;
    double mass = 0, weightedX = 0, weightedY = 0;
    if (valid) {
        const auto* pixels = static_cast<const BYTE*>(section.dsBm.bmBits);
        for (int y = 0; y < pixelSize; ++y) {
            for (int x = 0; x < pixelSize; ++x) {
                BYTE alpha = pixels[y * section.dsBm.bmWidthBytes + x * 4 + 3];
                if ((x == 0 || y == 0 || x == pixelSize - 1 || y == pixelSize - 1) && alpha > 8)
                    valid = false; // A visible edge pixel signals viewport clipping.
                mass += alpha;
                weightedX += x * alpha;
                weightedY += y * alpha;
            }
        }
        valid = valid && mass > pixelSize * pixelSize * 255.0 * 0.04;
        if (valid && requireCentered) {
            double center = (pixelSize - 1) * 0.5;
            valid = std::abs(weightedX / mass - center) < 0.45 &&
                    std::abs(weightedY / mass - center) < 0.45;
        }
    }
    DeleteObject(bitmap);
    return valid;
}

} // namespace windowcue
