#include "WindowCueNative.h"

#include <d2d1_3.h>
#include <d2d1svg.h>
#include <d3d11_1.h>
#include <dxgi1_2.h>
#include <shlwapi.h>
#include <wrl/client.h>

#include <cstring>
#include <iterator>

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

} // namespace windowcue
