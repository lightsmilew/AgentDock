#include "official_install.hpp"
#include <algorithm>
#include <fstream>
#include <intrin.h>
#include <winternl.h>

namespace ad {
J windows_platform() {
    static const J cached = [] {
        SYSTEM_INFO system{};
        GetNativeSystemInfo(&system);
        // GetNativeSystemInfo can report emulated x64 on ARM. IsWow64Process2 exposes the real host.
        USHORT processMachine = 0, nativeMachine = 0;
        auto architecture = reinterpret_cast<BOOL(WINAPI *)(HANDLE, USHORT *, USHORT *)>(
            GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "IsWow64Process2"));
        bool queried = architecture && architecture(GetCurrentProcess(), &processMachine, &nativeMachine);
        if (!queried)
            nativeMachine =
                system.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_AMD64   ? IMAGE_FILE_MACHINE_AMD64
                : system.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_ARM64 ? IMAGE_FILE_MACHINE_ARM64
                                                                                : IMAGE_FILE_MACHINE_I386;
        std::string arch = nativeMachine == IMAGE_FILE_MACHINE_AMD64   ? "x64"
                           : nativeMachine == IMAGE_FILE_MACHINE_ARM64 ? "arm64"
                                                                       : "x86";
        RTL_OSVERSIONINFOW os{};
        os.dwOSVersionInfoSize = sizeof os;
        auto query = reinterpret_cast<LONG(WINAPI *)(PRTL_OSVERSIONINFOW)>(
            GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "RtlGetVersion"));
        if (!query || query(&os) != 0)
            throw std::runtime_error("无法读取 Windows 版本，已停止兼容性判断");
        bool avx = false;
#if defined(_M_X64) || defined(_M_IX86)
        if (arch == "x64" || arch == "x86") {
            int cpu[4]{};
            __cpuid(cpu, 1);
            // AVX also needs OSXSAVE and XMM/YMM state enabled by the OS/hypervisor.
            avx = (cpu[2] & (1 << 28)) && (cpu[2] & (1 << 27)) && ((_xgetbv(0) & 6) == 6);
        }
#endif
        return J{{"architecture", arch},
                 {"major", os.dwMajorVersion},
                 {"minor", os.dwMinorVersion},
                 {"architecture_source", queried ? "IsWow64Process2" : "GetNativeSystemInfo"},
                 {"build", os.dwBuildNumber},
                 {"avx", avx}};
    }();
    return cached;
}
namespace {
J failure(std::string code, std::string message, bool repairable = true) {
    return {{"compatible", false}, {"code", code}, {"message", message}, {"repairable", repairable}};
}
std::string lower_extension(const fs::path &p) {
    auto s = pathstr(p.extension());
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return (char)tolower(c); });
    return s;
}
} // namespace
J binary_compatibility(const fs::path &exe, const J &host) {
    std::ifstream input(exe, std::ios::binary | std::ios::ate);
    if (!input)
        return failure("missing", "启动文件已不存在，请重新安装");
    auto size = input.tellg();
    auto get = [&](uint64_t offset, void *out, size_t n) {
        if (size < 0 || offset > (uint64_t)size || n > (uint64_t)size - offset)
            return false;
        input.seekg((std::streamoff)offset);
        return !!input.read(static_cast<char *>(out), n);
    };
    IMAGE_DOS_HEADER dos{};
    DWORD signature = 0;
    IMAGE_FILE_HEADER file{};
    if (!get(0, &dos, sizeof dos) || dos.e_magic != IMAGE_DOS_SIGNATURE || dos.e_lfanew < sizeof dos ||
        dos.e_lfanew > 4 * 1024 * 1024 || !get(dos.e_lfanew, &signature, sizeof signature) ||
        signature != IMAGE_NT_SIGNATURE || !get(dos.e_lfanew + 4, &file, sizeof file))
        return failure("invalid-pe", "启动文件不是有效的 Windows 程序，可能是安装占位文件或下载不完整");
    uint64_t optional = (uint64_t)dos.e_lfanew + 4 + sizeof file;
    WORD magic = 0;
    if (!get(optional, &magic, sizeof magic) || !file.NumberOfSections || file.NumberOfSections > 96 ||
        file.SizeOfOptionalHeader < 70 || !(file.Characteristics & IMAGE_FILE_EXECUTABLE_IMAGE) ||
        (file.Characteristics & IMAGE_FILE_DLL) ||
        (magic != IMAGE_NT_OPTIONAL_HDR32_MAGIC && magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC))
        return failure("invalid-pe", "Windows 启动文件头损坏，请修复安装");
    std::string arch = file.Machine == IMAGE_FILE_MACHINE_AMD64   ? "x64"
                       : file.Machine == IMAGE_FILE_MACHINE_ARM64 ? "arm64"
                       : file.Machine == IMAGE_FILE_MACHINE_I386  ? "x86"
                                                                  : "unknown";
    if ((arch == "x86" && magic != IMAGE_NT_OPTIONAL_HDR32_MAGIC) ||
        (arch != "x86" && magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC))
        return failure("invalid-pe", "启动文件的架构标记不一致，请修复安装");
    auto native = host.value("architecture", "unknown");
    bool supported = native == arch || (arch == "x86" && (native == "x64" || native == "arm64")) ||
                     (native == "arm64" && arch == "x64" && host.value("build", 0) >= 22000);
    if (!supported || arch == "unknown")
        return failure("architecture",
                       "程序架构 " + arch + " 与当前 Windows " + native + " 不兼容，请修复安装");
    WORD subsystemVersion[2]{};
    if (!get(optional + 48, subsystemVersion, sizeof subsystemVersion))
        return failure("invalid-pe", "启动文件不完整，请修复安装");
    if (subsystemVersion[0] > host.value("major", 0) ||
        (subsystemVersion[0] == host.value("major", 0) && subsystemVersion[1] > host.value("minor", 0)))
        return failure("windows-version", "该程序要求更高版本的 Windows，请先更新系统", false);
    // Validate the section table and raw data extents without reading the whole executable.
    uint64_t sections = optional + file.SizeOfOptionalHeader;
    for (unsigned i = 0; i < file.NumberOfSections; ++i) {
        IMAGE_SECTION_HEADER section{};
        if (!get(sections + i * sizeof section, &section, sizeof section) ||
            (section.SizeOfRawData &&
             ((uint64_t)section.PointerToRawData + section.SizeOfRawData > (uint64_t)size)))
            return failure("invalid-pe", "启动文件被截断，请重新下载安装");
    }
    return {{"compatible", true}, {"architecture", arch}, {"repairable", false}, {"message", ""}};
}
J agent_compatibility(const std::string &id, const fs::path &exe, const J &host) {
    if (!exe.empty() && !allowed_agent_path(exe))
        return failure("test-fixture", "此位置是 AgentDock 测试文件，请安装正式版本后重新检测");
    if (id == "claude") {
        if (host.value("major", 0) < 10 || host.value("build", 0) < 17763 ||
            host.value("architecture", "") == "x86")
            return failure("unsupported-host",
                           "Claude Code 需要 Windows 10 1809 / Server 2019 或更新的 64 位系统", false);
        if (host.value("architecture", "") == "x64" && !host.value("avx", false))
            return failure("unsupported-cpu", "当前 CPU 或虚拟机未提供 AVX；Claude 官方暂无兼容的原生版本",
                           false);
    }
    if (exe.empty())
        return {{"compatible", false}, {"repairable", false}, {"message", ""}, {"code", "not-installed"}};
    auto target = npm_agent_target(id, exe);
    auto selected = target.empty() ? exe : target;
    std::error_code error;
    if (!fs::is_regular_file(selected, error))
        return failure("missing", "启动文件或 npm 依赖缺失，请修复安装");
    J result = {{"compatible", true}, {"repairable", false}, {"message", ""}};
    if (lower_extension(selected) == ".exe")
        result = binary_compatibility(selected, host);
    if (id == "claude" && result.value("architecture", "") == "x86")
        return failure("architecture", "Claude Code 不提供 32 位程序，请修复为当前系统的 64 位版本");
    if (id == "claude" && host.value("architecture", "") == "arm64" &&
        result.value("architecture", "") == "x64" && host.value("build", 0) < 26100)
        return failure("architecture",
                       "此 Windows ARM 版本不能提供 Claude x64 所需的指令，请修复为官方 ARM64 版本");
    // npm launchers may be valid while the Node executable they use is wrong/too old.
    if (result.value("compatible", false) && !target.empty() && lower_extension(selected) != ".exe") {
        auto node = exe.parent_path() / "node.exe";
        if (!fs::is_regular_file(node))
            node = find_exe("node");
        result = node_compatibility(id, node, host);
    }
    result["target"] = display_path(selected);
    return result;
}
J node_compatibility(const std::string &id, const fs::path &node, const J &host) {
    if (node.empty())
        return failure("node-missing", "缺少 Node.js，请安装官方受支持的 Node.js 后重试", false);
    auto result = binary_compatibility(node, host);
    if (!result.value("compatible", false))
        return failure("node-architecture", "Node.js 不兼容当前系统，请安装匹配架构的官方 Node.js", false);
    int minimum = id == "claude" || id == "qwen" || id == "copilot" ? 22
                  : id == "gemini" || id == "factory"               ? 20
                  : id == "codex"                                   ? 16
                                                                    : 0;
    DWORD ignored = 0, bytes = GetFileVersionInfoSizeW(node.c_str(), &ignored);
    if (minimum && bytes && bytes <= 1024 * 1024) {
        std::vector<char> data(bytes);
        VS_FIXEDFILEINFO *version = nullptr;
        UINT length = 0;
        if (GetFileVersionInfoW(node.c_str(), 0, bytes, data.data()) &&
            VerQueryValueW(data.data(), L"\\", (void **)&version, &length) && length >= sizeof *version &&
            HIWORD(version->dwFileVersionMS) < minimum)
            return failure("node-version",
                           "此 Agent 需要 Node.js " + std::to_string(minimum) +
                               " 或更新版本，请更新官方运行环境",
                           false);
    }
    return result;
}
J agent_compatibility(const std::string &id, const fs::path &exe) {
    return agent_compatibility(id, exe, windows_platform());
}
} // namespace ad
