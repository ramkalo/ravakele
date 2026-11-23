# Ravakele

A synth exported from Lemnos. It runs as it is: no build step, no agent.

## Play it

Serve this folder with any static web server and open it in a browser (it must be served: browsers do not run audio code from files opened straight from disk). For example, from this folder:

```bash
python3 -m http.server 8000
```

then open http://localhost:8000. Click (or press a key) to start the sound. Play with the controls, ${keyboardHow(project, library)}, or a MIDI keyboard (in browsers with Web MIDI).

The page keeps what the player changes (saved patches and palette presets, and the sound as it was left) in the browser's storage for this synth, so it is there when the page opens again. Clearing the site's data starts it as shipped; a patch-presets .json file imports back on the Palette card.

## Put it online

Upload this folder to any static host (GitHub Pages, Netlify, Cloudflare Pages, your own server). Serve `engine.wasm` as `application/wasm` (most hosts do).

This synth offers updates: an open page reads `version.json` from the host and, when it names another build than the page's own, shows a notice whose Update button reloads the page. To publish an update, export again and upload the whole folder over the old one.

## What is in it

- `index.html`, `layout.css`, `runtime.css`: the page (layout.css draws the components, runtime.css frames the sheet and mod mode, the same files Test mode loads).
- `player.js`: the player (Lemnos's renderer and controls, library/runtime/player.js), bundled.
- `worklet.js`: the engine's audio thread (library/runtime/worklet.js), bundled.
- `engine.wasm`: the engine, compiled from `engine/` with Emscripten.
- `project.json`: the project: pages, controls, blocks, mod routes. The player reads it.
- `issues.json`: what the engine left out (the list below); the page shows it too.
- `library.json`: the manifests of the parts the blocks use and the navigation profile, which the runtime reads (library/runtime/params.js, keyboard.js).
- `version.json`: the build this folder is (a hash of the files above), which a page that is open compares with its own (library/runtime/update-check.js).
- `assets/`: images and fonts the pages use.
- `block-info.md`: the info text mod mode's MORE page shows for each block, by DSP part id (library/runtime/block-info.js reads it when MORE is first opened).
- `engine/`: the engine's C++ source. `Engine.hpp` is generated from the project (do not edit it: change the project in Lemnos and export again); `runtime.hpp`, `lemnos_engine.h` and `lemnos_engine.cpp` are the fixed runtime; `dsp/` is the DSP library. `lemnos_engine.h` is the C interface any host calls.
- `native/`: building it as a plugin or app; see `native/BUILD.md`.

## Rebuild engine.wasm

With Emscripten installed, from this folder:

```bash
em++ -std=c++17 -O2 -fno-exceptions -Iengine engine/lemnos_engine.cpp -o engine.wasm --no-entry -sSTANDALONE_WASM -sINITIAL_MEMORY=16777216 -sALLOW_MEMORY_GROWTH=1 -sEXPORTED_FUNCTIONS=@native/exports.json
```

## Left out of the engine

Nothing: every block and arrow is built.

## Not in this export yet

What Test mode does with this project that the export does not do yet (Lemnos's gap register, library/runtime/gaps.js); the page lists it too.

Nothing: the export plays as Test mode does.
