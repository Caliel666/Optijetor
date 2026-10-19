#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <string>
#include <fstream>
#include <mutex>

extern "C" IMAGE_DOS_HEADER __ImageBase;

static HMODULE g_systemVersion = nullptr;
static std::wstring g_optiscalerPath;
static std::wstring g_stagedPath;
static std::wstring g_stageDirectory;
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
    GetPrivateProfileStringW(L"OpenNR", L"OptiScalerPath", L"", value, 32768, ini.c_str());
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

    const auto slash = g_optiscalerPath.find_last_of(L"\\/");
    const std::wstring sourceDirectory = slash == std::wstring::npos ? L"." : g_optiscalerPath.substr(0, slash);

    // OptiScaler selects its proxy mode using the loaded module's filename.
    // Stage it as version.dll in a per-process subdirectory instead of loading
    // OptiScaler.dll directly, which does not activate version.dll proxy mode.
    g_stageDirectory = sourceDirectory + L"\\OpenNR-" + std::to_wstring(GetCurrentProcessId());
    if (!CreateDirectoryW(g_stageDirectory.c_str(), nullptr)) {
        Log(L"Could not create staging directory (Win32 error " + std::to_wstring(GetLastError()) + L").");
        return 0;
    }
    g_stagedPath = g_stageDirectory + L"\\version.dll";

    if (!CopyFileW(g_optiscalerPath.c_str(), g_stagedPath.c_str(), TRUE)) {
        Log(L"Could not stage OptiScaler as version.dll (Win32 error " +
            std::to_wstring(GetLastError()) + L").");
        RemoveDirectoryW(g_stageDirectory.c_str());
        return 0;
    }

    // Preserve dependency lookup from the folder containing the configured DLL.
    DLL_DIRECTORY_COOKIE cookie = AddDllDirectory(sourceDirectory.c_str());
    HMODULE loaded = LoadLibraryExW(g_stagedPath.c_str(), nullptr,
        LOAD_LIBRARY_SEARCH_DEFAULT_DIRS | LOAD_LIBRARY_SEARCH_USER_DIRS);
    if (cookie) RemoveDllDirectory(cookie);

    if (loaded) {
        Log(L"Loaded staged OptiScaler copy as version.dll: " + g_stagedPath);
        Log(L"Ensure OptiScaler's MainDllPath/config points at the folder containing its companion files.");
    } else {
        const DWORD error = GetLastError();
        DeleteFileW(g_stagedPath.c_str());
        RemoveDirectoryW(g_stageDirectory.c_str());
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
        // The staged module is still loaded during process teardown; leave its
        // per-process copy in place rather than deleting a mapped image.
    }
    return TRUE;
}
