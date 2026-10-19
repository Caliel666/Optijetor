# Optijetor

An x64 `version.dll` proxy/bootstrapper that forwards Windows version APIs and loads a separately installed OptiScaler binary named `version.dll`.

## Bridge behavior

Keep Optijetor's proxy beside the game executable and configure `OptiScalerPath` to the separate OptiScaler `version.dll`. Before loading OptiScaler, Optijetor recursively scans the configured OptiScaler installation, including subdirectories, and exposes each file in the game root using filesystem links rather than copying file contents:

- Hard links are attempted first (same-volume, zero additional file data).
- Symbolic links are attempted if a hard link cannot be created.
- Existing game files are never overwritten.
- `version.dll` and `OpenNR.ini` are never bridged.
- Each discovered file is logged, along with scan totals and link failures.
- The game directory is added to the DLL search path, and the external OptiScaler directory is added as an additional DLL directory.

**Important limitation:** this is a recursive companion-file bridge, not a full filesystem virtualization driver. The loaded OptiScaler module still has its real external module path; Windows does not let a normal proxy change that path simply by using `LoadLibraryEx`. If OptiScaler derives its internal `DllPath` strictly from its own module path, further process API interception may be required. Please attach `OpenNR.log` if hooks are still not detected so the scan and actual resolved paths can be compared.

## Configuration

```ini
[OpenNR]
; Absolute path to the separate OptiScaler DLL, renamed to version.dll
OptiScalerPath=C:\OptiScaler\version.dll

; Delay before loading OptiScaler (milliseconds; 0-30000; default 0)
LoadDelayMs=0

; 1 writes OpenNR.log beside the game's version.dll proxy; 0 disables file logging
Logging=1
```

Use a normal Windows path with single backslashes. The path must point to an existing DLL; relative paths are not supported.

## Installation

1. Back up any existing `version.dll` beside the game executable.
2. Copy Optijetor's built `version.dll` and `OpenNR.ini` beside the game executable.
3. Keep the complete OptiScaler installation in its separate directory.
4. Set `OptiScalerPath` to the full path of that installation's `version.dll`.
5. Launch the game and inspect `OpenNR.log` if loading or hooks fail.

## Build and validation

Open `Optijetor.sln` in Visual Studio 2022 with the **Desktop development with C++** workload and build **Release | x64**. GitHub Actions builds the same configuration and uploads an `Optijetor-x64` artifact.

A successful CI build verifies compilation and exports; it does not prove the target game's graphics hooks work. Proxy DLLs can conflict with other proxies or violate a game's rules; use only where permitted.
