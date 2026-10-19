# Optijetor

An x64 `version.dll` proxy/bootstrapper that forwards Windows version APIs and attempts to load a separately located OptiScaler installation.

## Important implementation detail

OptiScaler selects its working mode from the loaded DLL filename. Loading `OptiScaler.dll` directly is not equivalent to loading it as `version.dll`. Optijetor therefore copies the configured DLL to a per-process `OpenNR-<PID>\\version.dll` staging folder beside the configured DLL, then loads that staged copy. The original OptiScaler DLL is not overwritten.

This is a practical bootstrap implementation, but it still needs to be tested in a real game with the target OptiScaler build. OptiScaler's companion files and configuration must be accessible, and its `MainDllPath` setting should point to the folder containing the full OptiScaler installation when that differs from the default game-local `OptiScaler` folder.

## Files

- `version.dll` — place beside the game executable.
- `OpenNR.ini` — place beside `version.dll`.
- OptiScaler — keep its DLL and companion files in a separate, dedicated folder.

## OpenNR.ini

```ini
[OpenNR]
; Absolute path to the separate OptiScaler DLL
OptiScalerPath=C:\OptiScaler\OptiScaler.dll

; Delay before attempting to load OptiScaler (milliseconds; 0-30000)
LoadDelayMs=1500

; 1 writes OpenNR.log beside version.dll; 0 disables file logging
Logging=1
```

Use a normal Windows path with single backslashes. The path must point to an existing DLL; relative paths are not supported.

## Build

Open `Optijetor.sln` in Visual Studio 2022 with the **Desktop development with C++** workload, select **Release | x64**, and build. GitHub Actions builds the same configuration and uploads a `Optijetor-x64` artifact.

## Installation

1. Back up any existing `version.dll` beside the game executable; another proxy may be required by the game or launcher.
2. Copy the built `version.dll` and `OpenNR.ini` beside the game executable.
3. Set `OptiScalerPath` to the full path of the separate OptiScaler DLL in its dedicated folder.
4. Confirm the OptiScaler configuration and companion files are available. If necessary, set OptiScaler's `MainDllPath` to that installation folder.
5. Launch the game and inspect `OpenNR.log` if loading fails.

## Logging and cleanup

The bootstrapper writes diagnostic messages to `OpenNR.log`. OptiScaler is staged under a process-specific subfolder so the bootstrapper never overwrites an existing `version.dll` in the OptiScaler directory. A staged copy may remain after the game exits; it is not safe to delete a mapped DLL during process teardown.

## Compatibility notes

- This project forwards the common named version-information exports to the Windows system `version.dll`.
- It does not download OptiScaler, patch game files, or bypass anti-cheat.
- Proxy DLLs can conflict with another proxy or violate a game's rules; use only where permitted.
- Successful CI compilation proves the proxy builds, not that every OptiScaler/game combination initializes correctly.
