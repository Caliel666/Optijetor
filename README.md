# Optijetor

A small x64 `version.dll` proxy/bootstrapper for loading a separately installed OptiScaler build.

> **Status: early prototype.** Loading `OptiScaler.dll` as a module is not guaranteed to reproduce the initialization path of a graphics API proxy. This project deliberately logs the result and does not claim that every OptiScaler build will initialize through this method. A follow-up integration should use the supported OptiScaler proxy/loader mechanism for the target game.

## Layout

- `version.dll` — place beside the game executable.
- `OpenNR.ini` — optional configuration beside `version.dll`.
- OptiScaler files — remain in their own directory, configured by absolute path.

## OpenNR.ini

```ini
[OpenNR]
; Absolute path to the OptiScaler DLL. Use double backslashes only if your editor requires them.
OptiScalerPath=C:\\OptiScaler\\OptiScaler.dll

; Delay before attempting to load OptiScaler (milliseconds). Default: 1500.
; Tune this for the game's startup; very early loading can be too early for some engines.
LoadDelayMs=1500

; 1 writes a small OpenNR.log beside version.dll; 0 disables file logging.
Logging=1
```

The path must point to a real DLL. Relative paths are intentionally not supported to avoid ambiguous working-directory behavior.

## Build

Open `Optijetor.sln` in Visual Studio 2022 with the **Desktop development with C++** workload, select **Release | x64**, and build. GitHub Actions builds the same x64 configuration and publishes the build output as an artifact.

## Installation

1. Back up the game's existing `version.dll` if one exists. Some games or launchers depend on another proxy.
2. Copy the built `version.dll` and `OpenNR.ini` beside the game executable.
3. Set `OptiScalerPath` to the separate OptiScaler DLL.
4. Launch the game and inspect `OpenNR.log` if loading fails.

## Safety / compatibility

This is a local, opt-in DLL loader. It does not download or replace OptiScaler files, modify game files beyond the proxy/config you place, or attempt to bypass anti-cheat. Do not use proxy DLLs with games whose rules prohibit them. The proxy forwards version-information APIs to the Windows system `version.dll` and attempts OptiScaler loading after a configurable delay.