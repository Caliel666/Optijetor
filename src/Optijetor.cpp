#include <windows.h>
#include <string>
#include <fstream>
#include <mutex>
#include <cwchar>

extern "C" IMAGE_DOS_HEADER __ImageBase;

static HMODULE g_systemVersion = nullptr;
static std::wstring g_optiscalerPath;
static std::wstring g_stagedPath;
static std::wstring g_stageDirectory;
static std::wstring g_stageRoot;
static DWORD g_delayMs = 1500;
static bool g_logging = true;
static bool g_bridgeGameDlls = true;
static std::wstring g_root;
static std::mutex g_logMutex;
static INIT_ONCE g_resolveOnce = INIT_ONCE_STATIC_INIT;

static std::wstring ParentDirectory(const std::wstring& path)
{
    const auto slash = path.find_last_of(L"\\/");
    return slash == std::wstring::npos ? L"." : path.substr(0, slash);
}

static std::wstring ModuleDirectory()
{
    wchar_t path[MAX_PATH]{};
    GetModuleFileNameW(reinterpret_cast<HMODULE>(&__ImageBase), path, MAX_PATH);
    return ParentDirectory(path);
}

static std::wstring GameDirectory()
{
    wchar_t path[MAX_PATH]{};
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    return ParentDirectory(path);
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
    g_bridgeGameDlls = GetPrivateProfileIntW(L"OpenNR", L"BridgeGameDlls", 1, ini.c_str()) != 0;
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

static bool Exists(const std::wstring& path)
{
    return GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES;
}

static void CleanupStaleStageDirectories()
{
    if (g_stageRoot.empty()) return;
    WIN32_FIND_DATAW data{};
    HANDLE find = FindFirstFileW((g_stageRoot + L"\\OpenNR-*").c_str(), &data);
    if (find == INVALID_HANDLE_VALUE) return;

    do {
        if (!(data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ||
            wcscmp(data.cFileName, L".") == 0 || wcscmp(data.cFileName, L"..") == 0)
            continue;

        const std::wstring name = data.cFileName;
        const auto dash = name.find_last_of(L'-');
        if (dash == std::wstring::npos) continue;
        wchar_t* end = nullptr;
        const unsigned long pid = wcstoul(name.c_str() + dash + 1, &end, 10);
        if (!pid || !end || *end != L'\0' || pid == GetCurrentProcessId()) continue;

        HANDLE process = OpenProcess(SYNCHRONIZE, FALSE, pid);
        if (process) {
            const DWORD state = WaitForSingleObject(process, 0);
            CloseHandle(process);
            if (state == WAIT_TIMEOUT) continue;
        } else if (GetLastError() == ERROR_ACCESS_DENIED) {
            continue;
        }

        const std::wstring directory = g_stageRoot + L"\\" + name;
        WIN32_FIND_DATAW fileData{};
        HANDLE files = FindFirstFileW((directory + L"\\*").c_str(), &fileData);
        if (files != INVALID_HANDLE_VALUE) {
            do {
                if (wcscmp(fileData.cFileName, L".") == 0 || wcscmp(fileData.cFileName, L"..") == 0)
                    continue;
                if (!(fileData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
                    DeleteFileW((directory + L"\\" + fileData.cFileName).c_str());
            } while (FindNextFileW(files, &fileData));
            FindClose(files);
        }
        RemoveDirectoryW(directory.c_str());
    } while (FindNextFileW(find, &data));
    FindClose(find);
}

static bool LinkOrCopy(const std::wstring& source, const std::wstring& destination, bool preferHardLink)
{
    if (Exists(destination)) return false;
    if (preferHardLink && CreateHardLinkW(destination.c_str(), source.c_str(), nullptr))
        return true;
    return CopyFileW(source.c_str(), destination.c_str(), TRUE) != FALSE;
}

static unsigned int BridgePattern(const std::wstring& sourceDirectory, const wchar_t* pattern, bool hardLink)
{
    if (sourceDirectory.empty() || !Exists(sourceDirectory)) return 0;
    WIN32_FIND_DATAW data{};
    HANDLE find = FindFirstFileW((sourceDirectory + L"\\" + pattern).c_str(), &data);
    if (find == INVALID_HANDLE_VALUE) return 0;

    unsigned int count = 0;
    do {
        if (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        const std::wstring source = sourceDirectory + L"\\" + data.cFileName;
        const std::wstring destination = g_stageDirectory + L"\\" + data.cFileName;
        if (_wcsicmp(source.c_str(), destination.c_str()) == 0) continue;
        if (LinkOrCopy(source, destination, hardLink)) ++count;
    } while (FindNextFileW(find, &data));
    FindClose(find);
    return count;
}

static void BridgeFiles(const std::wstring& sourceDirectory, const std::wstring& gameDirectory)
{
    // Mirror OptiScaler config files first so it reads its existing settings
    // rather than silently generating a fresh config beside the staged image.
    unsigned int configs = BridgePattern(sourceDirectory, L"*.ini", true);
    configs += BridgePattern(gameDirectory, L"*.ini", true);

    unsigned int dlls = 0;
    if (g_bridgeGameDlls) {
        // Game-provided upscaler/NGX/FFX/XeSS DLLs need to be visible beside the
        // staged version.dll because OptiScaler resolves many "auto" paths there.
        dlls += BridgePattern(gameDirectory, L"*.dll", true);
    }
    // Also expose OptiScaler's companion DLLs; don't overwrite a game-provided
    // DLL with the same filename.
    dlls += BridgePattern(sourceDirectory, L"*.dll", true);

    // Some builds use JSON sidecar configuration files.
    BridgePattern(sourceDirectory, L"*.json", true);
    BridgePattern(gameDirectory, L"*.json", true);

    Log(L"Bridged " + std::to_wstring(dlls) + L" DLL file(s) and " +
        std::to_wstring(configs) + L" INI file(s) into the temporary OptiScaler runtime directory.");
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

    const std::wstring sourceDirectory = ParentDirectory(g_optiscalerPath);
    const std::wstring gameDirectory = GameDirectory();
    wchar_t tempPath[MAX_PATH]{};
    const DWORD tempLen = GetTempPathW(MAX_PATH, tempPath);
    if (!tempLen || tempLen >= MAX_PATH) {
        Log(L"Could not resolve the Windows temporary directory.");
        return 0;
    }

    g_stageRoot = std::wstring(tempPath) + L"Optijetor";
    if (!CreateDirectoryW(g_stageRoot.c_str(), nullptr) && GetLastError() != ERROR_ALREADY_EXISTS) {
        Log(L"Could not create the temporary staging root (Win32 error " +
            std::to_wstring(GetLastError()) + L").");
        return 0;
    }

    CleanupStaleStageDirectories();
    g_stageDirectory = g_stageRoot + L"\\OpenNR-" + std::to_wstring(GetCurrentProcessId());
    if (!CreateDirectoryW(g_stageDirectory.c_str(), nullptr)) {
        Log(L"Could not create the temporary staging directory (Win32 error " +
            std::to_wstring(GetLastError()) + L").");
        return 0;
    }
    g_stagedPath = g_stageDirectory + L"\\version.dll";

    if (!CopyFileW(g_optiscalerPath.c_str(), g_stagedPath.c_str(), TRUE)) {
        Log(L"Could not stage OptiScaler as version.dll (Win32 error " +
            std::to_wstring(GetLastError()) + L").");
        RemoveDirectoryW(g_stageDirectory.c_str());
        return 0;
    }

    BridgeFiles(sourceDirectory, gameDirectory);

    // Give Windows dependency resolution both original locations as well as
    // the staging directory. This complements the bridged files.
    DLL_DIRECTORY_COOKIE sourceCookie = AddDllDirectory(sourceDirectory.c_str());
    DLL_DIRECTORY_COOKIE gameCookie = AddDllDirectory(gameDirectory.c_str());
    HMODULE loaded = LoadLibraryExW(g_stagedPath.c_str(), nullptr,
        LOAD_LIBRARY_SEARCH_DEFAULT_DIRS | LOAD_LIBRARY_SEARCH_USER_DIRS);
    if (gameCookie) RemoveDllDirectory(gameCookie);
    if (sourceCookie) RemoveDllDirectory(sourceCookie);

    if (loaded) {
        Log(L"Loaded staged OptiScaler as version.dll from temporary directory: " + g_stagedPath);
        Log(L"OptiScaler files from the game and installation directories were bridged where missing.");
    } else {
        const DWORD error = GetLastError();
        DeleteFileW(g_stagedPath.c_str());
        // Other bridged files may be hard links to originals; deleting the
        // staging directory only removes those links, not their source files.
        WIN32_FIND_DATAW fileData{};
        HANDLE files = FindFirstFileW((g_stageDirectory + L"\\*").c_str(), &fileData);
        if (files != INVALID_HANDLE_VALUE) {
            do {
                if (wcscmp(fileData.cFileName, L".") != 0 && wcscmp(fileData.cFileName, L"..") != 0 &&
                    !(fileData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
                    DeleteFileW((g_stageDirectory + L"\\" + fileData.cFileName).c_str());
            } while (FindNextFileW(files, &fileData));
            FindClose(files);
        }
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
        // Keep staged files until the process exits; a mapped DLL cannot be
        // safely removed here. The next launch cleans stale per-process folders.
    }
    return TRUE;
}
