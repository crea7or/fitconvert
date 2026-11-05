# FIT Telemetry Converter – Add Live Ride Data to Your Videos (No Re-Encoding Required)

**FIT Telemetry Converter** turns your **.FIT activity files** (from Garmin, Wahoo, Bryton, Suunto, or any FIT-compatible device) into **subtitles** that overlay your speed, distance, heart rate, power, cadence, temperature, gradient, altitude and GPS position on your ride or run videos — *without* re-encoding the video.

Record your action camera footage and your cycling computer or smartwatch activity separately, then synchronize them later for YouTube or local playback — without touching the video quality.

Don’t want to deal with command-line tools? Try the web version - it even supports YouTube sync! Click here for the web-based app - [telemetry-subtitles](https://crea7or.link/telemetry-subtitles/).

---

## What It Does

This tool reads data from your recorded **.FIT** file (fitness telemetry) and exports:
- **.VTT** subtitles – directly usable by YouTube or media players (VLC, MPC-HC, etc.)
- **.JSON** files – for web previews, advanced integration, or custom overlays

You can then:
- Upload the `.vtt` file to **YouTube** as captions (see [YouTube captions upload guide](https://support.google.com/youtube/answer/2734796))
- Or place the `.vtt` file next to your video locally for any modern player to show real-time telemetry while watching

No conversion or editing of the video file itself — subtitles simply "float" over playback.

---

## Why It's Useful

Whether you're a cyclist, runner, or triathlete, you often record two things:
1. A video of your ride or run
2. A `.fit` activity file with telemetry (speed, power, heart rate, etc.)

**FIT Telemetry Converter** combines those worlds by letting you overlay your performance data directly on your video timeline — perfect for:
- **YouTube uploads** of races or rides with live metrics
- **Training analysis** videos
- **Action camera footage** synced with fitness sensors

All with zero video re-encoding, so your footage stays 100% original quality.

---

## Quick Start

```bash
fitconvert -i ride.fit -o ride.vtt
```

Then rename `ride.vtt` to match your video (`ride.mp4` → `ride.vtt`), put it next to the video and play it.
Each second of the ride becomes one subtitle line:

```
WEBVTT

00:00:00.000 --> 00:00:01.000
 12.1 km/h   0.00 km  108❤️     0⚡  18°C   0.0﹪  150 m↑  42.269008°   42.701149°

00:00:01.000 --> 00:00:02.000
 12.7 km/h   0.01 km  109❤️    22⚡  18°C   0.0﹪  150 m↑  42.269024°   42.701182°
```

A value that the device did not record (e.g. cadence before the sensor connects) is left out of the line.

---

## Usage

```
usage: fitconvert -i input_file -o output_file -t output_type -m offset -s N -v units -d data_types -f format
```

### Parameters

| Flag | Long form | Description |
|------|-----------|-------------|
| `-i` | `--input` | Path to the `.fit` file, or `stdin` to read it from standard input (required) |
| `-o` | `--output` | Path to the output file, or `stdout` to write to standard output (required) |
| `-t` | `--type` | Output type: `vtt` or `json` (default `vtt`) |
| `-m` | `--offset` | Offset in milliseconds to sync the video and the `.fit` data, see [Synchronizing](#synchronizing-video-and-telemetry) (default `0`) |
| `-s` | `--smooth` | Insert N (0–5) interpolated values between records for smoother, more frequent updates (default `0`) |
| `-v` | `--values` | Units: `metric` (km/h, km, m, °C) or `imperial` (mph, mi, ft, °F) (default `metric`) |
| `-d` | `--data` | Data to process, comma delimited, in the order to display (default: `speed,distance,heartrate,power,cadence,temperature,gradient,altitude,latitude,longitude`) |
| `-f` | `--format` | Custom VTT line format, see [Custom format](#custom-vtt-format) (optional) |
| `-h` | `--help` | Show help |

The tool returns `0` on success and a non-zero exit code on error (unreadable or damaged file, invalid option or format).

### Synchronizing video and telemetry

- **Positive offset:** you started the video *after* starting the activity. Telemetry from `offset` ms into the activity is shown at the start of the video.
  For example, `-m 3000` if the video started 3 seconds after you pressed start on the device.
- **Negative offset:** you started the video *before* starting the activity. The first telemetry record is shown at `abs(offset)` ms of the video, and before that the subtitle says `< .fit data is not yet available >`.
  For example, `-m -3000` if the device was started 3 seconds after the video.

Tip: find a moment that is easy to spot in both (starting to move, a sprint, a stop) and compare the video time with the activity time.

### Smoothing

Devices usually record once per second. With `-s N` the tool inserts N interpolated steps between records, so `-s 3` updates the numbers every 250 ms.
Only values present in both neighbouring records are interpolated. During pauses (stopped timer, auto pause) the gap between records is long, so the interpolation is spread over the whole pause.

### Gradient

When the file has distance and altitude, the gradient is calculated by the tool (linear regression over the last 5 records) instead of using the value from the device.
While less than 5 m are covered (stops, very low speed) the last value is kept, and it is limited to ±50%.

---

## Custom VTT Format

`-f` defines what each subtitle line looks like. The format is a list of fields separated by `;`, each field is

```
label{name:spec}suffix
```

- `name` – one of the data names from `-d`
- `spec` – optional [fmt format specification](https://fmt.dev/latest/syntax/) (width, precision, sign...), `{name}` uses the default formatting
- `label` and `suffix` – any UTF-8 text (emoji too) before and after the value; nothing is added between fields, so include the spaces yourself.
  On Windows this needs Windows 10 version 1903 or newer, older versions convert the arguments to the system code page.

| Name | Value | Metric | Imperial |
|------|-------|--------|----------|
| `speed` | decimal | km/h | mph |
| `distance` | decimal | km | mi |
| `altitude` | decimal (whole units) | m | ft |
| `gradient` | decimal | % | % |
| `latitude`, `longitude` | decimal | degrees | degrees |
| `heartrate` | integer | bpm | bpm |
| `power` | integer | W | W |
| `cadence` | integer | rpm | rpm |
| `temperature` | integer | °C | °F |

Decimal values take a precision (`{speed:4.1f}`), integer values do not (`{heartrate:3}`, not `{heartrate:3.1f}`).
An invalid format is reported as an error before the conversion starts. A field without a value in a record is left out of that line.

Examples:

```bash
fitconvert -i ride.fit -o ride.vtt -f "{speed:4.1f} km/h;  HR {heartrate};  {power} W;  {gradient:+.1f}%"
```
```
12.1 km/h  HR 108  0 W  +0.0%
```

```bash
fitconvert -i ride.fit -o ride.vtt -f "🚴 {speed:.1f};  ❤️ {heartrate};  ⛰️ {altitude:.0f} m"
```
```
🚴 12.1  ❤️ 108  ⛰️ 150 m
```

`-d` still selects which values are read from the file, so a format may only use names listed in `-d` (all by default).

---

## JSON Output

`-t json` writes all records with short keys, for web players and custom overlays:

```json
{
  "records": [
    {"f": 0, "s": 12.09, "h": 108},
    {"f": 1000, "s": 12.66, "h": 109}
  ],
  "types": {"speed": 1, "distance": 2, "heartrate": 4, "...": 0},
  "fields": {"speed": "s", "distance": "d", "heartrate": "h", "...": ""},
  "usedTypes": 5,
  "timestamp": 1119406015000,
  "offset": 0,
  "units": "metric"
}
```

- `records` – one object per record (and per interpolated step with `-s`); `f` is the video time in milliseconds (offset applied), the other keys are listed in `fields`. A key is missing when the record has no such value.
- `fields` – data name → record key: `s` speed, `d` distance, `h` heart rate, `a` altitude, `p` power, `c` cadence, `t` temperature, `g` gradient, `u` latitude, `o` longitude
- `types` / `usedTypes` – bit mask of every data type / of the types present in the file
- `timestamp` – activity start in milliseconds since the FIT epoch (1989-12-31 00:00:00 UTC, not the Unix epoch), plus a positive offset
- `units` – `metric` or `imperial`, the units are the same as in the table above

Records are not evenly spaced (pauses, smart recording), so always use `f` for timing.

---

## Example Workflow

1. Record a ride with your Garmin (or similar) and your action camera.
2. Export the `.fit` file from your device.
3. Run:
   ```bash
   fitconvert -i ride.fit -o ride.vtt -m 3000 -s 3
   ```
   *(The video started 3 seconds after the activity; values are updated 4 times per second)*
4. Rename `ride.vtt` to the video name with the `.vtt` extension, put it next to the video and play it — or upload it to YouTube as subtitles.

The tool also works in pipes:

```bash
cat ride.fit | fitconvert -i stdin -o stdout -t json > ride.json
```

---

## Optional: Embed Subtitles into a Video

You can also "bake" subtitles into your MP4 file without re-encoding:

```bash
ffmpeg -i video.mp4 -i ride.vtt -c copy -c:s mov_text output.mp4
```

This only copies the streams, so it is fast and keeps the original quality.

---

## Example Results

**Local playback (Media Player Classic):**
![Sample result while playing locally](local_video.jpg)

**YouTube upload:**
[Watch on YouTube](https://www.youtube.com/watch?v=fV2acJ4XffM)
![Sample result at YouTube](youtube.video.jpg)

---

## Building from Source

Works on Windows and Linux.
Requirements:
- a C++20 compiler (`std::to_chars` for floating point is required: Visual Studio 2022, GCC 11+)
- [CMake](https://cmake.org/) 3.31+
- [Conan](https://conan.io/) 2 for the dependencies (fmt, spdlog, rapidjson, cxxopts, GoogleTest, Google Benchmark)

Tested with:
- Visual Studio 2019/2022 (Open as Folder)
- GCC on Ubuntu

Visual Studio 2022:
```bash
conan install . -s build_type=Release --build=missing
conan install . -s build_type=Debug --build=missing
cmake -B build -S . -G "Visual Studio 17 2022" -DCMAKE_TOOLCHAIN_FILE=build/generators/conan_toolchain.cmake
cmake --build build --config Release
# or open build\fitconvert.sln
```

Linux (presets generated by Conan):
```bash
conan install . -s build_type=Release --build=missing
cmake --preset conan-release
cmake --build --preset conan-release
```

Targets: `fitconvert` (the tool), `fitconvert-tests`, `fitconvert-bench`.

### Tests

Tests use GoogleTest and need no data files: `fit_writer.h` builds synthetic `.fit` files in memory.

```bash
cmake --build build --config Release --target fitconvert-tests
build/Release/fitconvert-tests
# or with CTest
ctest --test-dir build -C Release
```

### Coverage

Configure a separate build with `-DFITCONVERT_COVERAGE=ON`, using clang/clang-cl (source based coverage) or GCC (gcov).
Example for clang-cl and Ninja on Windows, from a Visual Studio developer prompt, using the packages installed by Conan for the Release build:

```bash
cmake -B build-cov -S . -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_C_COMPILER=clang-cl -DCMAKE_CXX_COMPILER=clang-cl \
  -DCMAKE_PREFIX_PATH=build/generators -DCMAKE_MODULE_PATH=build/generators \
  -DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded \
  "-DCMAKE_CXX_FLAGS=/EHsc -fdelayed-template-parsing" \
  -DFITCONVERT_COVERAGE=ON
cmake --build build-cov --target fitconvert-tests
LLVM_PROFILE_FILE=tests.profraw build-cov/fitconvert-tests
llvm-profdata merge -sparse tests.profraw -o tests.profdata
llvm-cov report build-cov/fitconvert-tests -instr-profile=tests.profdata parser.cpp datasource.cpp
```

Notes:
- the Conan toolchain file is not used here because it selects the Visual Studio generator settings
- rapidjson 1.1.0 does not compile with clang 19+ out of the box, `-fdelayed-template-parsing` works around it

### Benchmarks

Benchmarks use Google Benchmark on synthetic 1 hour and 10 hour rides, plus micro benchmarks of the per record code.
A real activity is benchmarked too when given with `--fit_file=path` (or the `FITCONVERT_BENCH_FILE` environment variable, or `./300.fit` if it exists):

```bash
build/Release/fitconvert-bench --fit_file=ride.fit
build/Release/fitconvert-bench --benchmark_filter=BM_ConvertVtt --benchmark_repetitions=5
```

---

## License

MIT License, Copyright © 2025 **pavel.sokolov@gmail.com** / **CEZEO Software Ltd.**, see [LICENSE](LICENSE).

Part of this repository includes Garmin's [FIT SDK](https://developer.garmin.com/fit/download/) (in the `fitsdk/` folder), distributed under its respective license.

---

## About This Project

FIT Telemetry Converter is part of the **CEZEO Software** ecosystem — focused on creating efficient, portable, and open tools.

```
      .:+oooooooooooooooooooooooooooooooooooooo: `/ooooooooooo/` :ooooo+/-`
   `+dCEZEOCEZEOCEZEOCEZEOCEZEOCEZEOCEZEOCEZEOEZshCEZEOCEZEOEZ#doCEZEOEZEZNs.
  :CEZEON#ddddddddddddddddddddddddddddddNCEZEO#h.:hdddddddddddh/.yddddCEZEO#N+
 :CEZEO+.        .-----------.`       `+CEZEOd/   .-----------.        `:CEZEO/
 CEZEO/         :CEZEOCEZEOEZNd.    `/dCEZEO+`   sNCEZEOCEZEO#Ny         -CEZEO
 CEZEO/         :#NCEZEOCEZEONd.   :hCEZEOo`     oNCEZEOCEZEO#Ny         -CEZEO
 :CEZEOo.`       `-----------.`  -yNEZ#Ns.       `.-----------.`       `/CEZEO/
  :CEZEONCEZEOd/.ydCEZEOCEZEOdo.sNCEZEOCEZEOCEZEOCEZEOCEZEOCEZEOCEZEOEZNEZEZN+
   `+dCEZEOEZEZdoCEZEOCEZEOEZ#N+CEZEOCEZEOCEZEOCEZEOCEZEOCEZEOCEZEOCEZEOEZ#s.
      .:+ooooo/` :+oooooooooo+. .+ooooooooooooooooooooooooooooooooooooo+/.
 C E Z E O  S O F T W A R E (c) 2025   FIT telemetry to subtitles converter
```
