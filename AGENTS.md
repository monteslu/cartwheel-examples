# Working on cartwheel-examples

Notes for anyone (people or coding agents) changing these games. README.md has the full build, run, test and CI
description; this file is the short version plus the rules a change must keep.

## Build and test

```sh
scripts/fetch-deps.sh                 # the engine SDKs pinned in scripts/deps.lock (sha256 checked)
TARGETS=native scripts/build.sh       # native builds: build/native/games/<game>/<app>
scripts/build.sh                      # native and the dual GL + WebGPU carts (build/cart-wgpu/carts/<app>.wasc)
scripts/build.sh bikemesa             # one app (any ninja target)

node tests/gate.mjs bikemesa-ride --hosts native                 # one gate, native, GL
node tests/gate.mjs bikemesa-ride --hosts native --gpu webgpu    # the same on WebGPU
node tests/gate.mjs bikemesa-ride                                # every host the gate names (carts too)
node deps/cartwheel-sdk/linux-x64/tools/check-prompts.mjs games/<game>   # the button prompt check
```

- Each game's gates are in `tests/gates.json`, with input scripts in `tests/<game>-*.json`. A change is done when the
  gates of the games it touches pass on GL and on WebGPU, natively and as a cart.
- Run GPU-heavy local work through `node deps/cartwheel-engine/tools/with-gpu-suite.mjs <name> <command>`: it takes the
  machine's GPU suite lock and keeps test windows off the desktop.
- The engine is not built here. Engine changes go to cartwheel-engine first, then `scripts/deps.lock` pins the new
  release (its source commit and every SDK line with its sha256).
- A new material state fails the gates and names itself: add it to the game's program table with
  `node deps/cartwheel-sdk/<host>/tools/programs.mjs <app> --build build/native` and commit the generated files.
- Generated assets come from each game's `tools/` scripts: change the script, rerun it and commit both.

## Showing controls

Every prompt that tells the player which button, key or touch control does something goes through the engine's
prompts (`cartwheel/prompt.h`, and the "Showing controls" section of the engine's `docs/guides/input.md`):

1. Never write a button's name or colour in text or art. Write `{jump}: hop a gap` and draw it with
   `cw_prompt_text`; the engine draws the action's binding for the device the player holds (a diamond of four dots
   with the one to press solid on a pad, a keycap on a keyboard, the on-screen control on a phone). The word "Start"
   is the one exception: every pad has that button, and on a keyboard the prompt shows its key.
2. Colours belong to the game, not the controller: a green lane is fine, "press green" is not.
3. When a design is about position (lanes, answer slots), lay it out by position and draw each slot with
   `{pad:west}`, `{pad:south}`, `{pad:east}`.
4. Text says what the button does ("checkpoint"); the glyph says which button.
5. Do not track the player's last device yourself: ask `cw_prompt_device_of`. Call prompts from update or render,
   never from the fixed tick.
6. One string serves every device. Keyboard hints such as "W / S: gas" move to the markup too.
7. Confirm is the south button and back is the east button on every pad.
8. Every pad has four face buttons, a d-pad, two shoulders, Start and Back. Sticks and analog triggers are not always
   there: anything a game needs must also have a binding on a button or the d-pad.

CI runs `tools/check-prompts.mjs` (shipped in the SDK) over the games listed in `PROMPT_GAMES` in
`.github/workflows/ci.yml`. It fails on text such as `A:`, `(B)`, `press X`, `LB`, `RT`; a line that really means
it carries `prompt-ok: <reason>` on it or just above. It cannot see letters drawn in art or in code, so review those
by hand. Glyphs need at least 16 canvas pixels at a pixel scale of 1 (18 or more for anything read in motion).
