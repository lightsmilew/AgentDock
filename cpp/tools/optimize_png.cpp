#define NOMINMAX
#include <algorithm>
#include <wincodec.h>
#include <windows.h>
#include <wrl/client.h>
using Microsoft::WRL::ComPtr;
static void check(HRESULT hr) {
    if (FAILED(hr))
        throw hr;
}
int wmain(int argc, wchar_t **argv) {
    if (argc != 3)
        return 2;
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    try {
        ComPtr<IWICImagingFactory> factory;
        check(
            CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory)));
        ComPtr<IWICBitmapDecoder> decoder;
        check(factory->CreateDecoderFromFilename(argv[1], nullptr, GENERIC_READ, WICDecodeMetadataCacheOnLoad,
                                                 &decoder));
        ComPtr<IWICBitmapFrameDecode> frame;
        check(decoder->GetFrame(0, &frame));
        UINT w, h;
        check(frame->GetSize(&w, &h));
        UINT tw = std::min(w, 160u), th = std::max(1u, (UINT)(h * (double)tw / w));
        ComPtr<IWICBitmapScaler> scaler;
        check(factory->CreateBitmapScaler(&scaler));
        check(scaler->Initialize(frame.Get(), tw, th, WICBitmapInterpolationModeFant));
        ComPtr<IWICFormatConverter> converter;
        check(factory->CreateFormatConverter(&converter));
        check(converter->Initialize(scaler.Get(), GUID_WICPixelFormat32bppBGRA, WICBitmapDitherTypeNone,
                                    nullptr, 0, WICBitmapPaletteTypeCustom));
        ComPtr<IWICStream> stream;
        check(factory->CreateStream(&stream));
        check(stream->InitializeFromFilename(argv[2], GENERIC_WRITE));
        ComPtr<IWICBitmapEncoder> encoder;
        check(factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder));
        check(encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache));
        ComPtr<IWICBitmapFrameEncode> output;
        ComPtr<IPropertyBag2> props;
        check(encoder->CreateNewFrame(&output, &props));
        check(output->Initialize(props.Get()));
        check(output->SetSize(tw, th));
        WICPixelFormatGUID pixel = GUID_WICPixelFormat32bppBGRA;
        check(output->SetPixelFormat(&pixel));
        check(output->WriteSource(converter.Get(), nullptr));
        check(output->Commit());
        check(encoder->Commit());
    } catch (...) {
        CoUninitialize();
        return 1;
    }
    CoUninitialize();
    return 0;
}
