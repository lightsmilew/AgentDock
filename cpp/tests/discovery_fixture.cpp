#include <filesystem>
#include <fstream>
#include <windows.h>
int wmain(int argc, wchar_t **argv) {
    if (argc == 1)
        return 0;
    // Integration fixture only: never acts as a system package manager.
    wchar_t marker[32768]{}, own[32768]{};
    if (!GetEnvironmentVariableW(L"AGENTDOCK_DESKTOP_FIXTURE_ROOT", marker, 32768))
        return 81;
    std::filesystem::path target;
    bool cursor = false;
    for (int i = 1; i < argc; ++i) {
        if (std::wstring(argv[i]) == L"--location" && i + 1 < argc)
            target = argv[++i];
        if (std::wstring(argv[i]) == L"--id" && i + 1 < argc)
            cursor = std::wstring(argv[++i]) == L"Anysphere.Cursor";
    }
    auto root = std::filesystem::path(marker).lexically_normal().make_preferred();
    target = target.lexically_normal().make_preferred();
    auto a = target.wstring(), b = root.wstring();
    if (!cursor || a.size() <= b.size() || a.compare(0, b.size(), b) != 0 || a[b.size()] != L'\\')
        return 82;
    GetModuleFileNameW(nullptr, own, 32768);
    std::filesystem::create_directories(target / L"resources/app");
    std::filesystem::copy_file(own, target / L"Cursor.exe",
                               std::filesystem::copy_options::overwrite_existing);
    std::ofstream(target / L"resources/app/product.json")
        << R"({"nameShort":"Cursor","applicationName":"cursor"})";
    return 0;
}
