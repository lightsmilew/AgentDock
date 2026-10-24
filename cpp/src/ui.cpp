#include "ui.hpp"
#include "brands.hpp"
#include <algorithm>
#include <cmath>
#include <commctrl.h>
#include <dwmapi.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <sstream>
#include <wincodec.h>
#include <windowsx.h>

namespace ad {
static void hr(HRESULT result) {
    if (FAILED(result))
        throw std::runtime_error("Windows 图形服务初始化失败，请检查显卡驱动与系统版本");
}
static bool contains(Rect r, float x, float y) {
    return x >= r.left && x < r.right && y >= r.top && y < r.bottom;
}
static Rect intersection(Rect a, Rect b) {
    return {std::max(a.left, b.left), std::max(a.top, b.top), std::min(a.right, b.right),
            std::min(a.bottom, b.bottom)};
}
Window::~Window() {
    shutdown();
    if (hwnd)
        DestroyWindow(hwnd);
    if (editFont)
        DeleteObject(editFont);
}
void Window::shutdown() {
    stop = true;
    if (worker.joinable()) {
        if (cancelJob)
            cancelJob();
        worker.join();
    }
    if (hwnd) {
        MSG message;
        while (PeekMessageW(&message, hwnd, WM_APP + 2, WM_APP + 2, PM_REMOVE))
            delete (JobResult *)message.lParam;
    }
}
void Window::open(const std::string &text, int w, int h, HWND parent, bool isModal) {
    title = text;
    owner = parent;
    modal = isModal;
    static bool registered = false;
    if (!registered) {
        WNDCLASSEXW wc{sizeof wc};
        wc.lpfnWndProc = proc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.hIcon = (HICON)LoadImageW(wc.hInstance, MAKEINTRESOURCEW(1), IMAGE_ICON, 0, 0, LR_DEFAULTSIZE);
        wc.hIconSm = wc.hIcon;
        wc.lpszClassName = L"AgentDock.Native.Window";
        if (!RegisterClassExW(&wc))
            throw std::runtime_error("无法注册窗口");
        registered = true;
    }
    auto monitor = MonitorFromWindow(parent ? parent : GetDesktopWindow(), MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi{sizeof mi};
    GetMonitorInfoW(monitor, &mi);
    dpi = parent ? (float)GetDpiForWindow(parent) : (float)GetDpiForSystem();
    int aw = mi.rcWork.right - mi.rcWork.left, ah = mi.rcWork.bottom - mi.rcWork.top;
    int pw = std::min((int)(w * dpi / 96), aw - 24), ph = std::min((int)(h * dpi / 96), ah - 24);
    minWidth = (LONG)(pw * 96 / dpi);
    minHeight = (LONG)(ph * 96 / dpi);
    int x = mi.rcWork.left + (aw - pw) / 2, y = mi.rcWork.top + (ah - ph) / 2;
    hwnd = CreateWindowExW(WS_EX_APPWINDOW, L"AgentDock.Native.Window", wide(title).c_str(),
                           WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, x, y, pw, ph, parent, nullptr,
                           GetModuleHandleW(nullptr), this);
    if (!hwnd)
        throw std::runtime_error("无法创建桌面窗口");
    int corner = 2;
    DwmSetWindowAttribute(hwnd, 33, &corner, sizeof corner);
    MARGINS margins{1, 1, 1, 1};
    DwmExtendFrameIntoClientArea(hwnd, &margins);
    SetWindowPos(hwnd, nullptr, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_FRAMECHANGED | SWP_NOZORDER);
    if (modal && parent)
        EnableWindow(parent, FALSE);
    ShowWindow(hwnd, SW_SHOW);
}
void Window::initGraphics() {
    if (dc)
        return;
    UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
    D3D_FEATURE_LEVEL level;
    ComPtr<ID3D11DeviceContext> d3ctx;
    HRESULT result = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags, nullptr, 0,
                                       D3D11_SDK_VERSION, &d3d, &level, &d3ctx);
    if (FAILED(result))
        hr(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, flags, nullptr, 0, D3D11_SDK_VERSION,
                             &d3d, &level, &d3ctx));
    ComPtr<IDXGIDevice> dx;
    hr(d3d.As(&dx));
    D2D1_FACTORY_OPTIONS opts{};
    hr(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, __uuidof(ID2D1Factory1), &opts,
                         (void **)factory.GetAddressOf()));
    hr(factory->CreateDevice(dx.Get(), &device));
    ComPtr<ID2D1DeviceContext> context;
    hr(device->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, &context));
    hr(context.As(&dc));
    hr(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                           (IUnknown **)dw.GetAddressOf()));
    ComPtr<IDXGIAdapter> adapter;
    hr(dx->GetAdapter(&adapter));
    ComPtr<IDXGIFactory2> dxFactory;
    hr(adapter->GetParent(IID_PPV_ARGS(&dxFactory)));
    DXGI_SWAP_CHAIN_DESC1 desc{};
    desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.BufferCount = 2;
    desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
    desc.AlphaMode = DXGI_ALPHA_MODE_IGNORE;
    hr(dxFactory->CreateSwapChainForHwnd(d3d.Get(), hwnd, &desc, nullptr, nullptr, &swap));
    dxFactory->MakeWindowAssociation(hwnd, DXGI_MWA_NO_ALT_ENTER);
    hr(dc->CreateSolidColorBrush(color(INK), &brush));
    resizeGraphics();
}
void Window::resizeGraphics() {
    if (!dc || !swap)
        return;
    RECT r;
    GetClientRect(hwnd, &r);
    if (r.right <= 0 || r.bottom <= 0)
        return;
    dc->SetTarget(nullptr);
    targetBitmap.Reset();
    hr(swap->ResizeBuffers(0, r.right, r.bottom, DXGI_FORMAT_UNKNOWN, 0));
    ComPtr<IDXGISurface> surface;
    hr(swap->GetBuffer(0, IID_PPV_ARGS(&surface)));
    auto props = D2D1::BitmapProperties1(
        D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE), dpi, dpi);
    hr(dc->CreateBitmapFromDxgiSurface(surface.Get(), &props, &targetBitmap));
    dc->SetTarget(targetBitmap.Get());
    dc->SetDpi(dpi, dpi);
    dc->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_CLEARTYPE);
}
void Window::rect(Rect r, unsigned c, float radius, unsigned border) {
    brush->SetColor(color(c));
    auto rr = D2D1::RoundedRect(r, radius, radius);
    dc->FillRoundedRectangle(rr, brush.Get());
    if (border) {
        brush->SetColor(color(border));
        dc->DrawRoundedRectangle(rr, brush.Get(), 1);
    }
}
void Window::line(float x1, float y1, float x2, float y2, unsigned c, float weight) {
    brush->SetColor(color(c));
    dc->DrawLine(D2D1::Point2F(x1, y1), D2D1::Point2F(x2, y2), brush.Get(), weight);
}
void Window::text(const std::string &str, Rect r, float size, unsigned c, bool bold, bool wrap, int align) {
    if (r.right <= r.left || r.bottom <= r.top || str.empty())
        return;
    auto key = std::to_string(size) + (bold ? "b" : "n") + (wrap ? "w" : "n") + std::to_string(align);
    auto &format = formats[key];
    if (!format) {
        hr(dw->CreateTextFormat(
            L"Microsoft YaHei UI", nullptr, bold ? DWRITE_FONT_WEIGHT_SEMI_BOLD : DWRITE_FONT_WEIGHT_NORMAL,
            DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, size, L"zh-CN", &format));
        format->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        format->SetTextAlignment(align == 1   ? DWRITE_TEXT_ALIGNMENT_CENTER
                                 : align == 2 ? DWRITE_TEXT_ALIGNMENT_TRAILING
                                              : DWRITE_TEXT_ALIGNMENT_LEADING);
        format->SetWordWrapping(wrap ? DWRITE_WORD_WRAPPING_WRAP : DWRITE_WORD_WRAPPING_NO_WRAP);
        DWRITE_TRIMMING trimming{DWRITE_TRIMMING_GRANULARITY_CHARACTER, 0, 0};
        ComPtr<IDWriteInlineObject> ellipsis;
        dw->CreateEllipsisTrimmingSign(format.Get(), &ellipsis);
        format->SetTrimming(&trimming, ellipsis.Get());
    }
    auto s = wide(str);
    brush->SetColor(color(c));
    dc->DrawTextW(s.data(), (UINT32)s.size(), format.Get(), r, brush.Get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);
}
void Window::icon(const std::string &name, float x, float y, float size, unsigned c) {
    float a = size / 24;
    auto l = [&](float x1, float y1, float x2, float y2) {
        line(x + x1 * a, y + y1 * a, x + x2 * a, y + y2 * a, c, 1.5f);
    };
    auto rr = [&](float x1, float y1, float w, float h, float r) {
        brush->SetColor(color(c));
        dc->DrawRoundedRectangle(D2D1::RoundedRect(box(x + x1 * a, y + y1 * a, w * a, h * a), r * a, r * a),
                                 brush.Get(), 1.5f);
    };
    auto circle = [&](float cx, float cy, float r) {
        brush->SetColor(color(c));
        dc->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(x + cx * a, y + cy * a), r * a, r * a), brush.Get(),
                        1.5f);
    };
    if (name == "app") {
        rect(box(x, y, size, size), BLUE, size * .24f);
        l(6, 18, 12, 5);
        l(12, 5, 18, 18);
        l(8, 14, 16, 14);
        brush->SetColor(color(WHITE));
        dc->DrawLine({x + size * .28f, y + size * .74f}, {x + size * .5f, y + size * .24f}, brush.Get(),
                     2.8f);
        dc->DrawLine({x + size * .5f, y + size * .24f}, {x + size * .72f, y + size * .74f}, brush.Get(),
                     2.8f);
        dc->DrawLine({x + size * .37f, y + size * .56f}, {x + size * .63f, y + size * .56f}, brush.Get(),
                     2.3f);
        return;
    }
    if (name == "home") {
        l(3, 10, 12, 3);
        l(12, 3, 21, 10);
        l(5, 9, 5, 21);
        l(19, 9, 19, 21);
        l(5, 21, 10, 21);
        l(14, 21, 19, 21);
        l(10, 21, 10, 14);
        l(10, 14, 14, 14);
        l(14, 14, 14, 21);
    } else if (name == "skills") {
        rr(3, 3, 4, 18, 1);
        rr(10, 3, 4, 18, 1);
        l(17, 4, 21, 20);
        l(21, 20, 24, 19);
        l(24, 19, 20, 3);
        l(20, 3, 17, 4);
    } else if (name == "agent") {
        rr(4, 7, 16, 13, 4);
        l(12, 3, 12, 7);
        circle(12, 2, 1);
        l(1, 12, 4, 12);
        l(20, 12, 23, 12);
        circle(9, 12, 1);
        circle(15, 12, 1);
        l(9, 16, 15, 16);
    } else if (name == "cloud") {
        rr(3, 10, 18, 10, 5);
        circle(12, 9, 5);
        rect(box(x + 6 * a, y + 11 * a, 12 * a, 6 * a), WHITE, 0);
        l(5, 20, 19, 20);
    } else if (name == "folder") {
        rr(2, 6, 20, 15, 3);
        l(3, 6, 3, 3);
        l(3, 3, 10, 3);
        l(10, 3, 13, 6);
    } else if (name == "settings") {
        circle(12, 12, 4);
        circle(12, 12, 9);
        for (int i = 0; i < 4; ++i) {
            float dx = i % 2 == 0 ? 0 : 10, dy = i % 2 == 0 ? 10 : 0;
            l(12 - dx, 12 - dy, 12 - dx * 1.2f, 12 - dy * 1.2f);
            l(12 + dx, 12 + dy, 12 + dx * 1.2f, 12 + dy * 1.2f);
        }
    } else if (name == "search") {
        circle(10, 10, 6.5f);
        l(15, 15, 22, 22);
    } else if (name == "add") {
        l(12, 4, 12, 20);
        l(4, 12, 20, 12);
    } else if (name == "close") {
        l(6, 6, 18, 18);
        l(6, 18, 18, 6);
    } else if (name == "min") {
        l(5, 12, 19, 12);
    } else if (name == "max") {
        rr(5, 5, 14, 14, 2);
    } else if (name == "restore") {
        rr(4, 8, 12, 12, 2);
        l(8, 4, 20, 4);
        l(20, 4, 20, 16);
    } else if (name == "down") {
        l(6, 9, 12, 15);
        l(12, 15, 18, 9);
    } else if (name == "next" || name == "prev") {
        float m = name == "prev" ? -1 : 1;
        l(12 - 3 * m, 5, 12 + 4 * m, 12);
        l(12 + 4 * m, 12, 12 - 3 * m, 19);
    } else if (name == "check") {
        l(4, 12, 9, 17);
        l(9, 17, 20, 6);
    } else if (name == "scan") {
        circle(12, 12, 8);
        rect(box(x + 13 * a, y + 1 * a, 10 * a, 7 * a), WHITE, 0);
        l(19, 3, 19, 9);
        l(19, 9, 13, 9);
    } else if (name == "star" || name == "star-fill") {
        std::vector<D2D1_POINT_2F> p;
        for (int i = 0; i < 10; ++i) {
            float angle = -1.570796f + i * .6283185f, r = i % 2 ? 4.3f : 10.f;
            p.push_back({x + (12 + cosf(angle) * r) * a, y + (12 + sinf(angle) * r) * a});
        }
        if (name == "star-fill") {
            ComPtr<ID2D1PathGeometry> geometry;
            hr(factory->CreatePathGeometry(&geometry));
            ComPtr<ID2D1GeometrySink> sink;
            hr(geometry->Open(&sink));
            sink->BeginFigure(p[0], D2D1_FIGURE_BEGIN_FILLED);
            sink->AddLines(p.data() + 1, 9);
            sink->EndFigure(D2D1_FIGURE_END_CLOSED);
            hr(sink->Close());
            brush->SetColor(color(c));
            dc->FillGeometry(geometry.Get(), brush.Get());
        }
        for (int i = 0; i < 10; ++i)
            line(p[i].x, p[i].y, p[(i + 1) % 10].x, p[(i + 1) % 10].y, c, 1.5f);
    } else if (name == "code") {
        l(8, 6, 2, 12);
        l(2, 12, 8, 18);
        l(16, 6, 22, 12);
        l(22, 12, 16, 18);
        l(14, 3, 10, 21);
    } else if (name == "chart") {
        l(3, 3, 3, 21);
        l(3, 21, 22, 21);
        rr(7, 12, 3, 6, 1);
        rr(13, 8, 3, 10, 1);
        rr(19, 4, 3, 14, 1);
    } else if (name == "write") {
        rr(3, 3, 14, 18, 2);
        l(7, 8, 12, 8);
        l(7, 12, 11, 12);
        l(13, 16, 21, 6);
        l(21, 6, 23, 8);
        l(23, 8, 15, 18);
        l(13, 16, 13, 20);
        l(13, 20, 17, 18);
    } else if (name == "palette") {
        circle(12, 12, 10);
        circle(7, 10, 1.3f);
        circle(11, 6, 1.3f);
        circle(17, 8, 1.3f);
        circle(18, 14, 1.3f);
        rr(8, 17, 5, 3, 1.5f);
    } else if (name == "workflow") {
        rr(3, 2, 6, 5, 1);
        rr(15, 17, 6, 5, 1);
        rr(3, 17, 6, 5, 1);
        l(6, 7, 6, 12);
        l(6, 12, 18, 12);
        l(6, 12, 6, 17);
        l(18, 12, 18, 17);
        l(13, 9, 16, 12);
        l(16, 12, 13, 15);
    } else if (name == "research") {
        rr(3, 3, 13, 17, 2);
        l(6, 7, 12, 7);
        l(6, 11, 10, 11);
        circle(16, 15, 4);
        l(19, 18, 23, 22);
    } else if (name == "shield") {
        l(12, 2, 21, 6);
        l(21, 6, 20, 15);
        l(20, 15, 12, 22);
        l(12, 22, 4, 15);
        l(4, 15, 3, 6);
        l(3, 6, 12, 2);
        l(7, 11, 11, 15);
        l(11, 15, 17, 8);
    } else if (name == "tool") {
        rr(3, 7, 18, 14, 3);
        rr(8, 3, 8, 4, 1);
        l(3, 12, 21, 12);
        rr(10, 10, 4, 5, 1);
    } else if (name == "generate") {
        l(12, 1, 14, 9);
        l(14, 9, 22, 12);
        l(22, 12, 14, 15);
        l(14, 15, 12, 23);
        l(12, 23, 10, 15);
        l(10, 15, 2, 12);
        l(2, 12, 10, 9);
        l(10, 9, 12, 1);
    } else {
        rr(5, 2, 14, 20, 2);
        l(9, 8, 15, 8);
        l(9, 12, 15, 12);
        l(9, 16, 13, 16);
    }
}
void Window::brand(const std::string &key, Rect r) {
    if (key == "crush" || key == "glm" || key == "doubao" || key == "baidu" || key == "zcode") {
        auto &bitmap = bitmaps[key];
        if (!bitmap) {
            auto raw = resource(brand_resources.at(key));
            ComPtr<IStream> stream;
            stream.Attach(SHCreateMemStream((const BYTE *)raw.data(), (UINT)raw.size()));
            ComPtr<IWICImagingFactory> wic;
            hr(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wic)));
            ComPtr<IWICBitmapDecoder> decoder;
            hr(wic->CreateDecoderFromStream(stream.Get(), nullptr, WICDecodeMetadataCacheOnLoad, &decoder));
            ComPtr<IWICBitmapFrameDecode> frame;
            hr(decoder->GetFrame(0, &frame));
            ComPtr<IWICFormatConverter> converter;
            hr(wic->CreateFormatConverter(&converter));
            hr(converter->Initialize(frame.Get(), GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone,
                                     nullptr, 0, WICBitmapPaletteTypeCustom));
            hr(dc->CreateBitmapFromWicBitmap(converter.Get(), nullptr, &bitmap));
        }
        auto size = bitmap->GetSize();
        // The current official Wenxin header contains the symbol and wordmark.
        // Display its original square symbol without substituting the site's favicon.
        D2D1_RECT_F source = box(0, 0, key == "baidu" ? size.height : size.width, size.height);
        if (key == "baidu")
            size.width = size.height;
        float scale = std::min((r.right - r.left) / size.width, (r.bottom - r.top) / size.height);
        float w = size.width * scale, h = size.height * scale;
        dc->DrawBitmap(bitmap.Get(), box((r.left + r.right - w) / 2, (r.top + r.bottom - h) / 2, w, h), 1,
                       D2D1_BITMAP_INTERPOLATION_MODE_LINEAR, &source);
        return;
    }
    auto it = brand_resources.find(key);
    if (it == brand_resources.end()) {
        rect(r, SOFT, (r.bottom - r.top) * .24f);
        text(key.empty() ? "API" : key.substr(0, 2), r, (r.bottom - r.top) * .28f, BLUE, true, false, 1);
        return;
    }
    auto &svg = svgs[key];
    if (!svg) {
        auto raw = resource(it->second);
        ComPtr<IStream> stream;
        stream.Attach(SHCreateMemStream((const BYTE *)raw.data(), (UINT)raw.size()));
        if (!stream || FAILED(dc->CreateSvgDocument(stream.Get(), {24, 24}, &svg))) {
            svgs.erase(key);
            rect(r, SOFT, 8);
            text(key.substr(0, 2), r, 12, BLUE, true, false, 1);
            return;
        }
        ComPtr<ID2D1SvgElement> root;
        svg->GetRoot(&root);
        if (root) {
            D2D1_SVG_LENGTH len{24, D2D1_SVG_LENGTH_UNITS_NUMBER};
            root->SetAttributeValue(L"width", len);
            root->SetAttributeValue(L"height", len);
        }
    }
    auto old = D2D1::Matrix3x2F::Identity();
    dc->GetTransform(&old);
    float w = r.right - r.left, h = r.bottom - r.top;
    dc->SetTransform(D2D1::Matrix3x2F::Scale(w / 24, h / 24) * D2D1::Matrix3x2F::Translation(r.left, r.top) *
                     old);
    dc->DrawSvgDocument(svg.Get());
    dc->SetTransform(old);
}
void Window::hit(Rect r, std::function<void()> fn, bool enabled) {
    if (clipping)
        r = intersection(r, view);
    hits.push_back({r, std::move(fn), enabled});
}
void Window::button(const std::string &label, Rect r, std::function<void()> fn, int style,
                    const std::string &glyph, bool enabled) {
    int id = (int)hits.size();
    hit(r, std::move(fn), enabled);
    bool over = id == hover && enabled;
    unsigned fill = style == 1 ? BLUE : style == 2 ? SOFT : style == 3 ? BG : WHITE;
    unsigned fg = style == 1 ? WHITE : style == 2 ? BLUE : BODY;
    if (style == 4) {
        fill = WHITE;
        fg = RED;
    }
    if (over)
        fill = style == 1 ? 0x2467DE : style == 4 ? 0xFFF0F2 : 0xE9F1FB;
    if (!enabled) {
        fill = 0xF0F4FA;
        fg = 0xACBACA;
    }
    float h = r.bottom - r.top, w = r.right - r.left;
    float radius = label.empty() ? h / 2 : std::min(12.f, h * .3f);
    if (enabled && (style == 0 || style == 1 || style == 2) && h >= 34 && w >= 48)
        rect(box(r.left, r.top + 2, w, h), style == 1 ? 0xDCE8FF : 0xEAF0F8, radius);
    if (pressed == id && over)
        fill = style == 1 ? 0x205CC8 : 0xDDE9F9;
    rect(r, fill, style == 5 ? 10.f : radius, (style == 0 || style == 5) ? LINE : style == 2 ? 0xD6E5FC : 0);
    float tx = r.left + 14;
    if (!glyph.empty() && !(style == 5 && glyph == "down")) {
        icon(glyph, r.left + (label.empty() ? (r.right - r.left - 18) / 2 : 12),
             r.top + (r.bottom - r.top - 18) / 2, 18, fg);
        tx += 24;
    }
    if (style == 5 && glyph == "down")
        icon("down", r.right - 28, r.top + (r.bottom - r.top - 16) / 2, 16, fg);
    if (!label.empty())
        text(label,
             box(tx, r.top, r.right - tx - (style == 5 && glyph == "down" ? 36 : 12), r.bottom - r.top), 12,
             fg, style == 1, false,
             style == 5      ? 0
             : glyph.empty() ? 1
                             : 0);
    if (keyboardFocus && focus == id && GetFocus() == hwnd) {
        brush->SetColor(color(BLUE));
        auto border = r;
        border.left += 3;
        border.top += 3;
        border.right -= 3;
        border.bottom -= 3;
        dc->DrawRoundedRectangle(D2D1::RoundedRect(border, 12, 12), brush.Get(), 1);
    }
}
void Window::caption(bool main) {
    rect(box(0, 0, width, 56), WHITE);
    line(0, 55.5f, width, 55.5f, LINE, 1);
    if (main) {
        icon("app", 20, 14, 30);
        text(title.starts_with("AgentDock-QA") ? "AgentDock · 测试" : "AgentDock", box(61, 8, 230, 40), 18,
             INK, true);
    } else
        text(title, box(22, 6, width - 84, 44), 17, INK, true);
    if (main) {
        button("", box(width - 116, 13, 30, 30), [this] { ShowWindow(hwnd, SW_MINIMIZE); }, 3, "min");
        button(
            "", box(width - 78, 13, 30, 30),
            [this] { ShowWindow(hwnd, IsZoomed(hwnd) ? SW_RESTORE : SW_MAXIMIZE); }, 3,
            IsZoomed(hwnd) ? "restore" : "max");
    }
    button("", box(width - 40, 13, 28, 30), [this] { close(); }, 4, "close");
}
void Window::beginScroll(Rect r) {
    view = r;
    clipping = true;
    dc->PushAxisAlignedClip(r, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
}
void Window::endScroll(float content) {
    dc->PopAxisAlignedClip();
    clipping = false;
    float h = view.bottom - view.top;
    scrollMax = std::max(0.f, content - h);
    scroll = std::clamp(scroll, 0.f, scrollMax);
    if (scrollMax > 0) {
        float th = std::max(32.f, h * h / content), y = view.top + (h - th) * scroll / scrollMax;
        rect(box(view.right + 5, view.top, 6, h), 0xEDF3FB, 3);
        rect(box(view.right + 5, y, 6, th), dragScroll ? 0x89A8D1 : 0xB7CAE3, 3);
    }
}
void Window::invalidate() {
    if (hwnd)
        InvalidateRect(hwnd, nullptr, FALSE);
}
void Window::paint() {
    PAINTSTRUCT ps;
    BeginPaint(hwnd, &ps);
    try {
        initGraphics();
        hits.clear();
        dc->BeginDraw();
        dc->SetTransform(D2D1::Matrix3x2F::Identity());
        dc->Clear(color(BG));
        // Every window reserves a status strip; messages never cover controls or content.
        float frameHeight = height;
        height -= 32;
        try {
            paintedEdits.clear();
            draw();
            for (auto &[id, control] : edits)
                if (!paintedEdits.count(control))
                    ShowWindow(control, SW_HIDE);
            drawPopup();
        } catch (...) {
            height = frameHeight;
            throw;
        }
        height = frameHeight;
        float statusY = height - 32;
        rect(box(0, statusY, width, 32), WHITE);
        line(0, statusY, width, statusY, LINE, 1);
        float statusX = owner ? 22.f : 232.f;
        if (worker.joinable()) {
            float phase = float((GetTickCount64() - jobStarted) % 1600) / 1600.f;
            rect(box(statusX, statusY + 12, 8, 8), BLUE, 4);
            text(jobLabel + " · " + std::to_string((GetTickCount64() - jobStarted) / 1000) + " 秒",
                 box(statusX + 20, statusY, width - statusX - 140, 32), 11, BODY);
            float track = width - statusX;
            rect(box(statusX + (track - 80) * phase, statusY, 80, 2), BLUE, 1);
            if (cancelJob && !jobLabel.starts_with("安装 ") && !jobLabel.starts_with("卸载 ") &&
                jobLabel != "生成 Skill 草稿" && jobLabel != "完善 Skill 草稿" &&
                jobLabel != "正在取消 Skill 草稿") {
                button(
                    "取消", box(width - 92, statusY + 3, 70, 26),
                    [this] {
                        if (cancelJob)
                            cancelJob();
                        jobLabel = jobLabel.find("Skill 草稿") != std::string::npos ? "正在取消 Skill 草稿"
                                                                                    : "正在取消";
                    },
                    2);
            }
        } else {
            text(toastUntil > GetTickCount64() ? toast : "就绪",
                 box(statusX, statusY, width - statusX - 22, 32), 11,
                 toastUntil > GetTickCount64() ? BODY : MUTED);
        }
        HRESULT r = dc->EndDraw();
        HRESULT present = SUCCEEDED(r) ? swap->Present(1, 0) : r;
        auto ready = env("AGENTDOCK_READY_FILE");
        if (!owner && !ready.empty() && (!readyReported || FAILED(r) || FAILED(present))) {
            atomic_write(path(ready), J{{"draw", (int64_t)r},
                                        {"present", (int64_t)present},
                                        {"width", width},
                                        {"height", height},
                                        {"version", "1.0.0"}}
                                          .dump());
            readyReported = true;
        }
        if (SUCCEEDED(r) && SUCCEEDED(present))
            framePresented();
        if (FAILED(r) || FAILED(present)) {
            svgs.clear();
            bitmaps.clear();
            formats.clear();
            brush.Reset();
            targetBitmap.Reset();
            swap.Reset();
            dc.Reset();
            device.Reset();
            factory.Reset();
            dw.Reset();
            d3d.Reset();
        }
    } catch (const std::exception &e) {
        EndPaint(hwnd, &ps);
        MessageBoxW(hwnd, wide(e.what()).c_str(), L"AgentDock", MB_OK | MB_ICONERROR);
        return;
    }
    EndPaint(hwnd, &ps);
}
void Window::popup(std::vector<Choice> items, std::function<void(std::string)> selected, Rect anchor) {
    dismissPopup();
    popupItems = std::move(items);
    popupSelected = std::move(selected);
    popupAnchor = anchor.right > anchor.left ? anchor : lastAction;
    if (popupAnchor.right <= popupAnchor.left)
        popupAnchor = box(width / 2 - 140, 75, 280, 32);
    popupOffset = popupFocus = 0;
    value(91, "");
    invalidate();
}
void Window::dismissPopup() {
    popupSelected = {};
    popupItems.clear();
    popupFiltered.clear();
    if (edits.count(91))
        ShowWindow(edits[91], SW_HIDE);
    invalidate();
}
bool Window::popupKey(WPARAM key) {
    if (!popupSelected)
        return false;
    if (key == VK_ESCAPE || key == VK_TAB) {
        dismissPopup();
        SetFocus(hwnd);
        return true;
    }
    if (key == VK_UP || key == VK_DOWN || key == VK_PRIOR || key == VK_NEXT) {
        int step = key == VK_PRIOR ? -6 : key == VK_NEXT ? 6 : key == VK_UP ? -1 : 1;
        popupFocus = std::clamp(popupFocus + step, 0, std::max(0, (int)popupFiltered.size() - 1));
        if (popupFocus < popupOffset)
            popupOffset = popupFocus;
        if (popupFocus >= popupOffset + 6)
            popupOffset = popupFocus - 5;
        invalidate();
        return true;
    }
    if (key == VK_RETURN) {
        if (!popupFiltered.empty()) {
            auto id = popupItems[popupFiltered[std::min(popupFocus, (int)popupFiltered.size() - 1)]].id;
            auto fn = popupSelected;
            dismissPopup();
            SetFocus(hwnd);
            guard([&] { fn(id); });
        }
        return true;
    }
    return false;
}
void Window::drawPopup() {
    if (!popupSelected) {
        if (edits.count(91))
            ShowWindow(edits[91], SW_HIDE);
        return;
    }
    auto fold = [](std::string text) {
        for (auto &c : text)
            if ((unsigned char)c < 128)
                c = (char)tolower(c);
        return text;
    };
    auto filter = fold(value(91));
    popupFiltered.clear();
    for (int i = 0; i < (int)popupItems.size(); ++i)
        if (filter.empty() ||
            fold(popupItems[i].name + " " + popupItems[i].note).find(filter) != std::string::npos)
            popupFiltered.push_back(i);
    popupOffset = std::clamp(popupOffset, 0, std::max(0, (int)popupFiltered.size() - 6));
    popupFocus = std::clamp(popupFocus, 0, std::max(0, (int)popupFiltered.size() - 1));
    float w = std::clamp(popupAnchor.right - popupAnchor.left, 260.f, std::min(560.f, width - 32));
    bool search = popupItems.size() > 8;
    float rows = std::max(1, std::min(6, (int)popupFiltered.size())), header = search ? 50.f : 8.f;
    float h = header + rows * 44 + 8;
    float x = std::clamp(popupAnchor.left, 16.f, width - w - 16);
    float y = popupAnchor.bottom + 6;
    if (y + h > height - 10)
        y = std::max(58.f, popupAnchor.top - h - 6);
    popupBounds = box(x, y, w, h);
    // This is drawn in the same HWND. Native edits underneath must not cover the list.
    for (auto &[id, control] : edits)
        if (id != 91 && IsWindowVisible(control)) {
            RECT r;
            GetWindowRect(control, &r);
            MapWindowPoints(nullptr, hwnd, (POINT *)&r, 2);
            Rect logical = {(float)r.left * 96 / dpi, (float)r.top * 96 / dpi, (float)r.right * 96 / dpi,
                            (float)r.bottom * 96 / dpi};
            auto overlap = intersection(logical, popupBounds);
            if (overlap.right > overlap.left && overlap.bottom > overlap.top)
                ShowWindow(control, SW_HIDE);
        }
    hits.clear();
    hover = -1;
    rect(box(x + 2, y + 5, w, h), 0xDCE7F5, 12);
    rect(popupBounds, WHITE, 12, LINE);
    if (search)
        edit(91, box(x + 8, y + 8, w - 16, 34), "", false, false, false, "筛选选项", true);
    else if (edits.count(91))
        ShowWindow(edits[91], SW_HIDE);
    if (popupFiltered.empty())
        text("没有匹配项", box(x + 14, y + header, w - 28, 44), 12, MUTED);
    for (int i = popupOffset; i < std::min((int)popupFiltered.size(), popupOffset + 6); ++i) {
        auto item = popupItems[popupFiltered[i]];
        float rowY = y + header + (i - popupOffset) * 44;
        Rect r = box(x + 6, rowY, w - 12, 42);
        if (i == popupFocus)
            rect(r, SOFT, 8);
        float tx = x + 15;
        if (!item.brand.empty()) {
            brand(item.brand, box(tx, rowY + 9, 24, 24));
            tx += 34;
        }
        text(item.name,
             box(tx, rowY + (item.note.empty() ? 2 : 0), x + w - tx - 15, item.note.empty() ? 38 : 24), 12,
             INK, true);
        if (!item.note.empty())
            text(item.note, box(tx, rowY + 23, x + w - tx - 15, 17), 10, MUTED);
        hit(r, [this, id = item.id] {
            auto fn = popupSelected;
            dismissPopup();
            SetFocus(hwnd);
            if (fn)
                fn(id);
        });
    }
    if (popupFiltered.size() > 6) {
        float track = rows * 44, thumb = std::max(20.f, track * 6 / popupFiltered.size());
        float sy = y + header + (track - thumb) * popupOffset / (popupFiltered.size() - 6);
        rect(box(x + w - 5, sy, 3, thumb), 0xA9C1DF, 2);
    }
}
void Window::removeEdit(int id) {
    auto it = edits.find(id);
    if (it == edits.end())
        return;
    placeholders.erase(it->second);
    textAreas.erase(it->second);
    DestroyWindow(it->second);
    edits.erase(it);
}
void Window::activity(Rect r) {
    rect(r, 0xF4F8FF, 12, LINE);
    float cx = (r.left + r.right) / 2, cy = (r.top + r.bottom) / 2 - 40;
    float angle = float((GetTickCount64() - jobStarted) % 1600) / 1600.f * 6.2831853f;
    for (int i = 0; i < 8; ++i) {
        float a = angle + i * 0.785398f;
        float radius = i == 7 ? 4.f : 3.f;
        rect(box(cx + std::cos(a) * 19 - radius, cy + std::sin(a) * 19 - radius, radius * 2, radius * 2),
             i > 4 ? BLUE : 0xB5D2FF, radius);
    }
    text("正在生成 Skill", box(r.left + 16, cy + 38, r.right - r.left - 32, 30), 16, INK, true, false, 1);
    text("等待模型返回 · " + std::to_string((GetTickCount64() - jobStarted) / 1000) + " 秒",
         box(r.left + 16, cy + 73, r.right - r.left - 32, 25), 12, MUTED, false, false, 1);
    text("原有草稿会保留，生成完成后可继续编辑。", box(r.left + 20, cy + 110, r.right - r.left - 40, 48), 11,
         MUTED, false, true, 1);
    button(
        "取消生成", box(cx - 54, cy + 175, 108, 34),
        [this] {
            if (cancelJob)
                cancelJob();
            jobLabel = jobLabel.find("Skill 草稿") != std::string::npos ? "正在取消 Skill 草稿" : "正在取消";
        },
        0);
}
void Window::notice(const std::string &s) {
    toast = s;
    toastUntil = GetTickCount64() + 4200;
    SetTimer(hwnd, 3, 4300, nullptr);
    invalidate();
}
void Window::error(const std::string &s) {
    MessageBoxW(hwnd, wide(s).c_str(), L"AgentDock", MB_OK | MB_ICONWARNING);
}
bool Window::confirm(const std::string &t, const std::string &s) {
    return MessageBoxW(hwnd, wide(s).c_str(), wide(t).c_str(), MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2) ==
           IDYES;
}
void Window::guard(std::function<void()> fn) {
    try {
        fn();
    } catch (const std::exception &e) {
        error(e.what());
    }
}
void Window::close() {
    if (!hwnd || closing)
        return;
    if (!beforeClose())
        return;
    closing = true;
    if (worker.joinable()) {
        if (cancelJob)
            cancelJob();
        return;
    }
    DestroyWindow(hwnd);
}
void Window::clipboard(const std::string &s) {
    auto t = wide(s);
    if (!OpenClipboard(hwnd))
        throw std::runtime_error("剪贴板正在使用");
    auto mem = GlobalAlloc(GMEM_MOVEABLE, (t.size() + 1) * 2);
    if (!mem) {
        CloseClipboard();
        throw std::runtime_error("无法分配剪贴板内存");
    }
    memcpy(GlobalLock(mem), t.c_str(), (t.size() + 1) * 2);
    GlobalUnlock(mem);
    EmptyClipboard();
    if (!SetClipboardData(CF_UNICODETEXT, mem))
        GlobalFree(mem);
    CloseClipboard();
    notice("已复制");
}
fs::path Window::choose(bool folder, bool save, const std::string &name) {
    ComPtr<IFileDialog> dialog;
    if (save) {
        ComPtr<IFileSaveDialog> d;
        hr(CoCreateInstance(CLSID_FileSaveDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&d)));
        hr(d.As(&dialog));
    } else {
        ComPtr<IFileOpenDialog> d;
        hr(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&d)));
        hr(d.As(&dialog));
    }
    DWORD options;
    dialog->GetOptions(&options);
    dialog->SetOptions(options | FOS_FORCEFILESYSTEM | (folder ? FOS_PICKFOLDERS : 0));
    if (!name.empty())
        dialog->SetFileName(wide(name).c_str());
    if (!folder) {
        bool executable = !save && name == "*.exe;*.cmd;*.bat";
        COMDLG_FILTERSPEC filters[] = {{executable ? L"Agent executable"
                                        : save     ? L"ZIP Skill package"
                                                   : L"SKILL.md",
                                        executable ? L"*.exe;*.cmd;*.bat"
                                        : save     ? L"*.zip"
                                                   : L"SKILL.md"},
                                       {L"All files", L"*.*"}};
        dialog->SetFileTypes(2, filters);
        if (save)
            dialog->SetDefaultExtension(L"zip");
    }
    HRESULT r = dialog->Show(hwnd);
    if (r == HRESULT_FROM_WIN32(ERROR_CANCELLED))
        return {};
    hr(r);
    ComPtr<IShellItem> item;
    hr(dialog->GetResult(&item));
    PWSTR out = nullptr;
    hr(item->GetDisplayName(SIGDN_FILESYSPATH, &out));
    fs::path p(out);
    CoTaskMemFree(out);
    return p;
}
void Window::browse(const std::string &target) {
    if ((INT_PTR)ShellExecuteW(hwnd, L"open", wide(target).c_str(), nullptr, nullptr, SW_SHOWNORMAL) <= 32)
        throw std::runtime_error("无法打开此位置或链接");
}
static std::string crlf(const std::string &raw) {
    std::string out;
    out.reserve(raw.size());
    for (size_t i = 0; i < raw.size(); ++i) {
        char c = raw[i];
        if (c == '\n' && (i == 0 || raw[i - 1] != '\r'))
            out += '\r';
        out += c;
    }
    return out;
}
HWND Window::edit(int id, Rect r, const std::string &initial, bool multi, bool secret, bool readonly,
                  const std::string &placeholder, bool search) {
    auto &e = edits[id];
    if (!e) {
        if (!editFont)
            editFont = CreateFontW(-(int)(14 * dpi / 96), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                   DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                   CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Microsoft YaHei UI");
        e = CreateWindowExW(0, L"EDIT", wide(multi ? crlf(initial) : initial).c_str(),
                            WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_LEFT |
                                (multi ? ES_MULTILINE | ES_AUTOVSCROLL | ES_WANTRETURN : ES_AUTOHSCROLL) |
                                (secret ? ES_PASSWORD : 0) | (readonly ? ES_READONLY : 0),
                            0, 0, 0, 0, hwnd, (HMENU)(INT_PTR)id, GetModuleHandleW(nullptr), nullptr);
        SendMessageW(e, WM_SETFONT, (WPARAM)editFont, TRUE);
        SendMessageW(e, EM_SETLIMITTEXT, multi ? 512 * 1024 : 16384, 0);
        placeholders[e] = wide(placeholder);
        SetWindowSubclass(e, editProc, 1, (DWORD_PTR)this);
        SendMessageW(e, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, MAKELPARAM(2, 2));
    }
    placeholders[e] = wide(placeholder);
    paintedEdits.insert(e);
    auto visible = clipping ? intersection(r, view) : r;
    if (visible.right <= visible.left || visible.bottom <= visible.top) {
        ShowWindow(e, SW_HIDE);
        return e;
    }
    rect(r, WHITE, 10, GetFocus() == e ? 0xA6C6F5 : LINE);
    if (search)
        icon("search", r.left + 11, r.top + (r.bottom - r.top - 16) / 2, 16, MUTED);
    float pad = multi ? 12.f : 10.f;
    float leftPad = search ? 36.f : pad;
    float top = multi ? 10.f : (r.bottom - r.top - 23) / 2;
    RECT desired = {(LONG)((r.left + leftPad) * dpi / 96), (LONG)((r.top + top) * dpi / 96),
                    (LONG)((r.right - pad - (multi ? 8 : 0)) * dpi / 96),
                    (LONG)((r.bottom - (multi ? 10 : top)) * dpi / 96)};
    RECT current;
    GetWindowRect(e, &current);
    MapWindowPoints(nullptr, hwnd, (POINT *)&current, 2);
    if (!EqualRect(&desired, &current))
        SetWindowPos(e, nullptr, desired.left, desired.top, desired.right - desired.left,
                     desired.bottom - desired.top, SWP_NOZORDER | SWP_NOACTIVATE);
    RECT crop = {
        (LONG)std::max(0.f, visible.left * dpi / 96 - desired.left),
        (LONG)std::max(0.f, visible.top * dpi / 96 - desired.top),
        (LONG)std::min(float(desired.right - desired.left), visible.right * dpi / 96 - desired.left),
        (LONG)std::min(float(desired.bottom - desired.top), visible.bottom * dpi / 96 - desired.top)};
    auto region = CreateRectRgn(crop.left, crop.top, crop.right, crop.bottom);
    auto previousRegion = CreateRectRgn(0, 0, 0, 0);
    int regionType = GetWindowRgn(e, previousRegion);
    if (regionType == ERROR || !EqualRgn(region, previousRegion))
        SetWindowRgn(e, region, TRUE);
    else
        DeleteObject(region);
    DeleteObject(previousRegion);
    if (!IsWindowVisible(e))
        ShowWindow(e, SW_SHOWNA);
    if (multi) {
        textAreas[e].bounds = r;
        textAreas[e].visible = visible;
        auto info = textMetrics(e);
        float track = std::max(1.f, r.bottom - r.top - 20);
        if (info.maximum() > 0)
            rect(box(r.right - 10, r.top + 10 + info.position(track), 4, info.thumb(track)),
                 draggingText == e ? 0x7E9FC9 : 0xB7CAE3, 2);
    }
    return e;
}
TextScroll Window::textMetrics(HWND control) {
    TextScroll result;
    result.lines = std::max(1, (int)SendMessageW(control, EM_GETLINECOUNT, 0, 0));
    result.first = (int)SendMessageW(control, EM_GETFIRSTVISIBLELINE, 0, 0);
    RECT area{};
    SendMessageW(control, EM_GETRECT, 0, (LPARAM)&area);
    auto hdc = GetDC(control);
    auto previous = SelectObject(hdc, editFont);
    TEXTMETRICW tm{};
    GetTextMetricsW(hdc, &tm);
    SelectObject(hdc, previous);
    ReleaseDC(control, hdc);
    result.page = std::max(1, (int)((area.bottom - area.top) / std::max(1L, tm.tmHeight)));
    return result;
}
void Window::textScrollTo(HWND control, int line) {
    auto info = textMetrics(control);
    SendMessageW(control, EM_LINESCROLL, 0, info.clamped(line) - info.first);
    invalidate();
}
bool Window::textWheel(POINT point, int delta) {
    ScreenToClient(hwnd, &point);
    float x = point.x * 96.f / dpi, y = point.y * 96.f / dpi;
    for (auto &[control, area] : textAreas)
        if (IsWindow(control) && IsWindowVisible(control) && contains(area.visible, x, y)) {
            UINT lines = 3;
            SystemParametersInfoW(SPI_GETWHEELSCROLLLINES, 0, &lines, 0);
            auto info = textMetrics(control);
            area.wheelRemainder += delta;
            auto notches = area.wheelRemainder / WHEEL_DELTA;
            area.wheelRemainder %= WHEEL_DELTA;
            if (notches)
                textScrollTo(control,
                             info.first - notches * (lines == WHEEL_PAGESCROLL ? info.page
                                                                               : (int)std::min(lines, 100u)));
            return true;
        }
    return false;
}
std::string Window::value(int id) {
    auto it = edits.find(id);
    if (it == edits.end())
        return {};
    int n = GetWindowTextLengthW(it->second);
    std::wstring s(n + 1, 0);
    GetWindowTextW(it->second, s.data(), n + 1);
    s.resize(n);
    auto r = utf8(s);
    r.erase(std::remove(r.begin(), r.end(), '\r'), r.end());
    return r;
}
void Window::value(int id, const std::string &s) {
    auto it = edits.find(id);
    if (it != edits.end() && value(id) != s)
        SetWindowTextW(it->second, wide(crlf(s)).c_str());
}
void Window::hideEdits() {
    for (auto &[k, e] : edits)
        ShowWindow(e, SW_HIDE);
}
void Window::job(std::function<J()> work, std::function<void(J)> done, const std::string &label) {
    if (worker.joinable())
        throw std::runtime_error("已有任务正在进行");
    jobLabel = label;
    jobStarted = GetTickCount64();
    SetTimer(hwnd, 8, 50, nullptr);
    stop = false;
    worker = std::thread([this, work = std::move(work), done = std::move(done)] {
        auto r = new JobResult;
        r->done = done;
        try {
            r->data = work();
        } catch (const std::exception &e) {
            r->error = e.what();
        }
        if (!PostMessageW(hwnd, WM_APP + 2, 0, (LPARAM)r))
            delete r;
    });
    invalidate();
}
LRESULT CALLBACK Window::editProc(HWND e, UINT msg, WPARAM w, LPARAM l, UINT_PTR, DWORD_PTR data) {
    auto self = (Window *)data;
    if (msg == WM_MOUSEWHEEL) {
        SendMessageW(self->hwnd, WM_MOUSEWHEEL, w, l);
        return 0;
    }
    if (msg == WM_PAINT) {
        auto result = DefSubclassProc(e, msg, w, l);
        auto hint = self->placeholders.find(e);
        if (GetWindowTextLengthW(e) == 0 && hint != self->placeholders.end() && !hint->second.empty()) {
            HDC hdc = GetDC(e);
            RECT bounds;
            GetClientRect(e, &bounds);
            bounds.left += 2;
            auto font = SelectObject(hdc, self->editFont);
            SetBkMode(hdc, TRANSPARENT);
            SetTextColor(hdc, RGB(123, 145, 173));
            bool multi = (GetWindowLongPtrW(e, GWL_STYLE) & ES_MULTILINE) != 0;
            DrawTextW(hdc, hint->second.c_str(), -1, &bounds,
                      DT_NOPREFIX | (multi ? DT_WORDBREAK : DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS));
            SelectObject(hdc, font);
            ReleaseDC(e, hdc);
        }
        return result;
    }
    if (msg == WM_KEYDOWN && self->popupKey(w))
        return 0;
    if (msg == WM_KEYDOWN && (GetKeyState(VK_CONTROL) & 0x8000) &&
        (w == 'A' || w == 'S' || w == 'K' || (w >= '1' && w <= '6'))) {
        if (w == 'A') {
            SendMessageW(e, EM_SETSEL, 0, -1);
            return 0;
        }
        self->key(w);
        return 0;
    }
    if (msg == WM_KEYDOWN && w == VK_ESCAPE) {
        self->guard([&] { self->key(w); });
        return 0;
    }
    if (msg == WM_KEYDOWN && w == VK_RETURN && GetDlgCtrlID(e) == 90) {
        self->guard([&] { self->key(w); });
        return 0;
    }
    if (msg == WM_KEYDOWN && w == VK_TAB) {
        std::vector<HWND> visible;
        for (auto &[id, control] : self->edits)
            if (IsWindowVisible(control))
                visible.push_back(control);
        auto it = std::find(visible.begin(), visible.end(), e);
        bool backward = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
        if (it != visible.end() && (!backward && it + 1 != visible.end()))
            SetFocus(*(it + 1));
        else if (it != visible.end() && backward && it != visible.begin())
            SetFocus(*(it - 1));
        else {
            SetFocus(self->hwnd);
            self->keyboardFocus = true;
            self->focus = backward ? (int)self->hits.size() - 1 : 0;
            self->invalidate();
        }
        return 0;
    }
    auto r = DefSubclassProc(e, msg, w, l);
    if (msg == WM_MOUSEWHEEL || msg == WM_VSCROLL || msg == WM_KEYUP) {
        self->invalidate();
    }
    if (msg == WM_SETFOCUS || msg == WM_KILLFOCUS || msg == WM_SETTEXT || msg == WM_KEYUP) {
        InvalidateRect(e, nullptr, FALSE);
        self->invalidate();
    }
    return r;
}
LRESULT CALLBACK Window::proc(HWND h, UINT msg, WPARAM w, LPARAM l) {
    auto self = (Window *)GetWindowLongPtrW(h, GWLP_USERDATA);
    if (msg == WM_NCCREATE) {
        self = (Window *)((CREATESTRUCTW *)l)->lpCreateParams;
        self->hwnd = h;
        SetWindowLongPtrW(h, GWLP_USERDATA, (LONG_PTR)self);
    }
    if (self) {
        try {
            return self->message(msg, w, l);
        } catch (const std::exception &e) {
            self->error(e.what());
            return 0;
        }
    }
    return DefWindowProcW(h, msg, w, l);
}
LRESULT Window::message(UINT msg, WPARAM w, LPARAM l) {
    switch (msg) {
    case WM_NCCALCSIZE:
        if (w)
            return 0;
        break;
    case WM_NCHITTEST: {
        POINT pt{GET_X_LPARAM(l), GET_Y_LPARAM(l)};
        ScreenToClient(hwnd, &pt);
        float x = pt.x * 96 / dpi, y = pt.y * 96 / dpi;
        if (!IsZoomed(hwnd)) {
            bool left = x<6, right = x> width - 6, top = y<6, bottom = y> height - 6;
            if (top && left)
                return HTTOPLEFT;
            if (top && right)
                return HTTOPRIGHT;
            if (bottom && left)
                return HTBOTTOMLEFT;
            if (bottom && right)
                return HTBOTTOMRIGHT;
            if (top)
                return HTTOP;
            if (bottom)
                return HTBOTTOM;
            if (left)
                return HTLEFT;
            if (right)
                return HTRIGHT;
        }
        if (y < 56) {
            for (auto &h : hits)
                if (contains(h.r, x, y))
                    return HTCLIENT;
            return HTCAPTION;
        }
        return HTCLIENT;
    }
    case WM_GETMINMAXINFO: {
        auto mm = (MINMAXINFO *)l;
        mm->ptMinTrackSize = owner ? POINT{(LONG)(minWidth * dpi / 96), (LONG)(minHeight * dpi / 96)}
                                   : POINT{(LONG)(980 * dpi / 96), (LONG)(640 * dpi / 96)};
        MONITORINFO mi{sizeof mi};
        GetMonitorInfoW(MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST), &mi);
        mm->ptMaxPosition = {mi.rcWork.left - mi.rcMonitor.left, mi.rcWork.top - mi.rcMonitor.top};
        mm->ptMaxSize = {mi.rcWork.right - mi.rcWork.left, mi.rcWork.bottom - mi.rcWork.top};
        return 0;
    }
    case WM_SIZE:
        if (w != SIZE_MINIMIZED) {
            width = LOWORD(l) * 96.f / dpi;
            height = HIWORD(l) * 96.f / dpi;
            resizeGraphics();
            resized();
            invalidate();
        }
        return 0;
    case WM_DPICHANGED: {
        dpi = HIWORD(w);
        auto r = (RECT *)l;
        SetWindowPos(hwnd, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top,
                     SWP_NOZORDER | SWP_NOACTIVATE);
        if (editFont) {
            DeleteObject(editFont);
            editFont = nullptr;
            for (auto &[id, e] : edits) {
                auto f = CreateFontW(-(int)(14 * dpi / 96), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                     DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                     CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Microsoft YaHei UI");
                if (!editFont)
                    editFont = f;
                else
                    DeleteObject(f);
                SendMessageW(e, WM_SETFONT, (WPARAM)editFont, TRUE);
            }
        }
        resizeGraphics();
        invalidate();
        return 0;
    }
    case WM_PAINT:
        paint();
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_MOUSEMOVE: {
        mouse = {GET_X_LPARAM(l), GET_Y_LPARAM(l)};
        float x = mouse.x * 96 / dpi, y = mouse.y * 96 / dpi;
        if (draggingText) {
            auto control = draggingText;
            auto it = textAreas.find(control);
            if (it != textAreas.end() && IsWindow(control)) {
                auto r = it->second.bounds;
                auto info = textMetrics(control);
                textScrollTo(control, info.fromPosition(y - r.top - 10 - textGrab, r.bottom - r.top - 20));
            }
            return 0;
        }
        if (dragScroll) {
            float h = view.bottom - view.top, content = h + scrollMax, th = std::max(32.f, h * h / content);
            scroll = std::clamp(dragOffset + (y - dragY) * scrollMax / (h - th), 0.f, scrollMax);
            invalidate();
            return 0;
        }
        int over = -1;
        for (size_t i = 0; i < hits.size(); ++i)
            if (hits[i].enabled && contains(hits[i].r, x, y))
                over = (int)i;
        if (over != hover) {
            hover = over;
            invalidate();
        }
        TRACKMOUSEEVENT t{sizeof t, TME_LEAVE, hwnd, 0};
        TrackMouseEvent(&t);
        SetCursor(LoadCursorW(nullptr, over >= 0 ? IDC_HAND : IDC_ARROW));
        return 0;
    }
    case WM_MOUSELEAVE:
        hover = -1;
        invalidate();
        return 0;
    case WM_LBUTTONDOWN: {
        float x = GET_X_LPARAM(l) * 96.f / dpi, y = GET_Y_LPARAM(l) * 96.f / dpi;
        mouse = {GET_X_LPARAM(l), GET_Y_LPARAM(l)};
        if (!popupSelected)
            for (auto &[control, area] : textAreas) {
                auto r = area.bounds;
                auto info = IsWindowVisible(control) ? textMetrics(control) : TextScroll{};
                if (info.maximum() > 0 && contains(area.visible, x, y) &&
                    contains(box(r.right - 18, r.top + 10, 18, r.bottom - r.top - 20), x, y)) {
                    auto track = r.bottom - r.top - 20, thumb = info.thumb(track),
                         sy = r.top + 10 + info.position(track);
                    draggingText = control;
                    textGrab = y >= sy && y <= sy + thumb ? y - sy : thumb / 2;
                    pressed = -1;
                    keyboardFocus = false;
                    SetFocus(control);
                    SetCapture(hwnd);
                    textScrollTo(control, info.fromPosition(y - r.top - 10 - textGrab, track));
                    return 0;
                }
            }
        if (scrollMax > 0 && x >= view.right + 1 && x <= view.right + 15 && y >= view.top &&
            y <= view.bottom) {
            dragScroll = true;
            dragY = y;
            dragOffset = scroll;
            SetCapture(hwnd);
            float h = view.bottom - view.top, th = std::max(32.f, h * h / (h + scrollMax)),
                  sy = view.top + (h - th) * scroll / scrollMax;
            if (y < sy || y > sy + th) {
                scroll = std::clamp((y - view.top - th / 2) / (h - th) * scrollMax, 0.f, scrollMax);
                dragOffset = scroll;
            }
            invalidate();
            return 0;
        }
        pressed = -1;
        for (size_t i = 0; i < hits.size(); ++i)
            if (hits[i].enabled && contains(hits[i].r, x, y))
                pressed = (int)i;
        if (popupSelected && !contains(popupBounds, x, y)) {
            dismissPopup();
            pressed = -1;
            SetFocus(hwnd);
            return 0;
        }
        keyboardFocus = false;
        focus = pressed;
        SetFocus(hwnd);
        SetCapture(hwnd);
        invalidate();
        return 0;
    }
    case WM_LBUTTONUP: {
        bool wasDragging = draggingText || dragScroll;
        draggingText = nullptr;
        dragScroll = false;
        ReleaseCapture();
        if (wasDragging) {
            invalidate();
            return 0;
        }
        int index = pressed;
        pressed = -1;
        float x = GET_X_LPARAM(l) * 96.f / dpi, y = GET_Y_LPARAM(l) * 96.f / dpi;
        if (index >= 0 && index < (int)hits.size() && contains(hits[index].r, x, y)) {
            lastAction = hits[index].r;
            auto fn = hits[index].fn;
            guard(fn);
        }
        invalidate();
        return 0;
    }
    case WM_MOUSEWHEEL:
        if (popupSelected) {
            popupOffset = std::clamp(popupOffset - GET_WHEEL_DELTA_WPARAM(w) / WHEEL_DELTA * 3, 0,
                                     std::max(0, (int)popupFiltered.size() - 6));
            popupFocus = popupOffset;
            invalidate();
            return 0;
        }
        if (textWheel({GET_X_LPARAM(l), GET_Y_LPARAM(l)}, GET_WHEEL_DELTA_WPARAM(w)))
            return 0;
        scroll = std::clamp(scroll - GET_WHEEL_DELTA_WPARAM(w) * .55f, 0.f, scrollMax);
        invalidate();
        return 0;
    case WM_KEYDOWN:
        if (popupKey(w))
            return 0;
        if (w == VK_TAB) {
            keyboardFocus = true;
            if (focus < 0 || focus + 1 >= (int)hits.size()) {
                for (auto &[id, control] : edits)
                    if (IsWindowVisible(control)) {
                        SetFocus(control);
                        return 0;
                    }
            }
            if (hits.empty())
                return 0;
            int delta = (GetKeyState(VK_SHIFT) & 0x8000) ? -1 : 1;
            for (size_t i = 0; i < hits.size(); ++i) {
                focus = (focus + delta + (int)hits.size()) % (int)hits.size();
                if (hits[focus].enabled)
                    break;
            }
            invalidate();
            return 0;
        }
        if ((w == VK_RETURN || w == VK_SPACE) && focus >= 0 && focus < (int)hits.size()) {
            auto fn = hits[focus].fn;
            guard(fn);
            return 0;
        }
        if (w == VK_NEXT) {
            scroll = std::min(scrollMax, scroll + height * .7f);
            invalidate();
            return 0;
        }
        if (w == VK_PRIOR) {
            scroll = std::max(0.f, scroll - height * .7f);
            invalidate();
            return 0;
        }
        key(w);
        return 0;
    case WM_CAPTURECHANGED:
        draggingText = nullptr;
        dragScroll = false;
        return 0;
    case WM_COMMAND:
        if (HIWORD(w) == EN_VSCROLL) {
            invalidate();
            return 0;
        }
        if (LOWORD(w) == 91 && HIWORD(w) == EN_CHANGE) {
            popupOffset = popupFocus = 0;
            invalidate();
            return 0;
        }
        if (HIWORD(w) == EN_CHANGE)
            changed(LOWORD(w));
        return 0;
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORSTATIC:
        SetTextColor((HDC)w, RGB(24, 43, 70));
        SetBkColor((HDC)w, RGB(255, 255, 255));
        return (LRESULT)GetStockObject(WHITE_BRUSH);
    case WM_TIMER:
        if (w == 8) {
            invalidate();
        } else if (w == 3) {
            KillTimer(hwnd, 3);
            invalidate();
        } else if (w == 7) {
            KillTimer(hwnd, 7);
            changed(-3);
        } else if (w == 6) {
            KillTimer(hwnd, 6);
            changed(-2);
        } else if (w == 4) {
            KillTimer(hwnd, 4);
            changed(-1);
        }
        return 0;
    case WM_APP + 2: {
        std::unique_ptr<JobResult> r((JobResult *)l);
        if (worker.joinable())
            worker.join();
        KillTimer(hwnd, 8);
        if (closing) {
            DestroyWindow(hwnd);
            return 0;
        }
        if (!r->error.empty())
            error(r->error);
        else
            guard([&] { r->done(std::move(r->data)); });
        invalidate();
        return 0;
    }
    case WM_APP + 3:
        backgroundReady();
        return 0;
    case WM_NCDESTROY: {
        auto handle = hwnd;
        SetWindowLongPtrW(handle, GWLP_USERDATA, 0);
        hwnd = nullptr;
        return DefWindowProcW(handle, msg, w, l);
    }
    case WM_CLOSE:
        close();
        return 0;
    case WM_DESTROY: {
        if (modal && owner && IsWindow(owner)) {
            EnableWindow(owner, TRUE);
            SetActiveWindow(owner);
        }
        if (onClose)
            onClose();
        if (!owner)
            PostQuitMessage(0);
        return 0;
    }
    }
    return DefWindowProcW(hwnd, msg, w, l);
}

void Form::initModelRows() {
    if (modelRowsReady)
        return;
    modelRowsReady = true;
    for (auto &field : fields)
        if (field.modelTable) {
            std::istringstream input(field.initial);
            std::string line;
            while (std::getline(input, line))
                if (!trim(line).empty())
                    modelRows.push_back({nextModelControl++, trim(line)});
        }
    if (modelRows.empty())
        modelRows.push_back({nextModelControl++, ""});
}
void Form::drawModelTable(Rect r) {
    initModelRows();
    const int pageSize = 3;
    modelTablePage = std::clamp(modelTablePage, 0, std::max(0, ((int)modelRows.size() - 1) / pageSize));
    int first = modelTablePage * pageSize, last = std::min(first + pageSize, (int)modelRows.size());
    for (int i = 0; i < (int)modelRows.size(); ++i)
        if ((i < first || i >= last) && edits.count(modelRows[i].control))
            ShowWindow(edits[modelRows[i].control], SW_HIDE);
    rect(r, WHITE, 10, LINE);
    rect(box(r.left + 1, r.top + 1, r.right - r.left - 2, 28), 0xF3F7FD, 8);
    text("默认", box(r.left + 12, r.top, 42, 28), 11, MUTED);
    text("模型 ID", box(r.left + 62, r.top, r.right - r.left - 140, 28), 11, MUTED);
    text("操作", box(r.right - 58, r.top, 48, 28), 11, MUTED);
    auto selected = get("model");
    for (int i = first; i < last; ++i) {
        auto &row = modelRows[i];
        float y = r.top + 30 + (i - first) * 34;
        auto current = edits.count(row.control) ? value(row.control) : row.value;
        bool isDefault = !trim(current).empty() && (current == selected || (selected.empty() && i == 0));
        rect(box(r.left + 22, y + 9, 14, 14), WHITE, 7, 0xBDD0E7);
        if (isDefault)
            rect(box(r.left + 26, y + 13, 6, 6), BLUE, 3);
        hit(box(r.left + 8, y, 44, 32), [this, i] {
            auto &row = modelRows[i];
            auto v = trim(edits.count(row.control) ? value(row.control) : row.value);
            if (!v.empty()) {
                set("model", v);
                dirty = true;
            }
        });
        edit(row.control, box(r.left + 58, y, r.right - r.left - 126, 30), row.value, false, false, false,
             "输入模型 ID");
        if (pendingModelFocus == row.control) {
            SetFocus(edits[row.control]);
            pendingModelFocus = 0;
        }
        button(
            "", box(r.right - 51, y + 1, 32, 28),
            [this, i] {
                auto row = modelRows[i];
                auto old = edits.count(row.control) ? value(row.control) : row.value;
                removeEdit(row.control);
                modelRows.erase(modelRows.begin() + i);
                if (get("model") == old)
                    set("model", "");
                dirty = true;
                invalidate();
            },
            3, "close");
    }
    float footer = r.bottom - 31;
    button(
        "添加模型", box(r.left + 8, footer, r.right - r.left > 400 ? 128.f : 112.f, 27),
        [this] {
            initModelRows();
            if (modelRows.size() >= 2000)
                throw std::runtime_error("最多添加 2000 个模型");
            modelRows.push_back({nextModelControl++, ""});
            modelTablePage = ((int)modelRows.size() - 1) / 3;
            pendingModelFocus = modelRows.back().control;
            dirty = true;
            invalidate();
        },
        3, "add");
    if (modelRows.size() > 3) {
        button(
            "", box(r.right - 116, footer, 30, 27),
            [this] {
                --modelTablePage;
                invalidate();
            },
            3, "prev", modelTablePage > 0);
        text(std::to_string(modelTablePage + 1) + " / " + std::to_string(((int)modelRows.size() + 2) / 3),
             box(r.right - 84, footer, 44, 27), 10, MUTED, false, false, 1);
        button(
            "", box(r.right - 38, footer, 30, 27),
            [this] {
                ++modelTablePage;
                invalidate();
            },
            3, "next", last < (int)modelRows.size());
    }
}
std::string Form::get(const std::string &key) {
    if (key == "models" && std::any_of(fields.begin(), fields.end(), [](auto &f) { return f.modelTable; })) {
        initModelRows();
        std::string result;
        for (auto &row : modelRows) {
            auto v = trim(edits.count(row.control) ? value(row.control) : row.value);
            if (!v.empty())
                result += v + "\n";
        }
        return result;
    }
    for (size_t i = 0; i < fields.size(); ++i)
        if (fields[i].key == key) {
            if (fields[i].checkbox)
                return fields[i].initial;
            if (fields[i].options.empty())
                return edits.count(200 + (int)i) ? value(200 + (int)i) : fields[i].initial;
            return fields[i].initial;
        }
    return {};
}
void Form::set(const std::string &key, const std::string &v) {
    for (size_t i = 0; i < fields.size(); ++i)
        if (fields[i].key == key) {
            fields[i].initial = v;
            if (fields[i].modelTable) {
                for (auto &row : modelRows)
                    removeEdit(row.control);
                modelRows.clear();
                modelRowsReady = false;
                modelTablePage = 0;
                initModelRows();
            }
            if (edits.count(200 + (int)i))
                value(200 + (int)i, v);
        }
    invalidate();
}
void Form::draw() {
    caption();
    float y = 76;
    if (!brandName.empty()) {
        brand(brandName, box(28, y, 36, 36));
        text(subtitle, box(80, y - 2, width - 110, 44), 12, BODY, false, true);
        y += 48;
    } else if (!subtitle.empty()) {
        text(subtitle, box(28, y, width - 56, 42), 12, BODY, false, true);
        y += 56;
    }
    if (!sections.empty()) {
        for (size_t i = 0; i < sections.size(); ++i)
            button(
                sections[i], box(28 + i * 126.f, y - 4, 116, 32),
                [this, i] {
                    section = (int)i;
                    invalidate();
                },
                section == (int)i ? 2 : 3);
        y += 42;
    }
    float footer = height - 118;
    if (importChoices) {
        auto select = [this](bool folder) {
            auto chosen = choose(folder);
            if (!chosen.empty() && onChange)
                onChange(*this, pathstr(chosen));
        };
        float tileWidth = (width - 72) / 2;
        rect(box(28, y, tileWidth, 112), WHITE, 14, LINE);
        rect(box(44 + tileWidth, y, tileWidth, 112), WHITE, 14, LINE);
        icon("folder", 48, y + 18, 26, BLUE);
        icon("skills", 64 + tileWidth, y + 18, 26, BLUE);
        button("选择完整文件夹", box(44, y + 62, tileWidth - 32, 34), [select] { select(true); }, 2);
        button(
            "选择 SKILL.md", box(60 + tileWidth, y + 62, tileWidth - 32, 34), [select] { select(false); }, 0);
    } else if (!fields.empty())
        rect(box(16, y - 12, width - 32, footer - y + 12), WHITE, 14, LINE);
    y += 6;
    float start = y, rowHeight = 0;
    int col = 0;
    for (size_t i = 0; i < fields.size(); ++i) {
        auto &f = fields[i];
        if (f.section == -1 || (!sections.empty() && f.section != section)) {
            if (edits.count(200 + (int)i))
                ShowWindow(edits[200 + (int)i], SW_HIDE);
            if (f.modelTable)
                for (auto &row : modelRows)
                    if (edits.count(row.control))
                        ShowWindow(edits[row.control], SW_HIDE);
            continue;
        }
        bool grid = columns == 2;
        bool full = f.multi || f.fullWidth || !grid;
        if (grid && full && col) {
            y += rowHeight + 16;
            col = 0;
            rowHeight = 0;
        }
        float fw = grid && !full ? (width - 76) / 2 : width - 56;
        float fx = 28 + (col ? fw + 20 : 0);
        float fy = y;
        if (f.checkbox) {
            bool checked = f.initial == "true";
            rect(box(fx, fy + 5, 20, 20), checked ? BLUE : WHITE, 5, checked ? BLUE : LINE);
            if (checked) {
                line(fx + 5, fy + 15, fx + 9, fy + 19, WHITE, 2);
                line(fx + 9, fy + 19, fx + 16, fy + 10, WHITE, 2);
            }
            text(f.label, box(fx + 32, fy, fw - 32, 30), 13, INK, true);
            text(f.placeholder, box(fx + 32, fy + 30, fw - 32, 35), 11, MUTED, false, true);
            hit(box(fx, fy, fw, 64), [this, i] {
                fields[i].initial = fields[i].initial == "true" ? "false" : "true";
                dirty = true;
                if (onChange)
                    onChange(*this, fields[i].key);
                invalidate();
            });
            y += 76;
            continue;
        }
        Rect r;
        if (grid || f.multi) {
            text(f.label, box(fx, fy, fw, 22), 12, BODY, true);
            fy += 24;
            r = box(fx, fy, fw,
                    f.multi ? (f.height > 36 ? f.height : std::max(72.f, footer - fy - 16)) : f.height);
        } else {
            text(f.label, box(fx, fy, 118, f.height), 12, BODY);
            r = box(fx + 126, fy, fw - 126, f.height);
        }
        if (f.modelTable)
            drawModelTable(r);
        else if (f.options.empty()) {
            if (f.directory && !f.readonly) {
                auto bounds = r;
                r.right -= 46;
                button(
                    "", box(bounds.right - 38, bounds.top, 38, bounds.bottom - bounds.top),
                    [this, i] {
                        auto selected = choose(true);
                        if (!selected.empty()) {
                            set(fields[i].key, display_path(selected));
                            dirty = true;
                            if (onChange)
                                onChange(*this, fields[i].key);
                        }
                    },
                    0, "folder");
            }
            edit(200 + (int)i, r, f.initial, f.multi, f.secret, f.readonly, f.placeholder);
        } else {
            std::string name = f.initial;
            for (auto &[v, n] : f.options)
                if (v == f.initial)
                    name = n;
            button(
                name, r,
                [this, i, r] {
                    std::vector<Choice> options;
                    for (auto &[v, n] : fields[i].options)
                        options.push_back({v, n, "", ""});
                    popup(
                        std::move(options),
                        [this, i](std::string v) {
                            if (fields[i].initial == v)
                                return;
                            fields[i].initial = std::move(v);
                            dirty = true;
                            auto key = fields[i].key;
                            if (onChange)
                                onChange(*this, key);
                            invalidate();
                        },
                        r);
                },
                5);
            icon("down", r.right - 27, r.top + (r.bottom - r.top - 16) / 2, 16, MUTED);
        }
        float consumed = r.bottom - y;
        if (grid && !full && !col) {
            col = 1;
            rowHeight = consumed;
        } else {
            y += std::max(rowHeight, consumed) + 12;
            col = 0;
            rowHeight = 0;
        }
    }
    if (!help.empty())
        text(help, box(28, height - 108, width - 56, 38), 11, MUTED, false, true);
    rect(box(0, height - 66, width, 66), WHITE);
    line(0, height - 66, width, height - 66, LINE, 1);
    float x = width - 24;
    for (auto it = actions.rbegin(); it != actions.rend(); ++it) {
        float w = std::max(88.f, 32.f + wide(it->label).size() * 12.f);
        x -= w;
        auto fn = it->fn;
        button(
            it->label, box(x, height - 50, w, 34),
            [this, fn] {
                if (fn)
                    fn(*this);
            },
            it->style, "", it->enabled && !busy());
        x -= 8;
    }
    ready = true;
}
void Form::changed(int id) {
    for (auto &row : modelRows)
        if (row.control == id) {
            auto next = value(id);
            if (get("model") == row.value)
                set("model", next);
            row.value = next;
            dirty = true;
            invalidate();
            return;
        }
    if (!ready)
        return;
    if (id >= 200 && id < 200 + (int)fields.size() && fields[id - 200].readonly)
        return;
    dirty = true;
    if (id >= 200 && id < 200 + (int)fields.size()) {
        if (onChange)
            onChange(*this, fields[id - 200].key);
    }
    invalidate();
}
void Form::key(WPARAM w) {
    if (w == VK_ESCAPE)
        close();
    else if (w == 'S' && (GetKeyState(VK_CONTROL) & 0x8000) && !actions.empty() && !busy()) {
        auto fn = actions.back().fn;
        guard([&] { fn(*this); });
    }
}
bool Form::beforeClose() {
    if (canClose && !canClose(*this))
        return false;
    return true;
}
} // namespace ad
