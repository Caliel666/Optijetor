#include <windows.h>
#include <string>
#include <fstream>
#include <mutex>
#include <cwchar>
#include <vector>

extern "C" IMAGE_DOS_HEADER __ImageBase;

static HMODULE g_systemVersion = nullptr;
static std::wstring g_optiscalerPath;
static DWORD g_delayMs = 0;
static bool g_logging = true;
static std::wstring g_root;
static std::wstring g_gameRoot;
static std::mutex g_logMutex;
static INIT_ONCE g_resolveOnce = INIT_ONCE_STATIC_INIT;

static std::wstring ParentDirectory(const std::wstring& path)
{
    const auto slash = path.find_last_of(L"\\/");
    return slash == std::wstring::npos ? L"." : path.substr(0, slash);
}

static std::wstring ModuleDirectory()
{
    wchar_t path[32768]{};
    GetModuleFileNameW(reinterpret_cast<HMODULE>(&__ImageBase), path, 32768);
    return ParentDirectory(path);
}

static std::wstring GameDirectory()
{
    wchar_t path[32768]{};
    GetModuleFileNameW(nullptr, path, 32768);
    return ParentDirectory(path);
}

static std::wstring FullPath(const std::wstring& path)
{
    wchar_t buffer[32768]{};
    const DWORD n = GetFullPathNameW(path.c_str(), 32768, buffer, nullptr);
    return (n && n < 32768) ? std::wstring(buffer) : path;
}

static bool SamePath(const std::wstring& a, const std::wstring& b)
{
    return CompareStringOrdinal(FullPath(a).c_str(), -1, FullPath(b).c_str(), -1, TRUE) == CSTR_EQUAL;
}

static void Log(const std::wstring& message)
{
    if (!g_logging) return;
    std::lock_guard<std::mutex> lock(g_logMutex);
    std::wofstream out(g_root + L"\\OpenNR.log", std::ios::app);
    if (out) out << L"[Optijetor] " << message << L"\n";
}

static void InitializeConfig()
{
    g_root = ModuleDirectory();
    g_gameRoot = GameDirectory();
    const std::wstring ini = g_root + L"\\OpenNR.ini";
    wchar_t value[32768]{};
    GetPrivateProfileStringW(L"OpenNR", L"OptiScalerPath", L"", value, 32768, ini.c_str());
    g_optiscalerPath = value;
    g_delayMs = GetPrivateProfileIntW(L"OpenNR", L"LoadDelayMs", 0, ini.c_str());
    if (g_delayMs > 30000) g_delayMs = 30000;
    g_logging = GetPrivateProfileIntW(L"OpenNR", L"Logging", 1, ini.c_str()) != 0;
}

static BOOL CALLBACK InitializeSystemVersion(PINIT_ONCE, PVOID, PVOID*)
{
    wchar_t systemDir[MAX_PATH]{};
    const UINT len = GetSystemDirectoryW(systemDir, MAX_PATH);
    if (len && len < MAX_PATH) {
        std::wstring path(systemDir);
        path += L"\\version.dll";
        g_systemVersion = LoadLibraryW(path.c_str());
    }
    return TRUE;
}

static FARPROC Resolve(const char* name)
{
    InitOnceExecuteOnce(&g_resolveOnce, InitializeSystemVersion, nullptr, nullptr);
    return g_systemVersion ? GetProcAddress(g_systemVersion, name) : nullptr;
}

// OptiScaler often probes companion runtimes by looking in the game's root.
// Expose the external install's files there as filesystem links (no byte copies).
// Existing game files are never overwritten, and version.dll is never bridged.
static void BridgeCompanionFiles()
{
    const std::wstring sourceRoot = ParentDirectory(FullPath(g_optiscalerPath));
    if (sourceRoot.empty() || SamePath(sourceRoot, g_gameRoot)) {
        Log(L"Bridge skipped: OptiScaler is already in the game directory.");
        return;
    }

    Log(L"Recursive companion scan: " + sourceRoot);
    WIN32_FIND_DATAW data{};
    std::vector<std::wstring> pending{ sourceRoot };
    unsigned scanned = 0, linked = 0, skipped = 0, failed = 0;

    while (!pending.empty()) {
        const std::wstring dir = pending.back();
        pending.pop_back();
        const std::wstring pattern = dir + L"\\*";
        HANDLE find = FindFirstFileW(pattern.c_str(), &data);
        if (find == INVALID_HANDLE_VALUE) {
            ++failed;
            Log(L"Cannot enumerate directory: " + dir + L" (error " + std::to_wstring(GetLastError()) + L")");
            continue;
        }
        do {
            if (wcscmp(data.cFileName, L".") == 0 || wcscmp(data.cFileName, L"..") == 0)
                continue;
            const std::wstring src = dir + L"\\" + data.cFileName;
            if (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                // Avoid following reparse points and directory symlinks outside the configured tree.
                if (!(data.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT))
                    pending.push_back(src);
                continue;
            }
            ++scanned;
            if (SamePath(src, g_optiscalerPath) ||
                CompareStringOrdinal(data.cFileName, -1, L"version.dll", -1, TRUE) == CSTR_EQUAL ||
                CompareStringOrdinal(data.cFileName, -1, L"OpenNR.ini", -1, TRUE) == CSTR_EQUAL) {
                ++skipped;
                continue;
            }

            const std::wstring dst = g_gameRoot + L"\\" + data.cFileName;
            const DWORD existing = GetFileAttributesW(dst.c_str());
            if (existing != INVALID_FILE_ATTRIBUTES) {
                ++skipped;
                continue;
            }

            // Hard links are preferred (zero additional file data); symbolic links
            // are attempted for cross-volume installs or filesystems that reject hard links.
            if (CreateHardLinkW(dst.c_str(), src.c_str(), nullptr)) {
                ++linked;
                Log(L"Hard-link bridge: " + dst + L" -> " + src);
            } else {
                const DWORD hardLinkError = GetLastError();
                if (CreateSymbolicLinkW(dst.c_str(), src.c_str(), SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE)) {
                    ++linked;
                    Log(L"Symbolic-link bridge: " + dst + L" -> " + src);
                } else {
                    const DWORD linkError = GetLastError();
                    ++failed;
                    Log(L"Bridge failed for " + src + L" (hardlink " +
                        std::to_wstring(hardLinkError) + L", symlink " +
                        std::to_wstring(linkError) + L")");
                }
            }
        } while (FindNextFileW(find, &data));
        FindClose(find);
    }

    Log(L"Recursive bridge scan complete. Files scanned=" + std::to_wstring(scanned) +
        L", links created=" + std::to_wstring(linked) +
        L", skipped=" + std::to_wstring(skipped) +
        L", failures=" + std::to_wstring(failed));
}

static DWORD WINAPI LoadOptiScalerThread(void*)
{
    InitializeConfig();
    Log(L"Proxy initialized. Game directory: " + g_gameRoot);
    if (g_delayMs) Sleep(g_delayMs);

    if (g_optiscalerPath.empty()) {
        Log(L"OptiScalerPath is empty; set it in OpenNR.ini to the separate OptiScaler version.dll.");
        return 0;
    }

    g_optiscalerPath = FullPath(g_optiscalerPath);
    const DWORD attrs = GetFileAttributesW(g_optiscalerPath.c_str());
    if (attrs == INVALID_FILE_ATTRIBUTES || (attrs & FILE_ATTRIBUTE_DIRECTORY)) {
        Log(L"Configured OptiScalerPath does not exist or is a directory: " + g_optiscalerPath);
        return 0;
    }

    BridgeCompanionFiles();

    // Put the game root first in the legacy DLL search order, then add the real
    // OptiScaler directory for APIs that honor AddDllDirectory.
    SetDllDirectoryW(g_gameRoot.c_str());
    DLL_DIRECTORY_COOKIE cookie = AddDllDirectory(ParentDirectory(g_optiscalerPath).c_str());
    Log(L"Attempting direct OptiScaler load from: " + g_optiscalerPath);
    HMODULE loaded = LoadLibraryExW(g_optiscalerPath.c_str(), nullptr,
        LOAD_WITH_ALTERED_SEARCH_PATH);

    if (loaded) {
        Log(L"Loaded OptiScaler directly. Companion links are in the game root; module's real path remains " + g_optiscalerPath);
    } else {
        const DWORD error = GetLastError();
        Log(L"Loading OptiScaler failed (Win32 error " + std::to_wstring(error) + L"): " + g_optiscalerPath);
    }
    // Keep the added DLL directory active for the lifetime of the process.
    (void)cookie;
    return 0;
}

#define FORWARD(ret, exportName, params, args) \
extern "C" ret WINAPI Proxy_##exportName params { \
    using Fn = ret (WINAPI*) params; \
    static Fn fn = reinterpret_cast<Fn>(Resolve(#exportName)); \
    if (!fn) { SetLastError(ERROR_PROC_NOT_FOUND); return static_cast<ret>(0); } \
    return fn args; \
}

FORWARD(BOOL, GetFileVersionInfoA, (LPCSTR a, DWORD b, DWORD c, LPVOID d), (a,b,c,d))
FORWARD(BOOL, GetFileVersionInfoW, (LPCWSTR a, DWORD b, DWORD c, LPVOID d), (a,b,c,d))
FORWARD(BOOL, GetFileVersionInfoExA, (DWORD a, LPCSTR b, DWORD c, DWORD d, LPVOID e), (a,b,c,d,e))
FORWARD(BOOL, GetFileVersionInfoExW, (DWORD a, LPCWSTR b, DWORD c, DWORD d, LPVOID e), (a,b,c,d,e))
FORWARD(DWORD, GetFileVersionInfoSizeA, (LPCSTR a, LPDWORD b), (a,b))
FORWARD(DWORD, GetFileVersionInfoSizeW, (LPCWSTR a, LPDWORD b), (a,b))
FORWARD(DWORD, GetFileVersionInfoSizeExA, (DWORD a, LPCSTR b, LPDWORD c), (a,b,c))
FORWARD(DWORD, GetFileVersionInfoSizeExW, (DWORD a, LPCWSTR b, LPDWORD c), (a,b,c))
FORWARD(DWORD, VerLanguageNameA, (DWORD a, LPSTR b, DWORD c), (a,b,c))
FORWARD(DWORD, VerLanguageNameW, (DWORD a, LPWSTR b, DWORD c), (a,b,c))
FORWARD(DWORD, VerFindFileA, (DWORD a, LPCSTR b, LPCSTR c, LPCSTR d, LPSTR e, PUINT f, LPSTR g, PUINT h), (a,b,c,d,e,f,g,h))
FORWARD(DWORD, VerFindFileW, (DWORD a, LPCWSTR b, LPCWSTR c, LPCWSTR d, LPWSTR e, PUINT f, LPWSTR g, PUINT h), (a,b,c,d,e,f,g,h))
FORWARD(DWORD, VerInstallFileA, (DWORD a, LPCSTR b, LPCSTR c, LPCSTR d, LPCSTR e, LPCSTR f, LPSTR g, PUINT h), (a,b,c,d,e,f,g,h))
FORWARD(DWORD, VerInstallFileW, (DWORD a, LPCWSTR b, LPCWSTR c, LPCWSTR d, LPCWSTR e, LPCWSTR f, LPWSTR g, PUINT h), (a,b,c,d,e,f,g,h))
FORWARD(BOOL, VerQueryValueA, (LPCVOID a, LPCSTR b, LPVOID* c, PUINT d), (a,b,c,d))
FORWARD(BOOL, VerQueryValueW, (LPCVOID a, LPCWSTR b, LPVOID* c, PUINT d), (a,b,c,d))

BOOL WINAPI DllMain(HINSTANCE, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(reinterpret_cast<HMODULE>(&__ImageBase));
        HANDLE thread = CreateThread(nullptr, 0, LoadOptiScalerThread, nullptr, 0, nullptr);
        if (thread) CloseHandle(thread);
    } else if (reason == DLL_PROCESS_DETACH) {
        if (g_systemVersion) FreeLibrary(g_systemVersion);
    }
    return TRUE;
}
