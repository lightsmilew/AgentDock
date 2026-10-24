#include "ui.hpp"
#include <commctrl.h>
#include <shellapi.h>
using namespace ad;
int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    INITCOMMONCONTROLSEX controls{sizeof controls, ICC_STANDARD_CLASSES};
    InitCommonControlsEx(&controls);
    HANDLE mutex = nullptr, showEvent = nullptr;
    try {
        int argc = 0;
        auto argv = CommandLineToArgvW(GetCommandLineW(), &argc);
        auto root = path(env("LOCALAPPDATA")) / "AgentDock";
        std::string page;
        for (int i = 1; i < argc; ++i) {
            std::wstring arg = argv[i];
            if (arg == L"--data-dir" && i + 1 < argc)
                root = argv[++i];
            else if (arg == L"--page" && i + 1 < argc)
                page = utf8(argv[++i]);
        }
        LocalFree(argv);
        root = fs::absolute(root).lexically_normal();
        auto key = root.wstring();
        std::transform(key.begin(), key.end(), key.begin(), towlower);
        auto mutexName = wide("Local\\AgentDockNative_" + sha(utf8(key)).substr(0, 24));
        mutex = CreateMutexW(nullptr, FALSE, mutexName.c_str());
        if (!mutex)
            throw std::runtime_error("无法初始化单实例管理");
        bool already = GetLastError() == ERROR_ALREADY_EXISTS;
        showEvent = CreateEventW(nullptr, FALSE, FALSE, (mutexName + L"_Show").c_str());
        if (!showEvent)
            throw std::runtime_error("无法创建窗口唤醒事件");
        if (already) {
            SetEvent(showEvent);
            CloseHandle(showEvent);
            showEvent = nullptr;
            CloseHandle(mutex);
            if (SUCCEEDED(com))
                CoUninitialize();
            return 0;
        }
        {
            App app(root);
            app.start();
            MSG msg;
            bool quitting = false;
            while (!quitting) {
                DWORD result =
                    MsgWaitForMultipleObjectsEx(1, &showEvent, INFINITE, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
                if (result == WAIT_OBJECT_0) {
                    ShowWindow(app.hwnd, IsIconic(app.hwnd) ? SW_RESTORE : SW_SHOW);
                    SetForegroundWindow(app.hwnd);
                }
                while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
                    if (msg.message == WM_QUIT) {
                        quitting = true;
                        break;
                    }
                    TranslateMessage(&msg);
                    DispatchMessageW(&msg);
                    app.collect();
                }
            }
        }
        CloseHandle(mutex);
        CloseHandle(showEvent);
        showEvent = nullptr;
        if (SUCCEEDED(com))
            CoUninitialize();
        return 0;
    } catch (const std::exception &e) {
        MessageBoxW(nullptr, wide(e.what()).c_str(), L"AgentDock 启动失败", MB_OK | MB_ICONERROR);
        if (mutex)
            CloseHandle(mutex);
        if (showEvent)
            CloseHandle(showEvent);
        showEvent = nullptr;
        if (SUCCEEDED(com))
            CoUninitialize();
        return 1;
    }
}
