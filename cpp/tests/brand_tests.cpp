#include "brands.hpp"
#include "core.hpp"
#include <d2d1_3.h>
#include <d3d11.h>
#include <dxgi.h>
#include <shlwapi.h>
#include <wincodec.h>
#include <wrl/client.h>
using namespace ad;
using Microsoft::WRL::ComPtr;
static void checked(HRESULT result, const std::string &label) {
    if (FAILED(result))
        throw std::runtime_error(label);
}
int brand_tests() {
    ComPtr<ID3D11Device> d3;
    D3D_FEATURE_LEVEL level;
    ComPtr<ID3D11DeviceContext> immediate;
    checked(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT,
                              nullptr, 0, D3D11_SDK_VERSION, &d3, &level, &immediate),
            "brand test D3D");
    ComPtr<IDXGIDevice> dx;
    checked(d3.As(&dx), "brand test DXGI");
    ComPtr<ID2D1Factory1> factory;
    checked(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, IID_PPV_ARGS(&factory)),
            "brand test factory");
    ComPtr<ID2D1Device> device;
    checked(factory->CreateDevice(dx.Get(), &device), "brand test device");
    ComPtr<ID2D1DeviceContext> base;
    checked(device->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, &base), "brand test context");
    ComPtr<ID2D1DeviceContext5> dc;
    checked(base.As(&dc), "native SVG support");
    auto props = D2D1::BitmapProperties1(
        D2D1_BITMAP_OPTIONS_TARGET,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED), 96, 96);
    ComPtr<ID2D1Bitmap1> target;
    checked(dc->CreateBitmap({48, 48}, nullptr, 0, &props, &target), "brand render target");
    dc->SetTarget(target.Get());
    props.bitmapOptions = D2D1_BITMAP_OPTIONS_CPU_READ | D2D1_BITMAP_OPTIONS_CANNOT_DRAW;
    ComPtr<ID2D1Bitmap1> pixels;
    checked(dc->CreateBitmap({48, 48}, nullptr, 0, &props, &pixels), "brand readback");
    auto sources = J::parse(read(path(BRAND_SOURCE_FILE)));
    int count = 0;
    for (auto &[name, id] : brand_resources) {
        auto raw = resource(id);
        if (sha(raw) != sources.at(name).at("sha256").get<std::string>())
            throw std::runtime_error("Brand resource provenance hash mismatch: " + name);
        ComPtr<IStream> stream;
        stream.Attach(SHCreateMemStream((const BYTE *)raw.data(), (UINT)raw.size()));
        if (name == "crush" || name == "glm" || name == "doubao" || name == "baidu" || name == "zcode") {
            ComPtr<IWICImagingFactory> wic;
            checked(
                CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wic)),
                "PNG factory");
            ComPtr<IWICBitmapDecoder> decoder;
            checked(
                wic->CreateDecoderFromStream(stream.Get(), nullptr, WICDecodeMetadataCacheOnLoad, &decoder),
                "official Crush PNG");
            if (name == "baidu") {
                ComPtr<IWICBitmapFrameDecode> frame;
                checked(decoder->GetFrame(0, &frame), "Wenxin frame");
                UINT width = 0, height = 0;
                checked(frame->GetSize(&width, &height), "Wenxin size");
                if (width != 186 || height != 63)
                    throw std::runtime_error("Wenxin is not the verified official waveform asset");
            }
            ++count;
            continue;
        }
        ComPtr<ID2D1SvgDocument> svg;
        checked(dc->CreateSvgDocument(stream.Get(), {48, 48}, &svg), "SVG parse: " + name);
        ComPtr<ID2D1SvgElement> root;
        svg->GetRoot(&root);
        D2D1_SVG_LENGTH len{48, D2D1_SVG_LENGTH_UNITS_NUMBER};
        root->SetAttributeValue(L"width", len);
        root->SetAttributeValue(L"height", len);
        dc->BeginDraw();
        dc->Clear(D2D1::ColorF(D2D1::ColorF::White));
        dc->DrawSvgDocument(svg.Get());
        checked(dc->EndDraw(), "SVG render: " + name);
        checked(pixels->CopyFromBitmap(nullptr, target.Get(), nullptr), "SVG readback: " + name);
        D2D1_MAPPED_RECT mapped;
        checked(pixels->Map(D2D1_MAP_OPTIONS_READ, &mapped), "SVG map: " + name);
        bool visible = false, chromatic = false;
        for (int y = 0; y < 48; ++y)
            for (int x = 0; x < 48; ++x) {
                auto p = mapped.bits + y * mapped.pitch + x * 4;
                if (p[0] < 245 || p[1] < 245 || p[2] < 245)
                    visible = true;
                if (abs(p[0] - p[1]) > 15 || abs(p[1] - p[2]) > 15)
                    chromatic = true;
            }
        pixels->Unmap();
        if (!visible)
            throw std::runtime_error("Blank SVG: " + name);
        bool expectedColor = false;
        for (auto &c : sources[name].value("colors", J::array())) {
            auto value = c.get<std::string>();
            if (value.size() == 7) {
                int rgb = std::stoi(value.substr(1), nullptr, 16);
                if (((rgb >> 16) & 255) != ((rgb >> 8) & 255) || ((rgb >> 8) & 255) != (rgb & 255))
                    expectedColor = true;
            }
        }
        if (expectedColor && !chromatic)
            throw std::runtime_error("Color SVG rendered gray: " + name);
        ++count;
    }
    return count;
}
