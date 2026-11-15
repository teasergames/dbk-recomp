DKB Recomp 0.3.0: a static recompilation of *Donkey Kong Bananza* (Switch 2) to a native Windows exe with a Vulkan renderer. It is still experimental (0.x): it plays the tutorial, its cutscene and the first stage on recent NVIDIA cards, and it can crash.

**The packages hold no game data, and no release ever will.** You build the game on your own PC from your own dump of **1.0.0 or the 3.2.0 update**, given as a `.nup` file or as ExeFS and RomFS folders. Other updates are not supported.

## What is new since 0.2.0

- **Settings menu.** Settings are split into Display and Graphics tabs, controller and mouse-and-keyboard settings are separate rows, dropdowns and steppers scroll and behave better, and the binding help is corrected. Two wider resolution presets for ultrawide screens were added.
- **HDR (experimental, off by default).** An HDR10 output with `hdr`, `hdr_peak_nits` and `hdr_game_nits` settings, and an experiment that lets the game render its own HDR10 picture (`DKB_HDR_NATIVE=1`, 3.2.0 with `hdr = 1`). Native HDR highlights are taken up to the display's peak, because the game's own tonemap stops at about 1.36 times paper white.
- **DLSS (experimental, RTX only).** DLSS is now temporal: sub-pixel jitter, depth and camera-only motion vectors reach NGX. DLSS output is the game's own 3D resolution, so `render_scale` does nothing on Bananza and the HUD shows the upscaler's real sizes.
- **High resolutions.** 3840x2160 and 5120x1440 no longer overflow the game's fixed render-target pool, which now grows with the output size (#152). 3840x2160 ran 90 s at 60 fps on 3.2.0 without a crash.
- **Update 3.2.0.** DLSS works there with the same shaders as on 1.0.0, and a radial blur that stayed on screen above 60 fps after a smash is released (#158).
- **Frame generation (experimental, off by default).** `FRAME_GEN=2x` shows an interpolated frame half an interval before each real one, only in mailbox or immediate present mode; it skips a made frame on a hitch or a crop change. See `docs/dkb-framegen.md`.
- **Performance.** Descriptor memos each sit behind their own switch (those that did not pay were removed), the draw state is one context per thread, uniform buffers can be dynamic (`TOTK_DYN_UBO`, off by default until validated on AMD and Intel), range lists use an inlined merge sort, and about 1 ms was cut from the heavy-scene replay. Helper threads that translate long runs of draws (`TOTK_REPLAY_PAR`) and sliced big passes (`DKB_REPLAY_THREADS`) exist but are off by default. An unlimited frame limit (`TOTK_FPS_LIMIT=unlimited`) was added (#113).
- **Memory.** `TOTK_HEAP_LAZY=1` (Windows, off by default) commits the guest heap on first touch: the commit of the first level drops from about 14.5 GiB to about 9.3 GiB. It relies on spin-lock patches that are now in the 1.0.0 and 3.2.0 patch tables; the same patches fix a spin-lock livelock (#126).
- **Low-end bench.** `tools/lowend_bench.py` runs the game on the first N logical processors and prints one table. On the first level it held 60, 60, 58.9 and 55.6 fps on 16, 8, 4 and 2 logical processors (`docs/dkb-lowend.md`).
- **Installer.** A work folder path with shell-special characters is refused before the build starts (#189), and the work folder is named without them. The installer also tells you when a newer release exists (`--no-update-check` turns it off), and shows a clear message when Windows asks for elevation.
- **macOS and Metal.** The macOS build is fixed (a function defined twice), and the host side of the graphics layer now goes through `GfxBackend` to prepare a Metal back end. Play on macOS is still not verified.
- **Diagnostics.** `TOTK_SVC_SLOW=1` logs guest service calls that block for 100 ms or more, slow RomFS reads are logged, `TOTK_MEM_REPORT=N` reports where the commit goes, `DKB_DRAW_PROFILE` gives replay time by section, and two AMD device-lost aids exist (`TOTK_FIX_VS_FAULT=all`, `TOTK_CRUMB_BIND=1`).
- **Scripted input.** Wait markers and a replay clock that lands at the same game time on every run.

## Download

| Package | For | Status |
|---|---|---|
| `DKB-Setup-<version>-windows.zip` | Windows 10/11 x64 | Supported. Carries its own tools and the installer window: unpack, run `DKB-Setup.exe`, pick your dump. |
| `DKB-Setup-<version>-linux-x86_64.tar.gz` | Linux x86-64, kernel 6.7 or later | Preview. Source package with `setup.sh`: you install the system packages it lists (`./setup.sh --check`), then it runs the command-line installer. |
| `DKB-Setup-<version>-macos-arm64.zip` | macOS 14.4 or later, Apple silicon | Preview. Same idea with `setup.command`. Play is not verified and the destructible terrain is missing (Metal has no geometry stage). |

## Requirements (Windows)

- NVIDIA GPU with a recent driver (tested on an RTX 5060 Ti and an RTX 3070 Ti Laptop). AMD and Intel are experimental: device loss has been reported (#3, #84, #99, #124, #142, #193).
- 16 GB of RAM plus a large page file (the process commits about 14 GB, or about 9 GB with `TOTK_HEAP_LAZY=1`), 32 GB is comfortable.
- A controller is recommended.

## Known issues

- Device lost on several AMD cards without a fault address (#3, #84, #99, #124, #142, #193), and a GPU hang on an RX 6600 under RADV (#122).
- A crash on Linux when walking into the tunnel from the Lagoon Layer to the Hilltop Layer (#126).
- A frame limit below 60 on 1.0.0 only holds with "Game speed follows the clock" on; without it the game keeps its own timing (#118).
- A reproducible device lost in the Glacier layer rock ride on an RTX 5090 Laptop (#196).
- The validation layer reports 10 errors from a depth-compare sampler on an R32_SFLOAT view; no visible effect has been reported.
- The first visit to an area can stutter. Open-area performance is 31 to 35 fps on an RTX 3070 Ti Laptop; a heavy scene on an RTX 5060 Ti measured 51.8 fps with a 1% low of 20.6 fps, so heavy scenes can still fall under 60 fps.
- The cutscene at the end of the tutorial lost the device on NVIDIA before a workaround (#75); it is on by default and the cause is not known.
- The NVIDIA workaround for #9 skips light quads, which hides some particles (#62).
- Checkerboard textures (#5), one-sided sound in the first 20 seconds (#63), #76, #83, #112 and unrecorded NVN 2 calls (#6).

## Reporting a bug

Open an issue with the log (`dkb.log`), the build line at its top (`dkb 0.3.0+<commit>`), your GPU and driver, and what you did before it failed. **Never share generated files, keys or screenshots that show game content.**

Roadmap: #10. Testers: #3 (other GPUs) and #7 (play further).
