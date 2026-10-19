# Optijetor

An x64 `version.dll` proxy/bootstrapper that forwards Windows version APIs and loads a separately installed OptiScaler binary that you name `version.dll`.

## How it works

OptiScaler selects its proxy mode based on the loaded module filename. Keep Optijetor's `version.dll` beside the game executable, and place a separate OptiScaler build named `version.dll` in its own folder. Set `OptiScalerPath` in `OpenNR.ini` to that separate file.

Optijetor loads the configured OptiScaler DLL **directly from its actual installation path**. It does not create temporary directories, stage/rename DLLs, bridge files, create hard links, or copy the game's DLLs. Windows is asked to search the configured DLL's own directory for its dependencies, so keep OptiScaler's companion/upscaler DLLs alongside that OptiScaler `version.dll` as appropriate for the build.

## Files and configuration

- `version.dll` — Optijetor proxy, placed beside the game executable.
- `OpenNR.ini` — placed beside Optijetor's proxy.
- OptiScaler — separate folder containing OptiScaler renamed to `version.dll` and its own companion files.

```ini
[OpenNR]
; Absolute path to your separate OptiScaler DLL, renamed to version.dll
OptiScalerPath=C:\OptiScaler\version.dll

; Delay before attempting to load OptiScaler (milliseconds; 0-30000)
LoadDelayMs=1500

; 1 writes OpenNR.log beside the game's version.dll proxy; 0 disables file logging
Logging=1
```

Use a normal Windows path with single backslashes. The path must point to an existing DLL; relative paths are not supported.

## Build

Open `Optijetor.sln` in Visual Studio 2022 with the **Desktop development with C++** workload, select **Release | x64**, and build. GitHub Actions builds the same configuration and uploads an `Optijetor-x64` artifact.

## Installation

1. Back up any existing `version.dll` beside the game executable; another proxy may be required by the game or launcher.
2. Copy Optijetor's built `version.dll` and `OpenNR.ini` beside the game executable.
3. Put the separate OptiScaler build in its own directory, name it `version.dll`, and keep its companion files beside it.
4. Set `OptiScalerPath` to the full path of that OptiScaler `version.dll`.
5. Launch the game and inspect `OpenNR.log` if loading fails.

## Compatibility notes

- Optijetor forwards the common named version-information exports to Windows System32 `version.dll`.
- OptiScaler is loaded directly from the configured full path, using its directory for dependency lookup. This avoids moving or mirroring companion files, but the actual game's loading path still needs to be tested.
- A successful CI build proves the proxy compiles and its named exports exist; it does not replace testing the graphics-hook path in the target game.
- Proxy DLLs can conflict with another proxy or violate a game's rules; use only where permitted.
