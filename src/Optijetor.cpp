#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winver.h>
#include <string>
#include <fstream>
#include <mutex>

extern "C" IMAGE_DOS_HEADER __ImageBase;

static HMODULE g_systemVersion = nullptr;
static std::wstring g_optiscalerPath;
static DWORD g_delayMs = 1500;
static bool g_logging = true;
static std::wstring g_root;
static std::mutex g_logMutex;

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

static std::wstring ReadString(const wchar_t* section, const wchar_t* key, const wchar_t* fallback)
{
    wchar_t value[32768]{};
    GetPrivateProfileStringW(section, key, fallback, value, 32768, (g_root + L"\\OpenNR.ini").c_str());
    return value;
}

static void InitializeConfig()
{
    g_root = ModuleDirectory();
    g_optiscalerPath = ReadString(L"OpenNR", L"OptiScalerPath", L"");
    g_delayMs = GetPrivateProfileIntW(L"OpenNR", L"LoadDelayMs", 1500, (g_root + L"\\OpenNR.ini").c_str());
    if (g_delayMs > 30000) g_delayMs = 30000;
    g_logging = GetPrivateProfileIntW(L"OpenNR", L"Logging", 1, (g_root + L"\\OpenNR.ini").c_str()) != 0;
}

static DWORD WINAPI LoadOptiScalerThread(void*)
{
    if (g_delayMs) Sleep(g_delayMs);
    if (g_optiscalerPath.empty()) {
        Log(L"OptiScalerPath is empty; skipping load.");
        return 0;
    }
    const DWORD attrs = GetFileAttributesW(g_optiscalerPath.c_str());
    if (attrs == INVALID_FILE_ATTRIBUTES || (attrs & FILE_ATTRIBUTE_DIRECTORY)) {
        Log(L"Configured OptiScalerPath does not exist or is a directory.");
        return 0;
    }
    HMODULE loaded = LoadLibraryExW(g_optiscalerPath.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
    if (loaded) Log(L"OptiScaler DLL loaded as a module. Module load alone may not activate every graphics-proxy hook.");
    else Log(L"LoadLibraryExW failed for OptiScaler (Win32 error " + std::to_wstring(GetLastError()) + L").");
    return 0;
}

static FARPROC Resolve(const char* name)
{
    if (!g_systemVersion) {
        wchar_t systemDir[MAX_PATH]{};
        UINT len = GetSystemDirectoryW(systemDir, MAX_PATH);
        if (len && len < MAX_PATH) {
            std::wstring path(systemDir);
            path += L"\\version.dll";
            g_systemVersion = LoadLibraryW(path.c_str());
        }
    }
    return g_systemVersion ? GetProcAddress(g_systemVersion, name) : nullptr;
}

#define FORWARD(ret, name, params, args) \
extern "C" __declspec(dllexport) ret WINAPI name params { \
    using Fn = ret (WINAPI*) params; \
    static Fn fn = reinterpret_cast<Fn>(Resolve(#name)); \
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
        InitializeConfig();
        Log(L"Proxy initialized.");
        HANDLE thread = CreateThread(nullptr, 0, LoadOptiScalerThread, nullptr, 0, nullptr);
        if (thread) CloseHandle(thread);
        else Log(L"Could not create loader thread.");
    } else if (reason == DLL_PROCESS_DETACH) {
        if (g_systemVersion) FreeLibrary(g_systemVersion);
    }
    return TRUE;
}
