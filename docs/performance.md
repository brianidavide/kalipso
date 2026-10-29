# Performance

Measured on 2026-10-29 with the commands below, on an AMD Ryzen 5 PRO 6650U (12 threads), Debian 13, Qt 6.8.2, Release build, X11 (Xvfb, software rendering). Numbers on your machine will differ; the commands let you reproduce them.

## Results

| What | Result |
| --- | --- |
| Standalone app, editor and reader open, idle | 0.0% of one CPU core · 44 MiB PSS · 62 MiB RSS |
| Standalone app, steady scrolling at 140 words/min | 1.0% of one CPU core · 45–50 MiB PSS |
| `kalipso-engine` companion, idle | 0.0% of one CPU core · 10 MiB PSS · 17 MiB RSS |
| Tracker: a recognized phrase → new reading position (C++) | about 35 µs median for 1,000–50,000-word scripts |
| Tracker: worst case, off-script words found all over the script (C++) | 0.06 ms (1,000 words) · 0.11 ms (10,000) · 0.38 ms (50,000) median |
| Tracker: same two cases in JavaScript (Node 24) | 0.3–0.4 ms · up to 2 ms at 50,000 words |
| Linux `.deb` (app + companion) | 422 KiB download, 1 MiB installed |
| Plasma widget / GNOME extension / KWin script | 34 KiB / 36 KiB / 15 KiB |

CPU percentages are three 10-second runs each, after a one-second warm-up; all three runs agreed within 0.01 percentage points. PSS counts shared libraries proportionally and is the better estimate of what Kalipso adds to memory use.

The tracker's whole-script search, which lets the reader recover after you skip ahead or go back, used to scan every word and took 18 ms per recognized phrase on a 50,000-word script. It now only checks the places where the rarest spoken words occur.

## Not measured yet

- **Speech recognition** itself: model load time, CPU while listening and the delay from a spoken word to the highlight depend on the engine, the model and the machine. `benchmarks/asr/` contains the harness (`README.md` there); it needs the models and recorded test audio, which are not in the repository.
- **Plasma and GNOME** frontends: their cost is part of the desktop shell's process and needs a profiler on a real session.
- **macOS and Windows**, GPU-accelerated rendering, high-DPI and multi-monitor setups.

## Reproduce

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build

# Tracker latency, JSON per script size (microseconds)
build/tracker_benchmark

# CPU and memory of the app (process tree, PSS/RSS from /proc)
python3 tools/measure-process.py --seconds 10 -- build/kalipso --demo --prompter
python3 tools/measure-process.py --seconds 10 -- build/kalipso --demo --steady
dbus-run-session -- python3 tools/measure-process.py --seconds 10 -- build/kalipso-engine --serve
```

`--demo` uses a sample script and never touches your saved scripts. Run under `xvfb-run -a env QT_QPA_PLATFORM=xcb` for a headless X11 server.
