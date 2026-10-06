# Split/Second HFR — native 60 FPS for Split/Second (PC)

Split/Second (Black Rock Studio, 2010) is locked to 30 FPS on PC, and simply removing the cap makes the game run
at the wrong speed and breaks its physics. This mod makes the game run a **real 60 Hz game loop** instead — it is
not frame interpolation — while keeping the game's speed and physics exactly as designed.

## What it does

- **Native 60 FPS.** Internally the game already simulates physics in fixed 1/60 s steps; at 30 FPS it just runs two
  of them per frame. The mod changes the frame limiter so it runs one step per 1/60 s frame. Everything that counts
  those steps keeps its real-time speed, and the car physics are identical to the original.
- **Precise frame pacing.** The game's limiter sleeps in whole milliseconds; the mod waits for the exact deadline.
- **AI fix.** The AI drivers' steering controller is tuned per frame, so they keep making decisions at their
  original 30 Hz rate and drive exactly as in the original game.
- **F8** switches between 30 and 60 FPS while playing.
- **Nothing on disk is modified** and the game's copy protection is left untouched: the game itself loads the mod
  at start-up (it looks for a debugging library called `DXFreezerServer.dll` in its folder).

## Status

Version 0.1.1 — early release. Verified: steady 60 FPS, physics and handling, game speed, live 30 ↔ 60 switching.
Not yet verified at 60 FPS: every Power Play (especially multi-stage ones), shortcut doors and ramps, all tracks.
Please report anything that behaves differently at 60 than at 30 (F8 makes comparing easy).

For 120 Hz displays: run the mod at 60 FPS and add frame generation on top (e.g. Lossless Scaling ×2), or use VRR.

## Requirements

- The **Steam version** of Split/Second (`SplitSecond.exe` 1.0.0.1). Other executables (retail discs, patched or
  "unpacked" exes) are detected and the mod stays inactive.
- Windows 10 or 11.

## Install

1. Download `SplitSecond-HFR-v0.1.1.zip` from the [Releases](../../releases) page.
2. Copy `DXFreezerServer.dll` and `SplitSecondHFR.ini` into the game folder
   (Steam → right-click Split/Second → Manage → Browse local files).
3. Start the game.

To uninstall, delete those two files (and the `hfr_logs` folder the mod creates).

## Configuration (`SplitSecondHFR.ini`)

| Option | Default | Meaning |
|---|---|---|
| `[General] Enabled` | 1 | 0 = the mod changes nothing (handy for comparing with the original game) |
| `[Timing] Mode` | 60 | 60 = native 60 Hz; 30 = original behaviour |
| `[Timing] PreciseLimiter` | 1 | exact frame pacing |
| `[Fixes] AI30Hz` | 1 | AI decisions at their original 30 Hz rate |
| `[Hotkeys] ToggleFps` | 0x77 (F8) | key that switches 30 ↔ 60; 0 disables it |
| `[Logging] FrameLog` | 0 | 1 = also write per-frame timings (`frames.csv`) |

## Logs and troubleshooting

Each session writes `hfr_logs\<date-time>\hfr.log` in the game folder: the version check, the active mode and an
FPS summary every 5 seconds. If something looks wrong, set `Enabled=0` to check whether the original game does the
same, and include the log when reporting an issue.

## Building from source

Requires Visual Studio 2022 (MSVC x86 toolset) and CMake ≥ 3.25.

```
cmake -S . -B build -G "Visual Studio 17 2022" -A Win32
cmake --build build --config Release
```

`cmake --build build --config Release --target deploy` copies the DLL (and the INI, if none is present) into the
game folder; set `-DGAME_DIR=...` when configuring if the game is not in the default Steam location.

## How it works

The game's main loop (`0x558880`) accumulates measured frame time and calls `Update(ticks)` once per 1/30 s with two
ticks of 1/60 s. The mod redirects the loop's three reads of the 1/30 s period to its own 1/60 s value and changes
the "2 ticks" constants to 1 — five small in-memory changes, each checked against the expected original bytes before
it is written and reverted when switching back to 30. Physics are stepped once per tick, so they run exactly as
before. The AI driver update is gated to every second tick via a pointer swap in its virtual table.

## License and credits

- The mod's source code is licensed under the **GNU General Public License v3.0** (see `LICENSE`).
- Bundled: [SafetyHook](https://github.com/cursey/safetyhook) (Boost Software License 1.0) and
  [Zydis](https://github.com/zyantific/zydis) (MIT) in `third_party/`.
- This is an unofficial fan project, not affiliated with Disney or Black Rock Studio. Split/Second is a trademark of
  its respective owner. You need your own copy of the game.
