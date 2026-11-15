# Running on a weak PC

Measurements from `tools/lowend_bench.py`, which runs `dkb.exe` on the first N logical processors with the perf trace and
the draw profile on, and prints one table. This page is the baseline the low-end work is compared against.

## Baseline

Bananza 1.0.0, first level, the scripted input of about 2.4 minutes, 70 s per run, RTX 5060 Ti, 16 GB of RAM, before the
descriptor memos of pull request 181. This route is light: it does not show the cost of an open area (a 3070 Ti Laptop
reports 31 to 35 fps there with about 8,300 draws a frame), so treat it as a floor for the CPU limit, not a ceiling.

| Logical processors | Mean fps | Steady fps | Worst window (ms) | Replay avg (ms) | GPU avg (ms) | Commit max (MB) | Working set max (MB) |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 16 | 57.0 | 60.0 | 941 | 3.89 | 1.80 | 14,306 | 5,837 |
| 8 | 55.9 | 60.0 | 938 | 6.36 | 3.02 | 14,133 | 5,704 |
| 4 | 52.7 | 58.9 | 997 | 6.61 | 3.11 | 14,087 | 5,564 |
| 2 | 42.9 | 55.6 | 1,249 | 13.12 | 1.87 | 14,075 | 5,636 |

- Down to 4 logical processors the steady rate holds near 60 fps. At 2 it falls to about 56 fps and the replay thread
  takes 13 ms a frame.
- The worst window is about one second at every count. It falls in the first seconds, where shaders are translated.
- Process commit is 14 GB at every count, so memory, not the CPU, is what a 16 GB machine runs out of first.

## Where the commit goes

`TOTK_MEM_REPORT=N` logs the breakdown every N frames and at exit. Around frame 2400 of the first level, of about 14.4 GiB:

- guest heap and stack: 9.2 GiB, of which only about 4.2 GiB is resident in the working set;
- GPU device-local memory and mirrors: 2.6 to 5.1 GiB, growing during the run;
- the exe image: about 0.5 GiB.

`TOTK_APP_POOL` below 8 GiB makes the game crash at start (access violation reading 0xc8), so it is not a way to save memory.

## Thread knobs on 2 and 4 logical processors

Same route, 60 s runs, one knob changed from the defaults at a time. Repeated runs of the same setting differ by about
1 fps in the mean and 0.5 ms in the replay average.

| Logical processors | Setting | Mean fps | Steady fps | Replay avg (ms) |
| ---: | --- | ---: | ---: | ---: |
| 2 | defaults | 46.5 to 42.9 | 55.6 to 55.7 | 11.8 to 13.1 |
| 2 | `VK_RECORDER=0` | 48.2 to 48.4 | 58.9 to 60.0 | 5.9 to 6.6 |
| 2 | `REPLAY_ASYNC=1` | 45.6 | 56.1 | 12.4 |
| 2 | `SPLIT_DRAWS=0` | 46.4 | 57.4 | 9.2 |
| 2 | `SPLIT_DRAWS=256` | 46.2 | 57.5 | 12.4 |
| 2 | `FREE_CORES=1` | the run stopped at frame 1353 | | |
| 4 | defaults | 52.7 to 53.1 | 58.9 to 59.0 | 6.3 to 6.6 |
| 4 | `VK_RECORDER=0` | 50.3 | 59.0 | 6.3 |

- With 2 logical processors the recorder thread competes with the replay for a core. Turning it off halves the replay
  time and brings the steady rate to 60 fps. With 4 it changes nothing.
- `FREE_CORES=1` with 2 logical processors stopped producing frames after about 25 s. It is off by default and stays off.
- The worst window stays near 1 to 1.7 s whatever the setting: it is the shader translation at the start, not a thread
  setting.

This route is light, so these are not yet reasons to change a default. A low-end profile would turn the recorder off at
2 logical processors once a heavy scene confirms it.
