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

## Newcomer and idle-seat gates

Bots and soaks know the map and always press something, so they cannot catch a game that only someone who already
knows it can finish, or one that a second pad put down on the table holds hostage. Every game gets two gates of its
own, both on the built cart through romdev, on GL and on WebGPU:

- **Newcomer gate.** From power-on to the end of the first race, round, run or match, driven only by what the screen
  shows: the title's prompts, the controls line, objective text, markers and arrows (read from the debug fields the
  game draws them from), never a route the test knows in advance, and as slowly as a first-timer plays. Its control
  turns the cue off or brings the old rule back (a `sw` debug switch) and must leave the player short of the goal.
- **Idle-seat gate.** A second pad joins, then never touches anything. The first player must still reach play and
  finish: no ready check or ride pick that waits for ever, no split view without its controls line, no round that
  cannot end while the idle seat lives, no shared camera that holds the player back. Its control (a `sw` switch that
  brings the old wait back) must fail.

Pause, resume, retry and quit belong in one of the two. The existing ones show the shape:
`tests/bikemesa-newcomer.mjs`, `tests/derby-newcomer.mjs`, `tests/pugball-newcomer.mjs`, `tests/racer-newcomer.mjs`
and `tests/racer-slow-finish.mjs`. Each tears its romdev host down when it exits. Look at the frames they save: a
frame hash cannot show that the way is readable. A new game is not done until both gates pass.

## Graphics presets and launcher art

- Every game has the engine's five presets (Very Low first). A test of a preset or a setting value goes by name and by
  range (`q <= CW_QUALITY_LOW`, `CW_GFX_SHADOWS_MINIMAL`), never by number, so Very Low takes the cheapest branch. A game
  lever takes `cw_quality_lever_presets` with all five values. An array indexed by `cw_quality` has `CW_QUALITY_COUNT` entries.
- A new material, scene or level runs its game's quality sweep (`tests/<game>-quality.mjs`, inside a GPU suite, `--shard i/n`
  to keep each suite under the lock's 15 minutes): every preset on GL and WebGPU, 0 missing programs, 0 skipped draws.
  A state it reports goes into `games/<game>/programs/<game>.txt` (regenerate with `programs.mjs --table`).
- A new game gets `games/<game>/art` (wasmcart docs/art.md: icon, cover with the name, background, logo, title,
  3 to 6 labelled screenshots, all real renders), the art, description and author keys in its `project.cwproj`, a line
  in `tests/quality-range-scenes.json` and `tests/quality-grounding.mjs`, and its name in `tests/cart-art.mjs`.

## Readable text

- Body text (HUD values, menus, hints, captions) is at least 26 px at 1080 lines (14 * s on the games' 540-line
  canvas scale), nothing under 18 px; art text (a title logo) sets `.fixed_size`. Every gate runs with the engine's
  `textFloor` check.
- Lay text out from its measured size (cw_font_measure, cw_prompt_text_size, cw_text_drawn_size), never fixed offsets: the
  player's Text size goes to 200%. Measure prompts in update or render, never in the fixed tick.
- Each game keeps Text size and Text background rows in its own settings and `cw_text_scale_saved()` in its save.
- `tests/<game>-text.json` lists the screens; `CW_PROJECT_ROOT=$PWD node deps/cartwheel-sdk/linux-x64/tools/check-text.mjs
  tests/<game>-text.json --scales 1,2` (inside a GPU suite) checks them.

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

CI runs `tools/check-prompts.mjs` (shipped in the SDK) over every game, listed in `PROMPT_GAMES` in
`.github/workflows/ci.yml` (a new game goes on that list). It fails on text such as `A:`, `(B)`, `press X`, `LB`, `RT`; a line that really means
it carries `prompt-ok: <reason>` on it or just above. It cannot see letters drawn in art or in code, so review those
by hand. Glyphs need at least 16 canvas pixels at a pixel scale of 1 (18 or more for anything read in motion).
