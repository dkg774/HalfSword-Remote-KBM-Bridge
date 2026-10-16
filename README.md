# Half Sword Remote KBM Bridge

Half Sword Remote KBM Bridge gives the Steam Remote Play Together joiner independent keyboard and mouse control of Player 2. The host keeps normal keyboard and mouse control of Player 1. It does not emulate a controller.

## Nexus Mods release

[Half Sword Remote KBM Bridge on Nexus Mods](https://www.nexusmods.com/halfsword/mods/557)

## 1.3 Lock On Unstable Beta — manual review

The beta adds experimental lock-on for both players, adjustable lock-on timing, separate Controls / Lock On / Record / Keybinds tabs, press-to-bind joiner controls, and recording issue markers. It targets Half Sword Early Access 0.6.15. This is an unstable beta based on Test 49; complete multiplayer and newer-game compatibility testing has not been established.

The complete beta source is included alongside the 1.2 source:

- `src/bridge_v1_3_beta.cpp`: Steam Remote Play input routing, recordings, and lifecycle.
- `src/bridge_loader_v1_3_beta.cpp`: game process validation and Windows message-hook loading.
- `src/bridge_controls_v1_3_beta.cpp`: controller interface and settings.
- `src/keybindings_v1_3_beta.h`: shared settings and key definitions.
- `src/lockon_core_v1_3_beta.h`: target selection and yaw-only rotation with high-resolution timing.

The beta loads `HalfSwordBridge13Beta.dll` into the game through `SetWindowsHookExW`. It invokes game input and rotation functions and reads/writes game state. These behaviors are relevant to antivirus review; a detection is not assumed to be a confirmed false positive merely because the program is a mod.

From this repository's root in an **x64 Native Tools Command Prompt for Visual Studio 2022**, using Steamworks SDK 1.65:

```bat
set STEAMWORKS_SDK=C:\path\to\steamworks_sdk_165
build_1_3_beta.cmd
```

Outputs: `build/HalfSwordJoinerControls13Beta.exe` and `build/HalfSwordBridge13Beta.dll`. Valve's SDK is not redistributed here. The existing `build.cmd` builds version 1.2.

Package: `HalfSwordRemoteKBM-1.3-Lock-On-Unstable-Beta.zip`

| File | SHA-256 of the supplied artifact |
| --- | --- |
| ZIP | `46F34CE1F4EC1B7703B5C5DBCB69CEC1746A1B45D11F45671B86A9FABA115170` |
| DLL | `E5E4961D5AC384FF46344255886EB49C7DD229C56F8EE4ED277DDE55F608C4F2` |
| EXE | `3627A76DFB1781D88CE4626E9AD3CA8000C16984C4AF9C31CF20843E665C220C` |

These hashes identify the supplied package and binaries. Rebuilding can produce different hashes because compiler versions, timestamps, and build paths can differ.

See [beta setup instructions](docs/1.3-lock-on-unstable-beta/README.txt) and [Bug Reporting Guide](docs/1.3-lock-on-unstable-beta/Bug%20Reporting%20Guide.txt). Reports and logs can be emailed to alexanderschmidt13@yahoo.com. Logs are saved locally beside the application; email submission is manual.

## Version 1.2 implementation

The launcher locates the standard `HalfSwordUE5-Win64-Shipping.exe` process and loads `HalfSwordBridge21.dll` through a temporary Windows message hook. The bridge enables Steamworks Remote Play Together direct input, reads the joiner's keyboard and mouse events with `ISteamRemotePlay::GetInput`, and dispatches the corresponding Half Sword Player 2 input functions.

The loader validates the process name and expected Half Sword installation path before loading the DLL. It does not create a remote thread or write a DLL path into the game process. The bridge operates only while Half Sword is running and can be stopped from the launcher.

## Antivirus detections

The bridge must run inside Half Sword to call the game's input functions. Version 1.2 uses `SetWindowsHookExW` for that bootstrap and requests only enough process access to validate the target path. Some antivirus products may still report unfamiliar unsigned executables or DLL loading behavior. The complete launcher and bridge source is published here for review.

Version 1.2 release ZIP SHA-256:

```text
5FEE9A8905AF6ED3F1ABDF106E8DFDD573335CB58E13D70A584724D5D9DC8565
```

[VirusTotal analysis for the Version 1.2 ZIP](https://www.virustotal.com/gui/file/5fee9a8905af6ed3f1abdf106e8dfdd573335cb58e13d70a584724d5d9dc8565)

## Building

Requirements:

- Windows 10 or 11
- Visual Studio 2022 Build Tools with Desktop development with C++
- Steamworks SDK 1.65

Valve's Steamworks SDK is not included because its files are distributed under Valve's terms. Download it from the [official Steamworks SDK page](https://partner.steamgames.com/doc/sdk).

Open an x64 Native Tools Command Prompt for Visual Studio, then run:

```bat
set STEAMWORKS_SDK=C:\path\to\steamworks_sdk_165
build.cmd
```

The compiled files will be placed in `build`.

## Version compatibility

Version 1.2 targets Half Sword Early Access 0.6.15. It relies on game addresses and Unreal Engine function names from that build, so a Half Sword update may require corresponding source changes.

## Source layout

- `src/bridge_v21.cpp`: Steam Remote Play input capture and Player 2 game input bridge
- `src/bridge_loader_v21.cpp`: restricted Windows message-hook bootstrap
- `src/bridge_controls_v21.cpp`: launcher, controls, settings, recording, and keybind interface
- `src/keybindings_v21.h`: shared configurable keybind definitions

## Version 1.2 changes

- Improves cross-device keyboard and mouse compatibility.
- Fixes remote mouse attacks on hosts whose Unreal input settings omit usable cached mouse-key details.
- Avoids repeated internal input discovery that caused button delay or stalls on affected hosts.
- Uses safe direct fallbacks when optional native key mappings are unavailable.
- Allows more startup time on slower hosts before reporting a loading failure.
- Adds raw Steam Remote Play inputs, binding matches, and dispatch routes to deliberate troubleshooting recordings.
- Retains customizable keybinds, sensitivity, Classic Alt Thrust support, input reset, and Player 1, Player 2, or combined recordings.
- Keeps the experimental lock-on prototype separate from this release.

## Privacy and networking

The application does not include telemetry, advertising, automatic updates, or an independent network service. Network transport is provided by Steam Remote Play Together. Troubleshooting recordings are local text files created only when the user enables recording.

## License

The source code is available under the MIT License. Half Sword and Steamworks are owned by their respective rights holders.
