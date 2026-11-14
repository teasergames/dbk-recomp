# RecompLauncher

The Windows DKB setup ZIP includes `recomplauncher.json`. RecompLauncher reads this
release-owned profile without a built-in launcher integration. Use a version with
named Easy Setup inputs (0.90.0-beta.24 or newer).

1. Install the DKB setup release in RecompLauncher, or unpack it and add its folder
   through Settings → Developers → Add a build.
2. In Easy Setup, select your **base 1.0.0 ExeFS** folder (`main`, `sdk`, `rtld`,
   `main.npdm`) and **RomFS** folder (loose files, or `romfs.bin` and `index.json`).
3. Start setup. It checks the pinned executable hashes, builds the game and prepares
   shaders using the existing [Bananza installer](installer.md). Allow 20–30 minutes
   or longer on slower CPUs. Setup has a four-hour timeout.
4. Press Play to start `game/dkb.exe` directly. Display, audio, controller and language
   settings are available on the game's page and in Big Picture.

The release contains only tools and source. `installer.recomplauncher` runs without a
window and adapts the installer's events to `progress <0..1> <English text>` lines.
Compiler and shader diagnostics stay in the existing setup workspace's `logs/`;
the setup progress names that log when a tool fails. No dependencies are added.
On Windows, a kill-on-close job keeps all setup workers tied to the setup process,
so cancelling setup or closing the launcher also stops compilation and translation.

The generated portable game lives in `game/`, separate from the release's tools and
manifest, because the installer requires an empty or previously owned destination.
Failed or cancelled setup keeps its completed steps for retry. Run Easy Setup again
after a release update to check the inputs and rebuild changed code. Keep your source
folders available for retries and updates. The installer preserves `game/profile/`
and settings. RecompLauncher tracks the setup's selected paths, not a verified disc
set; RomFS packing checks establish packing integrity, not original dump authenticity.

Settings edit `game/dkb.ini`, which the runtime reads and updates itself. Nothing in
the release overwrites that file on update. Automatic window mode starts full screen
when `RECOMPLAUNCHER=1` and `RECOMPLAUNCHER_MODE=bigpicture`; otherwise the normal
runtime/device defaults apply. An explicit window mode in the settings file or
`DKB_FULLSCREEN`/`TOTK_FULLSCREEN` overrides this session default. It is never saved
automatically. SDL's existing automatic pad layout handles controller labels without
forcing a glyph set or enabling background input.

Backups/cloud saves contain only
`game/profile/` and `game/dkb.ini`; source dumps, tools, build state and shader caches
are excluded. Open saves and Open logs lead to the runtime's folders. Vulkan is
declared for the launcher's Looks feature; ReShade has not been tested with Bananza.

Game timing remains experimental and off by default; its frame-limit control is
disabled until experimental timing is enabled. No TotK-only gameplay patches or Xbox
prompt texture generation are advertised. No NSP/XCI, keys, updates, DLC, firmware or
Linux support is added. Bananza remains experimental; integration does not establish
that the game is playable from beginning to end.

## Checks without game files

```
python tests/recomplauncher_test.py
python tests/installer_bananza_test.py
python tests/recomplauncher_cfg_test.py --cc clang
python tools/check_content.py --staged
```

The release-source check assembles both Bananza and TotK source folders without
downloading a toolchain, and checks that only Bananza receives the profile. Installer
tests use synthetic input and build tools for progress, failure, cancellation and
resume. Full release assembly, real-dump setup, GPU execution and a live launcher
session are separate checks requiring the appropriate tools and the player's dump.
