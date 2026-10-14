# BomberDude

A 3D arena bomber on cartwheel and three.c (three.js r186): four critters,
eight themed arenas, CPU rivals, Battle and Adventure. The rules are in
`sim.c` (integer only, deterministic), the CPU players in `ai.c`, the 3D side
in `world.c`, `models.c`, `fx.c`, `scenery.c` and `textures.c`, and the app
(menus, HUD, input, save, audio) in `main.c`.

## Controls

| | keyboard | gamepad |
|---|---|---|
| move | WASD or the arrows | stick or d-pad |
| drop a bomb | Space, Z or Enter | A |
| action: set off a remote bomb, stop a kicked one | X, E or Shift | B or X |
| pause | Esc or P | START |
| menus | arrows / WASD, Enter or Space to choose, Esc to go back | stick / d-pad, A to choose, B to go back |

Touch (phones, tablets): a tap is a click in every menu (QUICK BATTLE is one
tap). In a round, a floating stick for the left thumb (it centres where the
thumb lands, anywhere on the left half), BOMB and ACT buttons for the right
thumb and a PAUSE button under the timer, shown while touch is in use and
bound to the same actions as the pad. A touch-first device (a phone or a
coarse-pointer browser: the web player tells the game) starts in touch mode
before anything is pressed. The game's own controls sit behind one switch
(`BOMBERDUDE_TOUCH_CONTROLS=0`, or the debug request `touch_controls 0`) for hosts
that draw their own overlay.

Players 1-4 are pads 1-4; player 1 is always you and also has the keyboard.
On the players screen, Enter / Space / A always goes on; up / down picks a
player card, left / right on P2-P4 picks who plays it (CPU Normal, CPU Hard,
off, a pad player, the arrows, CPU Easy), X / E (pad X / Y) picks a critter,
and pads 2-4 press A to join. A second player on the same keyboard is the
"Player (arrows)" choice: the arrows, Enter and Right Shift are then theirs,
and player 1 keeps WASD, Space and E. The mouse works every menu (click a
button, a card or an arena); it does not move the critters.

From the title, QUICK BATTLE (selected already: one press of Enter, Space or
A) is a round straight away: you and three CPUs on Clover Commons.

## How the controls behave

- Movement starts and stops on the tick a key goes down or up (no ramp, no
  slide; ice slides on purpose). Up is always toward the top of the screen:
  the camera never turns.
- Cornering forgives: pressing into a wall while leaning toward an opening
  slides you round the corner into it, and moving along a lane pulls you onto
  its centre line.
- With two directions held, the one pressed last goes first; when it cannot
  move, the other takes over, so you never stall against a wall.
- A bomb drops on the tile you are mostly on, on the tick the key goes down.
  A press up to 8 ticks early (still standing on your last bomb) drops as
  soon as it can. You can always walk off your own bomb, never back onto it.
- The title, the players screen and the pause menu show the controls for the
  device used last.
- From the title, QUICK BATTLE takes one press; Battle takes three presses of
  the same button (BATTLE, go, arena), and player 1 stays a person whatever
  is pressed (gate `bomberdude-confirm`).
- Every person's critter has a ring and a player marker ("P1"), every bomb shows
  its fuse burning down, red floor tiles mark where a blast is about to land,
  and the near wall is low so nothing hides behind it.
- CPUs drop their first bombs about a second after "GO!".
- Touch: one tap starts QUICK BATTLE; the stick walks and corners, BOMB
  drops, PAUSE pauses (gate `bomberdude-touch`).

## Tools

- `tools/make-arenas.mjs` writes the arena scenes from their layouts.
- `tools/make-grade.mjs` writes the colour grades, `tools/make-audio.mjs` the
  sound and music, `tools/make-gate-script.mjs` the gate input scripts.
- Dev aids (environment): `BOMBERDUDE_ARENA=n` straight into a Battle round,
  `BOMBERDUDE_CPU_ONLY=1`, `BOMBERDUDE_PLAYERS=n`, `BOMBERDUDE_FIRST_TO=n`, `BOMBERDUDE_ROUND_SECS=n`,
  `BOMBERDUDE_BOOM=tick`, `BOMBERDUDE_PROGRESS=n`, `BOMBERDUDE_CAM_CLOSE=1`, `BOMBERDUDE_NO_AUDIO=1`,
  `BOMBERDUDE_RECORD=file`.
