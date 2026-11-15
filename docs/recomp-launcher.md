# RecompLauncher

RecompLauncher is a third-party launcher (elripalda/RecompLauncher) that lists recompiled games, downloads their
releases and runs them. It lists DKB Recomp since [#213](https://github.com/Lucas-blip-png/dkb-pc-recomp/pull/213).
It supports the Linux and macOS packages. The Windows installer is separate ([installer.md](installer.md)).

## How it sets the game up

The package carries `recomplauncher.json` (source: `tools/release/recomplauncher.json`). The launcher reads it and:

1. Asks for your own copy of the game: a `.nup` file, or a folder with `exefs/` and `romfs/`. Base 1.0.0 and update
   3.2.0 are both accepted.
2. Runs the prepare step, `setup.sh --prepare --source <copy> --dest <package folder>/game` (`setup.command` on
   macOS). The step builds the game on your computer and takes a while. The profile allows 4 hours.
3. Starts `play.sh`, which runs `game/dkb` from the `game/` folder. If the game is not built yet, it says so and exits.
4. Backs up `game/profile` (the saves).

`--source PATH` stands for `--nup` (a file) or `--exefs` and `--romfs` (a folder). `--prepare` prints
`progress <0..1> <text>` lines for the launcher instead of JSON events. Running the step again resumes or updates the
recorded install. More in [installer.md](installer.md), "Bananza Linux and macOS packages".

If a system package is missing, the step stops with exit code 1 before Python starts. Run `./setup.sh --check` in a
terminal inside the package folder: it names what is missing and the command that installs it.

## Where the output and logs are

- The built game is in `game/` inside the package folder. Saves are in `game/profile`.
- The game log: one player found it at
  `~/.local/share/com.recomplauncher.app/game-output/dkb-pc-recomp.log` on Linux
  ([#218](https://github.com/Lucas-blip-png/dkb-pc-recomp/issues/218)). The launcher overwrites it at each start, so
  copy it before you start the game again.
- The setup logs: the installer writes one file per step under `logs/` in its temp folder, which is
  `build/install-temp` in the package folder.

## Reporting a bug

Use the issue form (crash, graphics or bug). Add:

- the release (the `VERSION` file in the package folder) and the game version, 1.0.0 or 3.2.0;
- GPU, driver and operating system, and the RecompLauncher version;
- what you did just before it stopped;
- the log file above, as an attachment. For a failed setup, send the last lines the launcher showed and the step logs.

If the game crashed, the log names the faulting function. Never attach game files, keys, builds or the shader cache.
See [dkb-testing.md](dkb-testing.md) for the rest of the checklist.
