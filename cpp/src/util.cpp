#include "core.hpp"
#include <algorithm>
#include <bcrypt.h>
#include <chrono>
#include <fstream>
#include <iomanip>
#include <memory>
#include <shlobj.h>
#include <sstream>
#include <wincrypt.h>
#include <winver.h>

namespace ad {
std::wstring wide(const std::string &s) {
    if (s.empty())
        return {};
    int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.data(), (int)s.size(), nullptr, 0);
    if (!n)
        throw std::runtime_error("文本不是有效 UTF-8");
    std::wstring r(n, 0);
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.data(), (int)s.size(), r.data(), n);
    return r;
}
std::string utf8(const std::wstring &s) {
    if (s.empty())
        return {};
    int n = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, s.data(), (int)s.size(), nullptr, 0, nullptr,
                                nullptr);
    if (!n)
        throw std::runtime_error("无效的 Unicode 文本");
    std::string r(n, 0);
    WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, s.data(), (int)s.size(), r.data(), n, nullptr,
                        nullptr);
    return r;
}
std::string pathstr(const fs::path &p) {
    return utf8(p.wstring());
}
std::string display_path(fs::path p) {
    p.make_preferred();
    return utf8(p.wstring());
}
fs::path path(const std::string &p) {
    return fs::path(wide(p));
}
std::string trim(std::string s) {
    auto a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos)
        return {};
    return s.substr(a, s.find_last_not_of(" \t\r\n") - a + 1);
}
std::string env(const std::string &key) {
    auto k = wide(key);
    DWORD n = GetEnvironmentVariableW(k.c_str(), nullptr, 0);
    if (!n)
        return {};
    std::wstring v(n, 0);
    GetEnvironmentVariableW(k.c_str(), v.data(), n);
    v.resize(n - 1);
    return utf8(v);
}
std::string uuid() {
    GUID g{};
    if (FAILED(CoCreateGuid(&g)))
        throw std::runtime_error("无法创建 ID");
    char b[33];
    auto p = (unsigned char *)&g;
    for (int i = 0; i < 16; ++i)
        sprintf_s(b + i * 2, 3, "%02x", p[i]);
    return b;
}
double now() {
    return std::chrono::duration<double>(std::chrono::system_clock::now().time_since_epoch()).count();
}
bool allowed_agent_path(const fs::path &p) {
    if (p.empty())
        return false;
    if (env("AGENTDOCK_TEST_SCOPES") == "1")
        return true;
    auto absolute = fs::absolute(p).lexically_normal();
    for (auto &component : absolute) {
        auto name = component.wstring();
        std::transform(name.begin(), name.end(), name.begin(), towlower);
        if (name.starts_with(L"agentdock-native-test-"))
            return false;
    }
    auto extension = absolute.extension().wstring();
    std::transform(extension.begin(), extension.end(), extension.begin(), towlower);
    if (extension == L".cmd" || extension == L".bat" || extension == L".ps1") {
        std::ifstream input(absolute, std::ios::binary);
        char prefix[4096]{};
        input.read(prefix, sizeof prefix);
        if (std::string_view(prefix, (size_t)input.gcount()).find("AGENTDOCK_SYNTHETIC_TEST_FIXTURE") !=
            std::string_view::npos)
            return false;
    }
    if (extension != L".exe")
        return true;
    DWORD unused = 0, size = GetFileVersionInfoSizeW(absolute.c_str(), &unused);
    if (!size || size > 1024 * 1024)
        return true;
    std::vector<BYTE> data(size);
    if (!GetFileVersionInfoW(absolute.c_str(), 0, size, data.data()))
        return true;
    struct Language {
        WORD language, codepage;
    } *languages = nullptr;
    UINT length = 0;
    if (!VerQueryValueW(data.data(), L"\\VarFileInfo\\Translation", (void **)&languages, &length))
        return true;
    for (UINT i = 0; i < length / sizeof(Language); ++i) {
        wchar_t key[120], *description = nullptr;
        UINT chars = 0;
        swprintf_s(key, L"\\StringFileInfo\\%04x%04x\\FileDescription", languages[i].language,
                   languages[i].codepage);
        if (VerQueryValueW(data.data(), key, (void **)&description, &chars) && description &&
            std::wstring(description).starts_with(L"AgentDock detection test fixture"))
            return false;
    }
    return true;
}
void check_path(const fs::path &p) {
    fs::path abs = fs::absolute(p).lexically_normal(), cur;
    for (const auto &c : abs) {
        cur /= c;
        DWORD a = GetFileAttributesW(cur.c_str());
        if (a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_REPARSE_POINT))
            throw std::runtime_error("路径包含链接或目录联接，已停止文件操作");
    }
}
bool inside(const fs::path &child, const fs::path &parent) {
    auto c = fs::absolute(child).lexically_normal().wstring(),
         p = fs::absolute(parent).lexically_normal().wstring();
    std::transform(c.begin(), c.end(), c.begin(), towlower);
    std::transform(p.begin(), p.end(), p.begin(), towlower);
    return c.size() > p.size() && c.compare(0, p.size(), p) == 0 &&
           (c[p.size()] == '\\' || c[p.size()] == '/');
}
std::string read(const fs::path &p, size_t max) {
    check_path(p);
    auto n = fs::file_size(p);
    if (n > max)
        throw std::runtime_error("文件超过大小限制");
    std::ifstream f(p, std::ios::binary);
    if (!f)
        throw std::runtime_error("无法读取文件");
    std::string s((size_t)n, 0);
    if (n && !f.read(s.data(), n))
        throw std::runtime_error("文件读取失败");
    return s;
}
void atomic_write(const fs::path &p, const std::string &s) {
    check_path(p);
    fs::create_directories(p.parent_path());
    auto temp = p;
    temp += L"." + wide(uuid()) + L".tmp";
    HANDLE f =
        CreateFileW(temp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE)
        throw std::runtime_error("无法写入临时文件");
    DWORD n = 0;
    bool ok = WriteFile(f, s.data(), (DWORD)s.size(), &n, nullptr) && n == s.size() && FlushFileBuffers(f);
    CloseHandle(f);
    if (ok)
        ok = MoveFileExW(temp.c_str(), p.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
    if (!ok) {
        DeleteFileW(temp.c_str());
        throw std::runtime_error("保存失败，请检查目录权限和磁盘空间");
    }
}
std::string sha(const std::string &s) {
    BCRYPT_ALG_HANDLE alg = nullptr;
    BCRYPT_HASH_HANDLE h = nullptr;
    unsigned char out[32];
    if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0)
        throw std::runtime_error("SHA256 不可用");
    auto status = BCryptCreateHash(alg, &h, nullptr, 0, nullptr, 0, 0);
    if (status >= 0)
        status = BCryptHashData(h, (PUCHAR)s.data(), (ULONG)s.size(), 0);
    if (status >= 0)
        status = BCryptFinishHash(h, out, 32, 0);
    if (h)
        BCryptDestroyHash(h);
    BCryptCloseAlgorithmProvider(alg, 0);
    if (status < 0)
        throw std::runtime_error("SHA256 计算失败");
    std::string r;
    static const char hex[] = "0123456789abcdef";
    for (auto b : out) {
        r += hex[b >> 4];
        r += hex[b & 15];
    }
    return r;
}
std::string protect(const std::string &s, bool decrypt) {
    std::string raw = s;
    if (decrypt) {
        DWORD n = 0;
        if (!CryptStringToBinaryA(s.c_str(), (DWORD)s.size(), CRYPT_STRING_BASE64 | CRYPT_STRING_STRICT,
                                  nullptr, &n, nullptr, nullptr))
            throw std::runtime_error("密钥密文损坏");
        raw.resize(n);
        if (!CryptStringToBinaryA(s.c_str(), (DWORD)s.size(), CRYPT_STRING_BASE64 | CRYPT_STRING_STRICT,
                                  (BYTE *)raw.data(), &n, nullptr, nullptr))
            throw std::runtime_error("密钥解码失败");
        raw.resize(n);
    }
    DATA_BLOB a{(DWORD)raw.size(), (BYTE *)raw.data()}, b{};
    BOOL ok =
        decrypt
            ? CryptUnprotectData(&a, nullptr, nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &b)
            : CryptProtectData(&a, L"AgentDock", nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &b);
    SecureZeroMemory(raw.data(), raw.size());
    if (!ok)
        throw std::runtime_error("Windows DPAPI 密钥操作失败（请使用原 Windows 用户）");
    std::string r((char *)b.pbData, b.cbData);
    SecureZeroMemory(b.pbData, b.cbData);
    LocalFree(b.pbData);
    if (!decrypt) {
        DWORD n = 0;
        CryptBinaryToStringA((BYTE *)r.data(), (DWORD)r.size(), CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF,
                             nullptr, &n);
        std::string encoded(n, 0);
        CryptBinaryToStringA((BYTE *)r.data(), (DWORD)r.size(), CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF,
                             encoded.data(), &n);
        while (!encoded.empty() && encoded.back() == 0)
            encoded.pop_back();
        SecureZeroMemory(r.data(), r.size());
        return encoded;
    }
    return r;
}
std::string resource(int id) {
    HRSRC r = FindResourceW(nullptr, MAKEINTRESOURCEW(id), RT_RCDATA);
    if (!r)
        throw std::runtime_error("内置资源缺失");
    auto h = LoadResource(nullptr, r);
    return std::string((const char *)LockResource(h), SizeofResource(nullptr, r));
}
J resource_json(int id) {
    return J::parse(resource(id));
}
std::string bytes(uint64_t n) {
    std::ostringstream o;
    if (n < 1024)
        o << n << " B";
    else if (n < 1024 * 1024)
        o << std::fixed << std::setprecision(1) << n / 1024.0 << " KB";
    else
        o << std::fixed << std::setprecision(2) << n / 1048576.0 << " MB";
    return o.str();
}
} // namespace ad
