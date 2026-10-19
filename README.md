# Optijetor

An x64 `version.dll` proxy/bootstrapper that forwards Windows version APIs and loads a separately located OptiScaler installation.

## How the bootstrap works

OptiScaler selects its proxy mode from the loaded DLL filename. Loading `OptiScaler.dll` directly is not equivalent to loading it as `version.dll`, so Optijetor stages a copy as `version.dll` under a per-process directory in the Windows temporary folder.

The staging directory is intentionally **not created in the OptiScaler installation or game folder**. A per-process directory avoids overwriting another `version.dll` and allows multiple game processes to run independently. Old staging directories are cleaned on later launches when their process is no longer running.

## Game upscaler DLL bridging

By default, `BridgeGameDlls=1` mirrors root-level `.dll` files from the game executable folder and the configured OptiScaler folder into the temporary runtime directory. Existing destination files are never overwritten. OptiScaler's root-level `.ini` and `.json` sidecar files are also bridged so it can read its existing configuration and find the game's upscaler libraries from its own module directory.

Optijetor tries a Windows hard link first (no duplicate file contents when both locations are on the same volume), then falls back to copying if hard links are unavailable. This means the game directory and OptiScaler installation remain unchanged apart from normal logging; the temporary folder contains the runtime aliases/copies.

Set `BridgeGameDlls=0` to disable bridging of game DLLs. Configuration sidecars are still bridged.

## Files and configuration

- `version.dll` — place beside the game executable.
- `OpenNR.ini` — place beside `version.dll`.
- OptiScaler — keep its DLL and companion files in a separate folder if desired.

```ini
[OpenNR]
; Absolute path to the separate OptiScaler DLL
OptiScalerPath=C:\OptiScaler\OptiScaler.dll

; Delay before attempting to load OptiScaler (milliseconds; 0-30000)
LoadDelayMs=1500

; Bridge game and OptiScaler DLLs into the temporary runtime directory
BridgeGameDlls=1

; 1 writes OpenNR.log beside version.dll; 0 disables file logging
Logging=1
```

Use a normal Windows path with single backslashes. The path must point to an existing DLL; relative paths are not supported.

## Build

Open `Optijetor.sln` in Visual Studio 2022 with the **Desktop development with C++** workload, select **Release | x64**, and build. GitHub Actions builds the same configuration and uploads an `Optijetor-x64` artifact.

## Installation

1. Back up any existing `version.dll` beside the game executable; another proxy may be required by the game or launcher.
2. Copy the built `version.dll` and `OpenNR.ini` beside the game executable.
3. Set `OptiScalerPath` to the full path of the separate OptiScaler DLL.
4. Launch the game and inspect `OpenNR.log` if loading fails.

## Compatibility notes

- Optijetor forwards the common named version-information exports to Windows System32 `version.dll`.
- Bridging is limited to root-level DLLs and root-level INI/JSON sidecars; nested folders and arbitrary data files are not mirrored.
- A successful CI build proves the proxy compiles and its named exports exist; it does not replace testing the graphics-hook path in the target game.
- Proxy DLLs can conflict with another proxy or violate a game's rules; use only where permitted.
