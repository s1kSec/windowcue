#include "WindowCueNative.h"
#include "../resource.h"

#include <wincodec.h>
#include <wrl/client.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

// This diagnostic exercises the shipping Direct2D SVG renderer without creating
// application windows or reading/writing the user's settings. A resource-only
// module can preserve a released build for a before/after pixel comparison.
namespace {
using Microsoft::WRL::ComPtr;

struct Pixels {
    int width{};
    int height{};
    std::vector<BYTE> bgra; // Premultiplied, top-down, matching RenderSvgResource.
};

Pixels ReadBitmap(HBITMAP bitmap) {
    DIBSECTION section{};
    if (!GetObjectW(bitmap, sizeof(section), &section) || !section.dsBm.bmBits) return {};
    Pixels pixels{section.dsBm.bmWidth, std::abs(section.dsBm.bmHeight), {}};
    pixels.bgra.resize(static_cast<size_t>(pixels.width) * pixels.height * 4);
    const BYTE* source = static_cast<const BYTE*>(section.dsBm.bmBits);
    // Both production RenderSvgResource and CreateCanvas create negative-height
    // DIB sections. GetObject can normalize the returned BITMAPINFOHEADER height
    // to positive; the underlying memory still starts at the top scanline.
    for (int row = 0; row < pixels.height; ++row) {
        std::memcpy(pixels.bgra.data() + static_cast<size_t>(row) * pixels.width * 4,
                    source + static_cast<size_t>(row) * section.dsBm.bmWidthBytes,
                    static_cast<size_t>(pixels.width) * 4);
    }
    return pixels;
}

bool WritePng(IWICImagingFactory* factory, const std::filesystem::path& path,
              const Pixels& source) {
    if (source.width <= 0 || source.height <= 0) return false;
    ComPtr<IWICStream> stream;
    ComPtr<IWICBitmapEncoder> encoder;
    ComPtr<IWICBitmapFrameEncode> frame;
    if (FAILED(factory->CreateStream(&stream)) ||
        FAILED(stream->InitializeFromFilename(path.c_str(), GENERIC_WRITE)) ||
        FAILED(factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder)) ||
        FAILED(encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache)) ||
        FAILED(encoder->CreateNewFrame(&frame, nullptr)) ||
        FAILED(frame->Initialize(nullptr)) ||
        FAILED(frame->SetSize(source.width, source.height))) return false;
    WICPixelFormatGUID format = GUID_WICPixelFormat32bppBGRA;
    if (FAILED(frame->SetPixelFormat(&format)) || format != GUID_WICPixelFormat32bppBGRA)
        return false;
    // PNG uses straight alpha. Unpremultiply here only; the native bitmap and
    // measurements retain the production renderer's original pixel values.
    std::vector<BYTE> straight = source.bgra;
    for (size_t offset = 0; offset < straight.size(); offset += 4) {
        const unsigned alpha = straight[offset + 3];
        for (int component = 0; component < 3; ++component) {
            straight[offset + component] = alpha ? static_cast<BYTE>(std::min(255u,
                (static_cast<unsigned>(straight[offset + component]) * 255 + alpha / 2) / alpha)) : 0;
        }
    }
    return SUCCEEDED(frame->WritePixels(source.height, source.width * 4,
        static_cast<UINT>(straight.size()), straight.data())) &&
        SUCCEEDED(frame->Commit()) && SUCCEEDED(encoder->Commit());
}

struct Measurements {
    int left{}, top{}, right{-1}, bottom{-1};
    double alphaMass{}, centroidX{}, centroidY{}, mirrorX{}, mirrorY{};
    unsigned edgePixels{};
    unsigned centerAlpha{};
};

Measurements Measure(const Pixels& pixels) {
    Measurements result;
    result.left = pixels.width;
    result.top = pixels.height;
    if (!pixels.bgra.empty()) result.centerAlpha = pixels.bgra[
        (static_cast<size_t>(pixels.height / 2) * pixels.width + pixels.width / 2) * 4 + 3];
    double weightedX = 0, weightedY = 0, mirrorX = 0, mirrorY = 0;
    for (int y = 0; y < pixels.height; ++y) {
        for (int x = 0; x < pixels.width; ++x) {
            auto alpha = [&](int atX, int atY) -> int {
                return pixels.bgra[(static_cast<size_t>(atY) * pixels.width + atX) * 4 + 3];
            };
            const int value = alpha(x, y);
            result.alphaMass += value / 255.0;
            weightedX += (x + 0.5) * value / 255.0;
            weightedY += (y + 0.5) * value / 255.0;
            mirrorX += std::abs(value - alpha(pixels.width - 1 - x, y)) / 255.0;
            mirrorY += std::abs(value - alpha(x, pixels.height - 1 - y)) / 255.0;
            if (value >= 8) {
                result.left = std::min(result.left, x);
                result.top = std::min(result.top, y);
                result.right = std::max(result.right, x);
                result.bottom = std::max(result.bottom, y);
                if (!x || !y || x == pixels.width - 1 || y == pixels.height - 1)
                    ++result.edgePixels;
            }
        }
    }
    if (result.alphaMass > 0) {
        result.centroidX = weightedX / result.alphaMass - pixels.width / 2.0;
        result.centroidY = weightedY / result.alphaMass - pixels.height / 2.0;
        result.mirrorX = mirrorX / (2 * result.alphaMass);
        result.mirrorY = mirrorY / (2 * result.alphaMass);
    }
    return result;
}

HBITMAP CreateCanvas(int width, int height, COLORREF background) {
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = width;
    info.bmiHeader.biHeight = -height;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    HBITMAP bitmap = CreateDIBSection(nullptr, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (bitmap && bits) {
        auto* pixels = static_cast<BYTE*>(bits);
        for (size_t index = 0; index < static_cast<size_t>(width) * height; ++index) {
            pixels[index * 4] = GetBValue(background);
            pixels[index * 4 + 1] = GetGValue(background);
            pixels[index * 4 + 2] = GetRValue(background);
            pixels[index * 4 + 3] = 255;
        }
    }
    return bitmap;
}

void DrawBitmap(HDC dc, HBITMAP bitmap, int left, int top, int width, int height) {
    BITMAP information{};
    GetObjectW(bitmap, sizeof(information), &information);
    HDC source = CreateCompatibleDC(dc);
    HGDIOBJ previous = SelectObject(source, bitmap);
    BLENDFUNCTION blend{AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
    AlphaBlend(dc, left + (width - information.bmWidth) / 2,
        top + (height - information.bmHeight) / 2,
        information.bmWidth, information.bmHeight, source, 0, 0,
        information.bmWidth, information.bmHeight, blend);
    SelectObject(source, previous);
    DeleteDC(source);
}

// The title's 34 DIP height, 38 DIP button slots and 18 DIP SVG size mirror
// Main.cpp. Three strips isolate resting / hover / pressed visibility; the
// background is a representative opaque caption tint, not a DWM screenshot.
Pixels MakeTitleStrip(HINSTANCE resources, int dpi, bool dark, int state) {
    const auto scale = [dpi](int value) { return MulDiv(value, dpi, 96); };
    const int width = scale(38), height = scale(34);
    const COLORREF background = dark ? RGB(40, 42, 48) : RGB(245, 248, 252);
    HBITMAP canvas = CreateCanvas(width * 3, height, background);
    if (!canvas) return {};
    HDC dc = CreateCompatibleDC(nullptr);
    HGDIOBJ previous = SelectObject(dc, canvas);
    if (state) {
        const COLORREF fill = dark ? (state == 2 ? RGB(65, 65, 65) : RGB(52, 52, 52))
            : (state == 2 ? RGB(214, 215, 218) : RGB(226, 227, 229));
        HBRUSH brush = CreateSolidBrush(fill);
        HGDIOBJ oldBrush = SelectObject(dc, brush);
        HGDIOBJ oldPen = SelectObject(dc, GetStockObject(NULL_PEN));
        RoundRect(dc, scale(3), scale(3), width - scale(3), height - scale(3), scale(5), scale(5));
        SelectObject(dc, oldPen);
        SelectObject(dc, oldBrush);
        DeleteObject(brush);
    }
    const int ids[] = {dark ? IDR_SETTINGS_DARK_SVG : IDR_SETTINGS_LIGHT_SVG,
        dark ? IDR_SUN_SVG : IDR_MOON_SVG,
        dark ? IDR_GITHUB_DARK_SVG : IDR_GITHUB_LIGHT_SVG};
    for (int index = 0; index < 3; ++index) {
        HBITMAP bitmap = windowcue::RenderSvgResource(resources, ids[index], scale(18));
        if (bitmap) {
            DrawBitmap(dc, bitmap, index * width, 0, width, height);
            DeleteObject(bitmap);
        }
    }
    // GDI rounded-rectangle drawing zeroes alpha; this opaque caption sample
    // intentionally restores it after blending, preserving its RGB evidence.
    Pixels result = ReadBitmap(canvas);
    for (size_t offset = 3; offset < result.bgra.size(); offset += 4) result.bgra[offset] = 255;
    SelectObject(dc, previous);
    DeleteDC(dc);
    DeleteObject(canvas);
    return result;
}

} // namespace

int wmain(int argc, wchar_t** argv) {
    std::filesystem::path output;
    std::filesystem::path module;
    bool strict = false;
    for (int index = 1; index < argc; ++index) {
        const std::wstring argument = argv[index];
        if (argument == L"--out" && index + 1 < argc) output = argv[++index];
        else if (argument == L"--resources" && index + 1 < argc) module = argv[++index];
        else if (argument == L"--strict") strict = true;
        else { std::fwprintf(stderr, L"Unknown or incomplete argument: %ls\n", argv[index]); return 2; }
    }
    if (output.empty()) {
        std::fwprintf(stderr, L"Usage: IconVisual.exe --out directory [--resources released.exe] [--strict]\n");
        return 2;
    }
    std::filesystem::create_directories(output);
    HRESULT initialized = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(initialized)) return 3;
    ComPtr<IWICImagingFactory> factory;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                IID_PPV_ARGS(&factory)))) { CoUninitialize(); return 4; }
    HINSTANCE resources = GetModuleHandleW(nullptr);
    if (!module.empty()) resources = LoadLibraryExW(module.c_str(), nullptr,
        LOAD_LIBRARY_AS_DATAFILE | LOAD_LIBRARY_AS_IMAGE_RESOURCE);
    if (!resources) { std::fwprintf(stderr, L"Cannot load resource module.\n"); CoUninitialize(); return 5; }
    const struct { int id; const char* name; bool gear; } icons[] = {
        {IDR_SETTINGS_DARK_SVG, "settings-dark", true},
        {IDR_SETTINGS_LIGHT_SVG, "settings-light", true},
        {IDR_SUN_SVG, "sun", false}, {IDR_MOON_SVG, "moon", false},
        {IDR_GITHUB_DARK_SVG, "github-dark", false},
        {IDR_GITHUB_LIGHT_SVG, "github-light", false}
    };
    std::ofstream statistics(output / "pixel-measurements.csv", std::ios::binary);
    statistics << "resource,size,bbox_left,bbox_top,bbox_right,bbox_bottom,alpha_mass,centroid_dx,centroid_dy,mirror_x_error,mirror_y_error,edge_pixels,center_alpha\n";
    int errors = 0;
    for (const auto& icon : icons) {
        for (int size : {18, 23, 27, 36}) {
            HBITMAP bitmap = windowcue::RenderSvgResource(resources, icon.id, size);
            if (!bitmap) { ++errors; std::printf("FAIL %s %d render\n", icon.name, size); continue; }
            Pixels pixels = ReadBitmap(bitmap);
            DeleteObject(bitmap);
            const Measurements measured = Measure(pixels);
            const auto filename = std::string(icon.name) + "-" + std::to_string(size) + ".png";
            if (!WritePng(factory.Get(), output / filename, pixels)) ++errors;
            statistics << icon.name << ',' << size << ',' << measured.left << ',' << measured.top
                << ',' << measured.right << ',' << measured.bottom << ',' << measured.alphaMass
                << ',' << measured.centroidX << ',' << measured.centroidY << ',' << measured.mirrorX
                << ',' << measured.mirrorY << ',' << measured.edgePixels << ',' << measured.centerAlpha << '\n';
            if (strict && icon.gear && (measured.alphaMass < size || measured.edgePixels != 0 ||
                std::abs(measured.centroidX) > 0.35 || std::abs(measured.centroidY) > 0.35 ||
                measured.mirrorX > 0.10 || measured.mirrorY > 0.10 || measured.centerAlpha > 8)) {
                ++errors;
                std::printf("FAIL %s %d geometry: center=(%.3f,%.3f), mirror=(%.3f,%.3f), edges=%u, hub_alpha=%u\n",
                    icon.name, size, measured.centroidX, measured.centroidY,
                    measured.mirrorX, measured.mirrorY, measured.edgePixels, measured.centerAlpha);
            }
        }
    }
    for (int dpi : {96, 120, 144, 192}) {
        for (bool dark : {false, true}) {
            for (int state = 0; state < 3; ++state) {
                const char* names[] = {"rest", "hover", "pressed"};
                const std::string filename = std::string("title-") + (dark ? "dark-" : "light-") +
                    std::to_string(dpi) + "-" + names[state] + ".png";
                if (!WritePng(factory.Get(), output / filename, MakeTitleStrip(resources, dpi, dark, state)))
                    ++errors;
            }
        }
    }
    statistics.close();
    if (!module.empty()) FreeLibrary(resources);
    factory.Reset();
    CoUninitialize();
    std::printf("ICON_VISUAL_ERRORS=%d\n", errors);
    return errors ? 1 : 0;
}
