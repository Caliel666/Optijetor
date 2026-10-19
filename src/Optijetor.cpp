#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <string>
#include <fstream>
#include <mutex>

extern "C" IMAGE_DOS_HEADER __ImageBase;

static HMODULE g_systemVersion = nullptr;
static HMODULE g_proxyModule = nullptr;
static std::wstring g_optiscalerPath;
static std::wstring g_stagedPath;
static DWORD g_delayMs = 1500;
static bool g_logging = true;
static std::wstring g_root;
static std::mutex g_logMutex;
static INIT_ONCE g_resolveOnce = INIT_ONCE_STATIC_INIT;

static std::wstring ModuleDirectory()
{
    wchar_t path[MAX_PATH]{};
    GetModuleFileNameW(reinterpret_cast<HMODULE>(&__ImageBase), path, MAX_PATH);
    std::wstring value(path);
    const auto slash = value.find_last_of(L"\\/");
    return slash == std::wstring::npos ? L"." : value.substr(0, slash);
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
    const std::wstring ini = g_root + L"\\OpenNR.ini";
    wchar_t value[32768]{};
    GetPrivateProfileStringW(L"OpenNR", L"OptiScalerPath", L"", value,
                             static_cast<DWORD>(std::size(value)), ini.c_str());
    g_optiscalerPath = value;
    g_delayMs = GetPrivateProfileIntW(L"OpenNR", L"LoadDelayMs", 1500, ini.c_str());
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

static DWORD WINAPI LoadOptiScalerThread(void*)
{
    InitializeConfig();
    Log(L"Proxy initialized.");
    if (g_delayMs) Sleep(g_delayMs);

    if (g_optiscalerPath.empty()) {
        Log(L"OptiScalerPath is empty; set it in OpenNR.ini.");
        return 0;
    }

    const DWORD attrs = GetFileAttributesW(g_optiscalerPath.c_str());
    if (attrs == INVALID_FILE_ATTRIBUTES || (attrs & FILE_ATTRIBUTE_DIRECTORY)) {
        Log(L"Configured OptiScalerPath does not exist or is a directory.");
        return 0;
    }

    // OptiScaler selects its operating mode from the loaded module's filename.
    // Loading OptiScaler.dll directly does not activate its version.dll proxy mode.
    // Stage a copy beside the configured DLL as version.dll so its normal DllMain
    // follows the same mode-selection path as a conventional version.dll install.
    const auto slash = g_optiscalerPath.find_last_of(L"\\/");
    const std::wstring directory = slash == std::wstring::npos ? L"." : g_optiscalerPath.substr(0, slash);
    g_stagedPath = directory + L"\\version.dll";

    const DWORD stagedAttrs = GetFileAttributesW(g_stagedPath.c_str());
    if (stagedAttrs != INVALID_FILE_ATTRIBUTES && !(stagedAttrs & FILE_ATTRIBUTE_DIRECTORY)) {
        // Never overwrite an existing file in the OptiScaler folder.
        // If it is already our own staged copy, refresh it only when it is safe to do so.
        Log(L"A version.dll already exists beside OptiScalerPath; refusing to overwrite it. Use a dedicated OptiScaler folder.");
        return 0;
    }

    if (!CopyFileW(g_optiscalerPath.c_str(), g_stagedPath.c_str(), TRUE)) {
        Log(L"Could not stage OptiScaler as version.dll (Win32 error " +
            std::to_wstring(GetLastError()) + L"). Use a dedicated OptiScaler folder with write permission.");
        return 0;
    }

    HMODULE loaded = LoadLibraryExW(g_stagedPath.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
    if (loaded) {
        Log(L"Loaded staged OptiScaler copy as version.dll: " + g_stagedPath);
    } else {
        const DWORD error = GetLastError();
        DeleteFileW(g_stagedPath.c_str());
        Log(L"Loading staged OptiScaler failed (Win32 error " + std::to_wstring(error) + L").");
    }
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
FORWARD(DWORD, VerFindFileA, (DWORD a, LPSTR b, LPSTR c, LPSTR d, UINT* e, LPSTR f, UINT* g), (a,b,c,d,e,f,g))
FORWARD(DWORD, VerFindFileW, (DWORD a, LPWSTR b, LPWSTR c, LPWSTR d, UINT* e, LPWSTR f, UINT* g), (a,b,c,d,e,f,g))
FORWARD(DWORD, VerInstallFileA, (DWORD a, LPSTR b, LPSTR c, LPSTR d, LPSTR e, LPSTR f, LPSTR g, UINT* h), (a,b,c,d,e,f,g,h))
FORWARD(DWORD, VerInstallFileW, (DWORD a, LPWSTR b, LPWSTR c, LPWSTR d, LPWSTR e, LPWSTR f, LPWSTR g, UINT* h), (a,b,c,d,e,f,g,h))
FORWARD(BOOL, VerQueryValueA, (LPCVOID a, LPCSTR b, LPVOID* c, PUINT d), (a,b,c,d))
FORWARD(BOOL, VerQueryValueW, (LPCVOID a, LPCWSTR b, LPVOID* c, PUINT d), (a,b,c,d))

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(instance);
        g_proxyModule = instance;
        HANDLE thread = CreateThread(nullptr, 0, LoadOptiScalerThread, nullptr, 0, nullptr);
        if (thread) CloseHandle(thread);
    } else if (reason == DLL_PROCESS_DETACH) {
        if (g_systemVersion) FreeLibrary(g_systemVersion);
        if (!g_stagedPath.empty()) DeleteFileW(g_stagedPath.c_str());
    }
    return TRUE;
}
