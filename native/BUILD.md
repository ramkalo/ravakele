# Building Ravakele natively

The browser synth in the folder above runs as it is. This folder builds the same engine (`../engine`) for your
machine: a quick offline check, a CLAP plugin, and with clap-wrapper VST3, AUv2 and a standalone app.

**Status.** `render.cpp` is built and run by Lemnos's own tests. `plugin.cpp` and `CMakeLists.txt` are written
against CLAP 1.x and clap-wrapper's clap-first CMake, but Lemnos could not compile them: it does not ship or download
the CLAP headers. Expect to fix small things the first time.

In a plugin, hosts show their own generic controls for the params (one per engine param, named by its address, like
`DSP002.cutoff`). Your designed interface is not in the plugin yet.

## 1. Check the engine (a C++17 compiler is all it needs)

```bash
clang++ -std=c++17 -O2 -I../engine ../engine/lemnos_engine.cpp render.cpp -o render
```

```bash
./render note.wav 48 2
```

It writes `note.wav`: two seconds of C3, let go at 1.5 s. It also says if the synth is louder than full scale.

## 2. A CLAP plugin

You need CMake (3.21+) and the CLAP headers.

1. Put the CLAP repository (github.com/free-audio/clap) in `third_party/clap`.
2. Configure and build:

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
```

```bash
cmake --build build --config Release --target ravakele_clap
```

3. Copy the `.clap` from `build/` into your plugin folder (macOS: `~/Library/Audio/Plug-Ins/CLAP`; Windows:
   `%COMMONPROGRAMFILES%\CLAP`; Linux: `~/.clap`), then rescan in a CLAP host (Bitwig, Reaper, …).

## 3. VST3, AUv2 and a standalone app

1. Also put clap-wrapper (github.com/free-audio/clap-wrapper) in `third_party/clap-wrapper`. With
   `CLAP_WRAPPER_DOWNLOAD_DEPENDENCIES` on (as CMakeLists.txt sets it) it fetches the VST3 and AU SDKs while
   configuring.
2. Configure with the wrapped formats and build:

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release -DLEMNOS_WRAPPED=ON
```

```bash
cmake --build build --config Release
```

3. Install the `.vst3` and `.component` like any plugin. The standalone app opens on its own.

## 4. Shipping

- **macOS:** sign with your Developer ID (`codesign --deep --options runtime --sign "Developer ID Application: …"`),
  then notarize (`xcrun notarytool submit … --wait`) and staple. Without it, other people's Macs refuse the plugin.
- **Windows:** sign the binaries if you distribute them widely.
- Change the plugin's id (`lemnos.ravakele`) and vendor in `plugin.cpp` to your own before releasing.

## The engine's C interface

`../engine/lemnos_engine.h` is all a host needs: create, prepare, params by index or address, notes, MIDI, process,
and state as JSON. `plugin.cpp` is one host of it; the browser's `worklet.js` is another.
