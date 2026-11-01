# Changelog

What each release added, changed and fixed, newest first. Releases are git tags; each one's game
carts and desktop and Android builds are on its GitHub release. Entries name the game they touch.

## v0.16.23 (2026-11-01)

- **BikeMesa**: Reloading tracks no longer runs the cart out of memory: each load left a whole rider behind (about 8 MB on the first track), which crashed the game after about 84 loads. The log and a `mem` debug command now show memory at each load

## v0.16.22 (2026-11-01)

- **racer**: The truck's windscreen is an open frame: the opaque glass hid its driver and left only the wheel and hands on show

## v0.16.21 (2026-11-01)

- **racer**: An open helmet's dust wrap is dull cloth lying flat on the face (in v0.16.20 it stuck out in the suit's colour like a beak)

## v0.16.20 (2026-11-01)

- **racer**: The drivers look like people: a shaped racing suit with sloped shoulders and a harness, tapered arms and legs, gloved fists, closed helmets with visors (or open ones with goggles and a dust wrap), a thin scarf; the propeller beanie and helmet spikes are gone

## v0.16.19 (2026-11-01)

- **racer**: The wall's U needs steering: the road does 80% of the turn and the rest is the driver's, so a kart that does not steer runs wide into the outside rail and loses speed
- **racer**: A kart takes one item box from a row, the nearest it touches, even when it touches two

## v0.16.18 (2026-11-01)

- **racer**: The chase view keeps up through the wall's U (it fell 45 degrees behind the kart and stared at the outside rail)

## v0.16.17 (2026-11-01)

- **racer**: The wall's U carries the kart round like a rail: in v0.16.16 it slid to the outside rail and fell from 38 to 23 m/s at the top; now it holds 38 m/s within 2.7 m of the middle at every entry speed tried, 2.7 s on the wall; Fairgrounds' medal times from the measured lap

## v0.16.16 (2026-11-01)

- **racer**: Fairgrounds' wall U-turn is tight now: the course's two long sides pinch in to it through S bends and the map runs about 70 m further out, and the U is one 68 m arc on 100 m of wall (it was 291 m of wall, about 210 m across)

## v0.16.15 (2026-11-01)

- **racer**: Fairgrounds' second U-turn really goes up a wall now: a new road piece (P_WALLU) curls the road up onto a 110 m painted wall, climbs it, arcs over and along it and comes back down to the ground going the other way, the kart's up level the whole way (v0.16.14's level turn on a steep bank was not that); time trial times sit on see-through panels; a CPU kart backing round for 2 s turns round forwards instead (one slid backwards down a Temple wall for 50 s); debug: karts.y, karts.upy

## v0.16.14 (2026-10-31)

- **racer**: Fairgrounds' second U-turn is now a wall turn: the road banks to 78 degrees over 330 m on low walls so the fair stays in view, carried on rusted hydraulic rams and raking struts on concrete pads over a dug pit; karts hold to it like a loop but can still drift and pass; entries from 38 to 58 m/s run clean, faster ones scrape the wall without falling; debug: put <s> [speed] [lat] and karts.lat

## v0.16.13 (2026-10-31)

- **racer**: Fords drag less and a boost skims them (a kart crawled at half speed for 5 to 7 s of every lap on Drowned, Temple and the Dam, with no way round); at least 40 s between Shockwaves (at 25 s one spun the leader about three times a race)

## v0.16.12 (2026-10-31)

- **racer**: Engine v0.28.6 (no stall on the first turn, water as before v0.26.0); art from a race-camera survey of all eight maps: Frost's road is the photographed packed snow with tyre tracks (it was a flat white), its aurora has no hard hem or cut ends; Temple's walls are stone, not green, under a blue-sky ambient; Salt's sky is no longer green; the Dam's rainbows are a faint arc; Drowned's lit windows are lit from the ceiling and dimmer, its road glow a sheen

## v0.16.11 (2026-10-31)

- **racer**: Concrete roads mostly grey and paler (the photo's brown read as chocolate in a warm sun); the Dam's ground bounce less red, so its steep shaded climb reads as weathered concrete

## v0.16.10 (2026-10-31)

- **racer**: Salt's standing stones are weathered textured rock, more and rougher drums in buff and grey (flat pink drums with gold tops sat oddly on the photographed land)

## v0.16.9 (2026-10-31)

- **racer**: Temple in hazy mid-morning light, with twice the palms (it read as a temperate wood in a dusk sky): four views 7.8 ms on the 890M, Low in lean mode 6.0

## v0.16.8 (2026-10-31)

- racer at Low: terrain's macro colour from 40 m (0.5-1 ms of the 890M's scene pass, no change seen from the race camera); log marks at the green light and the race's end, for a host with no debug channel

## v0.16.7 (2026-10-31)

- **racer**: Auto measures only from three seconds past the green (it stepped Medium to Low on the load's tail and the countdown); below High the sun's shadows reach 70 m (a seventh to a quarter fewer draws, no change seen from the race camera)

## v0.16.6 (2026-10-31)

- examples on cartwheel-engine v0.28.2: Auto's level is saved and retried one level up each run; tables regenerated. racer: writes the settings when Auto steps, so a weak machine starts its next session at that level. BikeMesa: BIKEMESA_BENCH_QUALITY turns Auto off before it sets the level (the other order reset it to High).

## v0.16.5 (2026-10-31)

- **racer**: Auto takes one step a race (a step shows only in the next race's scenery, so it walked High to Low on one heavy race); examples on cartwheel-engine v0.27.0 (full resolution is the default)

## v0.16.4 (2026-10-30)

- examples on cartwheel-engine v0.26.1: Auto graphics reads the real clock, so it steps down on a libretro core, which delivers a fixed time step (the Vega 8 box never stepped); tables regenerated. Every game stays at full resolution under Auto (Auto gives up detail, never pixels). BikeMesa: the seven draw states its table lacked on the new engine (the red banner on Red Planet).

## v0.16.3 (2026-10-30)

- racer perf on weak machines (a Vega 8 at 40-55 fps): below High the graphics settings build what three or four views get at any number of views: shorter ranges, fewer trees, no tree shadows, no planar water, no ambient occlusion and so no depth prepass (that pass doubles the draws, 20k WebGPU calls a frame on the Dam). Dam 1332 draws to 401, Temple 1872 to 734; on the 890M Temple 14.4 to 7.8 ms of CPU and 13.2 to 7.6 of GPU. Auto's budget 17.5 ms (the default 19 sat at 53 fps at High), at full resolution always (Auto gives up detail, never pixels), terrain follows the settings, the level goes in the log. Temple: palms (tools/palm-gen.py) and giant calathea leaves, canopy to 170 m; CREDITS

## v0.16.2 (2026-10-30)

- **racer**: The time trial's ghost draws again (it took the tyre's rough diffuse, a state no table has for a see-through material)

## v0.16.1 (2026-10-30)

- cartwheel-engine v0.25.4 SDKs (games link on macOS again)

## v0.16.0 (2026-10-30)

- **otium**: A Roman trade game where time moves only while you act
- **otium**: Board-game goods and values, a calm icon HUD, gates on every host
- Revert "otium: board-game goods and values, a calm icon HUD, gates on every host"
- Revert "otium: a Roman trade game where time moves only while you act"
- examples on cartwheel-engine v0.24.1 (scattered glTF trees keep their crowns). racer: the debug command eye (a fixed camera for stills on a cart, as RACER_EYE natively) and tools/rd-shot.sh (stills of the cart on romdev)
- **racer**: Breakout's yard floor at half the repeat (at 9 m its cracks read as giant tiles), Sunspire's mud warmer and brighter (it read near black)
- **racer**: One Shockwave at a time and none from a box for 25 s after one goes off (a leader who missed a hop was spun five times a race: the keyboard-driven bot on Salt Flats, 5 of its 6 spins); RACER_STATS adds the human's wall hits, spins by cause, falls, hits taken and seconds crawling under half speed
- **racer**: A square hit on a wall in a race turns the kart's speed along the wall the way the road goes, keeping some of it (driven straight into the Refinery's last bend at 41 m/s a kart was thrown back at 11 and crawled for 2.5 s, every lap); keyboard-driven bot, 3 laps on all 8 maps: seconds crawling 141 -> 107, its spins 38 -> 25, one fall in the whole field; RACER_SLOWLOG=1 logs each crawl with where it ended
- racer pilot (branch only): the Dam's land on Terrain3D (ambientCG CC0 layers by slope and strata, tools/terrain-layers.sh) with the oax EZ-Tree conifers and Yughues grass as TerrainScatter3D layers (tools/foliage-art.mjs, licences beside them); the debug command eye (fixed camera stills on a cart); tools/rd-shot.sh (stills on romdev); engine v0.24.1
- racer pilot (branch): the realistic Dam, wip: buttes in the land, Poly Haven rock scans (tools/rock-decimate.py, CC0) as scatter layers, matte ground layers, the switchyard pad, no cartoon clouds, mesas or procedural rocks; RACER_NOSCATTER
- **racer**: The realistic Dam. Its land on Terrain3D with photo layers by slope and strata, real buttes in the land, EZ-Tree conifers, Yughues grass and Poly Haven rock scans as scatter layers, no cartoon mesas, cactus, procedural rocks or clouds; the switchyard on a gravel pad; terrain High alone, Medium in split screen; examples on cartwheel-engine v0.24.2 (terrain shading 38 -> 8 ms on a screen of cliffs); CREDITS for the new art; dam_perf.sh takes RACER_EXE
- **racer**: The Dam's rock outcrops only on slopes to 55 degrees and sunk deeper (to 80 degrees and sunk 0.6 they hung off the cliff faces)
- **racer**: The realistic Salt Flats on a shared land module (land.c: a map's Terrain3D, paint and scatter layers): a real mountain range out of the foothills, the pan's white crust and bright salt, the plateau's stony ground, the escarpment's rust rock, gravel fans; rock scans, junipers, pinyons and dry grass; no cartoon ridges, mesas, cliff slabs or clouds (RACER_CARTOON=1 brings the old ground back); 4.1-5.3 ms of GPU at 1 and 4 players; tools/terrain-layers.sh takes the map and Poly Haven sets; CREDITS
- **racer**: Photo bases for road textures (textures.c: a terrain layer's photographed albedo and height, decoded once with a private stb_image and sampled by metres, the generator's lane paint and tyre tracks over it); the Salt Flats' road is now packed salt from the photographed crust, not cartoon hex plates
- **racer**: Photo bases for the concrete road too (the Dam, the arenas' ramps): Poly Haven worn concrete under the slab joints, stains, cracks and lane paint; tools/terrain-layers.sh roads packs the road photos (concrete, race-track asphalt, old asphalt)
- racer CREDITS: the road photo bases
- **racer**: The realistic Frostbite Pass on the land module: snow, drifts, packed verges, grey and dark rock, scree and glossy lake ice painted by slope and height, firs, ponderosas, snags and rock scans; no cartoon peaks, pines, crags or crystals (the aurora, stars, cave and set pieces stay); its cut banks a smooth clamp (a hard one creased into a sawtooth); split screen without tree shadows and with fewer, nearer trees on Frost and the Dam (four views: Frost 7.4-7.7 ms, the Dam 4.3-6.5); examples on cartwheel-engine v0.24.3 (terrain cliffs without bands or diamonds); CREDITS
- **racer**: The realistic Overgrowth Temple: jungle soil, leaf litter, moss, mud, clearings and mossy rock on the land module; a canopy of generated broadleaf emergents and mid-storey trees (EZ-Tree, ambientCG LeafSet004; configs in tools/eztree-configs), Poly Haven ferns and anthurium below (tools/plant-split.py); the temple road on photographed mossy paving; no planar water reflections there (each redrew the jungle), no cartoon jungle or crowns on the masonry; four views 5.2-7.3 ms; water_opts.no_planar; CREDITS
- examples on cartwheel-engine v0.25.1 (three.c on three.js r187): every game's program tables regenerated. racer: the realistic Rustwheel Fairgrounds and Cinder Refinery on the land module with photo wood, metal and dirt roads; Fairgrounds mesas in the land itself, grey weathered plank walls and a clearer dusk sky; the Refinery's plate is rusted paint, not bare metal (a dusk sky left it black); Drowned photo asphalt and building facades; land follows the road grading on Salt, Frost and Temple (ground_bind); reversed depth; rough diffuse on roads, walls, props and rubber; the Dam's brush patches blend in; CREDITS

## v0.15.0 (2026-10-29)

- examples on cartwheel-engine v0.24.0. racer: a blast at a player's camera comes out small, thin and dim (a hit on the followed kart whited out the whole view for half a second), smoke, fireballs and flares fade near the lens (camera_fade); no camera shake anywhere (the pad rumbles); Breakwater's cove walls get rock ledges and strata with grass on them, low sand dunes up the beach and sand tracks up the cliff

## v0.14.3 (2026-10-29)

- **racer**: Breakwater's shore: the sea in over the shallows and the sea bed under it (no grass at the waterline), tide pools, kelp lines and damp patches on the beach; Breakout's yard: the exercise court painted on it, the gate's no-go box, oil stains and puddles, tyre tracks down the canyon road

## v0.14.2 (2026-10-29)

- **racer**: Floodgate's dams get buttress ribs, white water spilling down two chutes into foam, lamps along the crests and a girder under the bridge, the canyon's sides in rock; Breakout gets guard towers with lit cabins at each stretch's corners, razor wire along the walls' tops and a warmer dusk

## v0.14.1 (2026-10-29)

- **racer**: Breakwater is a cove now: grey rock cliffs either side and behind (the land rising behind them, pines and gorse on the rims), the beach running down into the sea at the open south end (drive in and you are back at the boats), grassy terraces behind concrete parapets, a concrete seawall with its parapet, storm line and lamps, a brighter morning
- **racer**: Sunspire Keep sits down in a jungle valley (steep slopes to over its walls, canopy giants leaning in along them), a mud floor and mossier stone; Floodgate's rails are concrete, not hazard stripes

## v0.14.0 (2026-10-29)

- **racer**: Four new arena weapons (arena item boxes only): the Flak Burst (a cone of pellets: close in a kart spins, farther a jolt), the Shock Orb (a slow ball of light set off with a second press, by a foe running into it, or by a blaster bolt for the bigger shock combo), the Rocket Pod (four missiles in a fan) and the Rail Lance (a short charge with its aiming line drawn for all to see, then an instant shot 140 m straight on). HUD icons, their shots drawn, the CPU drivers use them all (and on assault targets). RACER_ITEMLOG logs every arena item used
- **racer**: Two more big capture the flag maps. Floodgate: two dams across a flooded canyon, 740 m flag to flag, three ways across (the broken bridge with a 36 m kicker jump over the water, the covered spillways whose tunnels end in one-way pads up onto the enemy crest and whose roofs are a middle road, the reservoir floor with an island). Spindle: an orbital foundry in half gravity (arena_def.gravity), the enclosed core rail down its spine, broken hull plating with floating kicker jumps, the furnace ring in the middle; Twin Spires' space sky shared (space_sky_build). CPU drivers go straight up a ramp once lined up on it (the route pulled them back to its foot: Mesa Run 1-2 -> 2-3 in a 3-minute round). Breakwater's masts and wrecks set on the ground they stand on. README

## v0.13.0 (2026-10-29)

- **racer**: Assault, a fourth arena round: one team attacks a fortress stage by stage while the other holds it, then they swap and the second attack must beat the first's time. Objectives to destroy (hit points: missiles, bombs, boomerangs, turbo rams), reach or hold; gates and doors that sink away when their objective or stage is done (each a static body of its own); spawns that move up with the stages; defender gun nests that fire homing missiles at attackers in sight. A blaster for any kart with nothing in hand (fast bolts that chip targets and jolt karts). CPU drivers attack the stage's objectives and defend them, their route rebuilt as gates open. HUD: ATTACK/DEFEND, the stage and its pips, a pointer with the target's health, the half clock and the time to beat; news, stamps, results. First map: Breakwater (beach landing, three pillboxes behind a tank-trap line, the seawall gate, a three-terrace switchback cliff, the cannon bunker). New racer-assault gate
- **racer**: Assault plays by full stage rules: both halves always run, the second half's clock is the time the first attack took (the whole clock if it failed), only breaking through counts (no credit for objectives short of it) and two failed attacks are a draw. Karts have hit points in an assault: wrecked at none and back at the team's spawn for the stage (UT's respawn, the attrition that is the defenders' job), healing a while after the last hit; armour on the HUD. Gun nests knocked out wake again 15 s later until their stage is passed (UT's cannons). A hold counts while an attacker is in the zone (a timed hold). +10 points an objective, +25 the last. CPU drivers: a goal counts as seen past its own solid, ramps chained level by level, attackers stay on the objective. Second map: Sunspire Keep (blow the tunnel, drop the drawbridge, hold the altar, destroy the crystal on the pyramid)
- **racer**: Assault's four maps: Breakout (prisoners blast a pen door, take the yard switch, break the portcullis winches, hold the gate lever and hold the pad down the canyon for the lift) and Deepwell (an oil rig over open sea: blow the gangway's boom gate, destroy all four pumps in any order, then the wellhead up the drilling tower) join Breakwater and Sunspire Keep. README: assault

## v0.12.16 (2026-10-29)

- **racer**: CPU karts take a shortcut's bends at a speed they can hold and fly straight off jumps: on a branch the corner speed is judged from where the kart is (it ignored the bend it was in and sped up through the boardwalk's first one into the wall) with less grip trusted beside a wall; in the air a CPU leaves the stick alone and the yaw nobody asks for dies away (the pull to the flight path alone swung the nose 20 degrees off by the Dam jump's landing). Fall sweep, 3 classes x 8 maps x 2 laps: 2 left, none on a shortcut. Time trial references redone (Orbit +1.7 s: the spacewalk is braked for)

## v0.12.15 (2026-10-29)

- **racer**: CPU karts stop falling off open edges: on a branch they brake for the bend by how fast the wheels can turn it (a 40 m bend at 41 m/s was full lock and off the Orbit spacewalk), trust less grip with no wall beside them, and brake harder when over; a kart left facing the wrong way backs round to the line, and if that is blocked turns forward instead (the old escape backed out again and sat there). Fall sweep, 3 classes x 8 maps x 2 laps: 12 falls -> 2. RACER_TEST_YAW turns a test-placed kart off the road's heading

## v0.12.14 (2026-10-29)

- **racer**: Capture the flag cannot stall at 0-0: a carrier at its base scores once its own flag has been away 20 s (both teams holding the other's flag at their bases left whole rounds scoreless: Mesa Run 0-0 -> 1-2, Twin Spires 0-1 -> 1-3), the HUD counts it down (HOME (SCORES IN 12s)); main.c's keyboard bot compares its integer steer with abs, not fabsf

## v0.12.13 (2026-10-29)

- **racer**: Overgrowth Temple thins its trees again with three or four views (70 canopy giants round the pyramid, wider-spaced roadside trees): four views 7.35 -> 7.14 ms CPU, 6.58 -> 6.41 ms GPU

## v0.12.12 (2026-10-29)

- **racer**: Frostbite Pass: the slope ledges are outcrops swelled out of the slope instead of a sheer cliff band over a flat tread (from the road below that read as a grey fence hovering on the snow). Each one rises gently from the hill behind with the snow drifted over its top, drops steeply in front and runs out into a bank of snow at its foot; its whole border ring is sunk just under the ground, its height rolls along its length and it fades into the slope over its ends. Faces that look up wear the ground's own snow colour at that steepness (no bright rim), steep faces are cool slate in strata by height, dusted between; it is wound by its own grid so every face looks up and out of the hill. Four views at frame 1625: draws unchanged (873), triangles 1.77M -> 1.95M; GPU 5.01 -> 5.21 ms
- **racer**: Salt Flats Run road varies by stretch (dark tyre wear heavier into bends, wind-rippled salt drifts across the deck on the pan, an ochre dirt road with ragged ends through the derrick field) laid as skins in the road's own uv; pan set pieces read through the haze (salt stones dark at the foot and gold at the crown, stouter signal towers, the airship's hull weathered dark with a raised nose and a red beacon) and a little less haze at range
- **racer**: Spillway Dam gate hoist towers read as towers from the road: the hoist house sits 8 m up on a flared pier head (was 12) and the towers stand 7.5 m off the road (was 4.2) with their piers running further down the dam face, so the red roof stays in the view as a tower comes alongside instead of the top of the frame cutting it to a stump
- **racer**: Cinder Refinery cutting's strata read: the old skin followed a terrain cliff and lay flat at the plain's height (thin ribbons floating over the cut, the banks bare); now each bank is a rock face of its own from the wall cap to the plain, in wavy beds of pale ash and ochre between near-black basalt (rock texture, colour per corner so the beds run on unbroken), each bed edge a lit lip over a strip of shadow, the foot warmed by the sodium lamps; the height fog thins more slowly with depth (falloff 0.1 -> 0.06), so the 22 m deep cut is less smothered while the plant above looks the same
- **racer**: Wreck Orbit's loops were the weakest stretch (from inside, the road curling up into a black void): each loop now runs between two great hoops tied to its reactor by spokes, a ring of lamps round every hoop and runway lights flush on both kerbs chasing round the way the karts go (four meshes stepped by the frame); a far belt of rocks wheels slowly about the course, so the backdrop has parallax against the stars

## v0.12.11 (2026-10-29)

- **racer**: Drowned Downtown art pass 3: the shop fronts on the run to the line get deep frames with the glass set back, lit reveals, pendant lamps, rain running down the glass, a street reflection, names standing off the fascia and their own awning or steel canopy; the overpass gets a lit edge line, hazard chevrons toward the karts, deck lamps under the span and lit pier bands; the plaza's fountain throws a tall lit jet, the clock tower is taller with neon corners and brighter faces, the statue stands higher and the lamps are taller (4-player draws unchanged, 1450)
- **racer**: Rustwheel Fairgrounds art pass 3: the high run home (the road some 16 m up between tall walls) gets what shows over the walls from up there: two lattice drop towers with chase bulbs and flags, a second big wheel standing still and facing the karts, a swing ride frozen mid-swing, tents on tall poles flying long pennants with bunting, and masts on the wall tops with strings of bulbs at eye level. The midway and the scatter keep off the new rides. Draws at frame 1625 in four views unchanged (692); one view on the high run 743..1063 -> 763..1093 (+2..3%)
- **racer**: Overgrowth Temple art pass 3: the jungle's trees are the map's own now (the toolbox's jungle tree and broadleaf were faceted lumps on a stick, plain blobs once the canopy giants scaled them up): a flanged trunk, limbs, a canopy in tiers of flattened pads dark underneath, a ragged rim, aerial roots and vines hanging from under it, some in flower; the crowns over the road get the same tiers, roots and flowering vines; bird flocks wheel high over the canopy and butterfly swarms flutter over the verges as particles. Four views at frame 1625: draws 1547 -> 1565, GPU 5.87 -> 5.98 ms

## v0.12.10 (2026-10-29)

- **racer**: With three or four views the height fog keeps its haze but drops its volumetric light shafts (a froxel grid per view): the temple in four views 7.9 -> 6.8 ms CPU, 7.3 -> 6.2 ms GPU; every map now 4.1-6.9 ms CPU

## v0.12.9 (2026-10-29)

- **racer**: With three or four views the water draws no planar reflection or refraction (each redrew the scene twice more per view: the temple's water pass was 4.7 ms of CPU in four views); one and two views keep both. Four players mid-race, quiet machine: CPU 4.1-7.9 ms, GPU 3.6-7.3 ms on all eight maps

## v0.12.8 (2026-10-28)

- **racer**: A hit on a jump's run-up or in its flight only shakes the kart, at full speed (spun on Wreck Orbit's ramp at 62 m/s one left the lip at 25 and dropped into the pit); a jump's flight bends up toward the landing's arc when a kart near the jump's speed falls below it; the crest hold stays off over the whole jump (it held karts down just past the lip). Fast class, two laps, all eight maps: 1 fall

## v0.12.7 (2026-10-28)

- **racer**: Air control over the road: in a long flight (off a crest at speed, down a slope) a kart drifts no further out over an edge and bends gently back past three quarters of the half width (a boosted kart off the top of the temple's descent drifted 6 m out, clipped the wall top and tumbled off: temple falls 1 -> 0, tricks 5 -> 6)

## v0.12.6 (2026-10-28)

- **racer**: An unwalled edge (the temple's rope bridge) is safe from other karts: a bump there only nudges, and the tyres bite against a sideways slide unless drifting (a kart clipped in a pass slid off sideways at 13 m/s, nose still straight); a jump's last 14 m carries a kart to the jump's speed whichever way it faces (one spun at the lip dropped into the pit at 1 m/s)

## v0.12.5 (2026-10-28)

- **racer**: A crest holds a kart that is lifting off where the road falls away faster than gravity can follow (beyond 0.85 g, up to 2 g, never at a jump's lip or in a loop): fast karts were thrown off the fairground's lift hill into the drop (2 Blazing laps 5 falls -> 0, a full race 3 -> 0); the fairground's helix rolls in over 60 m (over 24 at 46 m/s it threw the player off); time-trial references re-measured

## v0.12.4 (2026-10-28)

- **racer**: Drowned Downtown straights each get a landmark (bus terminal canopy, billboard canyon, collapsed overpass, flooded plaza with clock tower and statue, lit shop fronts to the line); the road a shade lighter with cat's eyes down the lane lines
- **racer**: Rustwheel Fairgrounds tall walls rise out of the low ones over a 22 m swept fairing at the top of the drop and fall away again before the jump ramp, no more hard step
- **racer**: Overgrowth Temple, the helix's last turn over the approach is an open span on slim columns instead of a 20 m carved wall, piers by a lower road slim down; the ground round the jump becomes a ruined court (broken walls with vines, a colonnade, two giant guardians, a spring falling into a pool)
- **racer**: Wreck Orbit spacewalk reads as a walkway: knee-high rails with a lit handrail and lamps down the straight between the bends, unbroken edge lights and chevrons on the two open bends, a dashed centre line

## v0.12.3 (2026-10-28)

- **racer**: Salt Flats pan dressed with tall verticals (standing stones of salt, red and white signal towers on guy wires, wrecked drilling rigs, a crashed airship) in one chunked builder; escarpment faces tested over their whole footprint and slid back so none is hidden from the lanes
- **racer**: Spillway Dam closing crest opened to the reservoir (lake-side wall drawn as a balustrade, collision unchanged), lamp standards, banners and spillway gate hoist towers; water tunnel relined in pale sea-green tile with ring frames, cable trays, a hazard tide line and lit section plates
- **racer**: Frostbite Pass snowfield texture laid through a slow two-level warp with broad lavender and cyan tint patches and softer ripple relief, so the wind ripples no longer stamp one pattern over the pass; rock ledges with snow treads and icicles (some frozen seeps) lifted out of the steep faces themselves, so they sit on the slope
- **racer**: Cinder Refinery cracked mud laid through a two-level warp with soot, rust and ash patches; the cutting's banks above the retaining walls wear a skin of rock strata with basalt and ochre ledges and drain pipes; both tunnels get ring frames, cable trays and a painted band, the tank tunnel lined in blued steel plate and the pipe tunnel in sooty red
- **racer**: Frostbite Pass has no loose rocks on steep snow: the half-drowned rocks and the ice lumps keep to slopes under about 20 degrees, the boulder scatter over the steep faces is gone (a pale rim with no contact line read as floating); the steep faces and the banks graded between the climb's switchbacks carry ledges shaped out of the slope itself, in grey slate
- examples on cartwheel-engine v0.21.1 (water seen from under its surface on WebGPU)

## v0.12.2 (2026-10-28)

- **racer**: The touch stick sits still in the bottom left corner where it is drawn (a fixed stick in a square round it, its throw scaled to the screen); the floating stick over the left half jumped to wherever the thumb landed
- examples on cartwheel-engine v0.21.0: touch sticks keep their ring at a home in the bottom left instead of jumping to the thumb; bikemesa, bomberdude, chromite and spout draw the ring where the engine keeps it

## v0.12.1 (2026-10-28)

- **racer**: Rustwheel Fairgrounds midway (game stalls, tent pairs, small carousels and dodgem wrecks on the bare grass between the attractions) and paving that follows the ground; Cinder Refinery cutting dressed (retaining walls with buttresses and sodium lamps, three pipe bridges high over the road) and blotchier ground
- **racer**: Rustwheel Fairgrounds walls whitewashed (the mint tint read as mouldy grey-green boards on the tall helix walls)
- **racer**: Rustwheel Fairgrounds lift hill tops out over a long rounded crest (same height, length and place), the tall walls stand 4.4 m and run on from the helix to the jump ramp: fewer karts thrown off the summit or over the walls at the foot of the drop; its time trial reference re-measured
- examples on cartwheel-engine v0.20.1 (three.c v0.186.22): clustered lighting once a frame in split screen

## v0.12.0 (2026-10-28)

- Add BikeMesa bikes, Night Burbs, and level and rider improvements
- **racer**: A kart racer on Box3D: turtle-path tracks (loops, corkscrews, jumps), karts on cw_vehicle3d, CPU drivers, first light
- **racer**: Split main.c into the race (main.c) and the front end (ui.c); stubs for fx, audio, textures, props, items and eight map files; loop easing, jump design speeds, wall handling
- **racer**: Kartgfx.c, build and run helpers
- **racer**: Sim_init takes the map def; RACER_MAP=-1 runs the proving ground; world_frame hook
- **racer**: Procedural PBR textures for every road, wall, ground, kerb and boost kind; world materials use them
- **racer**: Fx first pass: kart emitters, pooled bursts, skid marks, missile trails, ambient emitter API
- **racer**: Eight track layouts (closing solve bounds, lapcheck, tracktest dump), kart shoves stay level
- **racer**: Rocket starts and flooded engines, CPU rubber band
- **racer**: Steering ease for keys, speed field of view, best lap debug fields
- **racer**: Road and wall texture tuning, brighter asphalt, texture dump tool
- **racer**: The front end and HUD (title, join, vehicle and map select, options, pause, results, race HUD, touch overlay)
- **racer**: Six kart models, kart rig and animation, item graphics, studio view
- **racer**: Texture tuning, material states for emissive and rough maps
- **racer**: Program table for kart and item materials
- **racer**: Audio (engines, tyres, wind, event sounds, tunnel echo, ten music tracks, generators, offline mix render)
- **racer**: Ui polish, dev screens, scripted-input helpers
- **racer**: Touch layout, results hold, dev hud demo
- **racer**: Ui fixes (escape is back, touch hud, menu font)
- **racer**: Audio fixes (drop-away sound, attract demo stays quiet, pause music level), music brightness
- **racer**: Kart hull rides above the road, loop gravity and alignment, AI holds the middle on corkscrews
- **racer**: Narrow view scale
- **racer**: Race debug command for carts, audio frame and pause hooks
- **racer**: Fx: shockwave and skid decals, effect sheet generator, dev aids (RACER_FX), tuned bursts
- **racer**: Props toolbox (generators, instancing, scatter), track dressing, ground and water
- **racer**: Finish banner placement, race count on the title
- **racer**: Fx: pad boost lighter, cached camera lookup, trail log aid
- **racer**: Final lap time, finished karts cruise on the CPU driver, results in one view
- **racer**: Dam map, first pass: anchors from the track, canyon terrain with strata, reservoir, midday scene
- **racer**: Props polish (ruins, statues, wrecks, lit signs), ground grades to banked roads, fewer draws, water polygon and hills helpers
- **racer**: Drowned downtown, first pass (neon walls, street canyons, storm sky, grade)
- **racer**: Frostbite Pass first pass (twilight scene, aurora, stars, peaks, terrain, forest, lake, ice cave, start gantry)
- **racer**: Dam map, spillway water, river, tunnel hill, dam face, dressing
- **racer**: Salt Flats Run scenery, dawn scene, ground, set pieces, animation
- **racer**: Cinder Refinery first pass (dusk scene, flares, pits, tunnels, racks, lightning tower, robots)
- **racer**: Overgrowth temple map, first pass (start gate, jungle, tunnels, rope bridge, ring, waterfall, pyramid)
- **racer**: Program tables for the kart, item, prop and water materials
- **racer**: Drowned downtown, lake tube, jump billboard, corkscrew ruins, wheel, sky life
- **racer**: Sheet tool
- **racer**: Drowned downtown, window and searchlight tuning
- **racer**: Refinery grade, perf tools, ground follows banked road, fewer draws
- **racer**: Salt Flats Run dust devils, yard scatter, telegraph poles, performance toggles
- **racer**: A gate per map (eight CPU karts, state must match on every host)
- **racer**: Rustwheel Fairgrounds first pass (clown mouth, wheel, carousel tower, strings, loops dressed, backdrop, fireworks)
- **racer**: Dam map: jet gates, rainbows, tunnel lining and drips, bridge, lookout, intake towers, power station, props, clouds and eagles
- **racer**: Refinery keep-outs, slits, flames
- **racer**: Frostbite Pass crevasse with rope bridges and prayer flags, cave portals, station and mast, camps, wrecks, weather, colour grade
- **racer**: Drowned downtown, colour grade in the pack list, program tables
- **racer**: Refinery brighter ground, signage, blinking lip lamps
- **racer**: Wreck Orbit scenery (sky dome, planet, giant, sun, station, deck, reactors, hangar, jump modules, animation)
- **racer**: Refinery shot helper
- **racer**: Fair map: tears in the tents, fences, clouds, batched scatter, fewer draws
- **racer**: Wreck Orbit docking arms, banners, far modules, pit bays, checkpoints, tuning
- **racer**: Run.sh waits as long as a race takes; program tables
- **racer**: Frostbite Pass frozen waterfalls, lantern posts, lake cracks, fisher hut, lookout, cave veins, anemometer, ridge plumes
- **racer**: Fair map: sky left unfogged, no strings over the crest, scene fixes
- **racer**: Fair map: no clouds, smaller corner boards, three real lights
- **racer**: Overgrowth temple map, full pass (ford and waterfall, gorge, ring, pyramid, grade)
- **racer**: Kart and item frame hooks, perf tool, program tables
- **racer**: The _auto build starts its own race; gate, CI lists, README, credits
- **racer**: Game_planned_views for maps
- **racer**: Frostbite Pass round 2 (thinner in split screen, brighter aurora, bigger ice falls, slide crystals, jump warning hoops, snow-blue spires)
- **racer**: Temple round 2 (views-aware thinning, tree variety, visible birds, grid clearance)
- **racer**: Wreck Orbit round 2 (camera-anchored sky, brighter fill, second hangar, bigger reactor cores, smaller masts)
- **racer**: Refinery round 2 (view-aware cost, landmarks, river, pads, pit walls)
- **racer**: Salt Flats Run thinner split screen, jump bollards, lifted shadow fill, programs
- **racer**: Refinery smoke, program tables
- **racer**: Drowned downtown round 2: view-count thinning, flash hold, rim lights, sea level
- **racer**: Dam map round 2: saturated strata, smooth clouds, stronger sun, split-screen thinning, spillway plant, climb detail, jump hoops
- **racer**: Fair map round 2: thinning for split views, round hot bulbs, moving lift chain, smoother ground and paving, hall flank windows
- **racer**: Maps reload cleanly (water and road-index state reset, drowned buffers and handles), carts start at 256 MB, the _auto build starts before the demo, reload dev aid
- **racer**: A reload gate
- **racer**: RACER_BOT dev aid drives human karts with the CPU driver
- **racer**: Move the lap record label off the TIME header
- **racer**: The bot taps the trick button in the air so the human trick path is exercised
- **racer**: Racer-reload also runs on the WebGPU axis
- **racer**: RACER_BOT=keys drives with keyboard-style steering
- **racer**: RACER_TEST_AT=L<n>:speed and RACER_TEST_KART put a chosen kart before a loop or corkscrew
- **racer**: Cheaper frames: ground cells batched into 128 m blocks, 80 m track chunks, far karts draw simpler wheels and no driver detail, no depth prepass in split screen; prof.sh and draw-count dev aids
- **racer**: Slipstream boost and tow, a catch-up for humans far behind
- **racer**: The Shockwave gives a warning and can be dodged with a hop; RACER_USE dev aid
- **racer**: Time trial with a ghost and medals, cups with trophies and unlocks, speed classes and assists
- **racer**: Pad rumble for the human karts (compiles to nothing without the engine's cw_input_rumble)
- **racer**: Camera debug fields; racer-drowned compares a quiet frame (a spin-out burst renders differently per host)
- **racer**: Loops cap the speed at 40 m/s and jumps at 50 m/s, so the faster speed classes stop flying off corkscrews and overshooting landings
- **racer**: Trial medal references re-measured after the speed caps
- **racer**: Racer-trial also runs on the WebGPU axis (racer-cup's 19000 frames outlast romdev's request limit there)
- **racer**: Program table states for the ghost's translucent paint under the frost and dam skies
- **racer**: The ghost kart glows more so it reads beside the player
- **racer**: The cup trophy's handles loop out of the bowl
- **racer**: The cup trophy is polished metal: lathed shading, a sweeping glint, twinkles
- **racer**: Close calls: shaving past a kart at speed pays a little boost
- **racer**: TURBOS ONLY item preset, fewer Shockwaves on the last lap
- **racer**: Battle mode: two arenas, balloons, points, a clock, battle CPU drivers, HUD, arena select and results
- **racer**: Racer-battle gate; battle results name the winner, CPU karts take paints no human has, arenas play their own music
- **racer**: Program table states for the arenas
- **racer**: Battle CPU drivers: brake for close obstacles, back out only when boxed in, take the open way round, slow to pick up a box instead of circling it
- **racer**: Battle HUD: the spectator's tag, no HIT and SPIN OUT under POPPED, the POP stamp clear of the item; floor paint for Crusher Yard; smaller balloons
- **racer**: Crusher Yard's ring painted orange so it reads on the salt
- **racer**: The turbos-only preset makes a battle of turbo rams
- **racer**: A kart's balloons blink while it cannot lose another; battle dev aids in main.c's notes
- **racer**: Battle oil lies on the deck it was dropped on; a bigger arena map in split screen
- **racer**: Hold an oil drum or a mine out behind to stop a missile from behind; the skilled CPU drivers do it too; program states
- **racer**: Branches, alternate routes that leave the main road and rejoin it (track, meshes, physics, progress, CPU drivers); Wreck Orbit's spacewalk
- **racer**: Shortcuts signposted (deco_branches: lane arrow, island sign, striped noses, markers, chevrons), roadside dressing keeps off forks, the minimap draws them; the spacewalk's keel and edge lights
- **racer**: Toxic Refinery's pipe-yard jump, a shortcut across the first dirt hairpin over a lava pit; branch jumps get the jump markings; RACER_SPLIT dev aid
- **racer**: Funfair's boardwalk (a raised wall-less walk across the first hairpin) and Frostbite Pass's ice chute; checkpoint arches step over a fork; a fall off a raised branch respawns before the ground
- **racer**: Trial medal references re-measured on the maps with shortcuts; dev aids documented
- **racer**: Temple canopy crowns keep clear of road that climbs to canopy height (the chase camera flew through the leaves)
- **racer**: Shortcut signposting fixes (a big arrow board on the approach, bigger island signs, the orbit sign on a plinth, sign backs painted grey, a thicker minimap line); Toxic Refinery's second jump lands on a longer slope so a boosted kart no longer flies off into the hairpin; its medal reference re-measured
- **racer**: The announcer's voice lines, spoken by Kokoro-82M (Apache-2.0) run locally
- **racer**: The announcer calls the race
- **racer**: Options: ANNOUNCER on/off, saved; the voice calls records, medals and cups
- **racer**: The announcer in a battle: the countdown, hits and pops, alarms and colour; no laps, places or welcome
- **racer**: Temple's scenery draws its random numbers in sequence, so native and carts grow the same trees
- **racer**: A second OK on the cup standings no longer skips a race or ends the cup early
- **racer**: Kart air_peak records the flight's highest point (the announcer and tricks can read it)
- **racer**: Announcer lines for close calls, a blocked missile, and the battle (pops, last balloon, knocked out, a rival out, the winner)
- **racer**: Battle CPU drivers steer by a field of nine rays, back out only when boxed in, keep to the box they chose; stuck CPU karts are put back sooner
- **racer**: Battle sounds: a balloon pop and a knockout, synthesised by tools/make-battle-audio.mjs
- **racer**: Battle results stand the top three on a podium with the balloons they kept; the pause screen says which battle, the clock and who is still in
- **racer**: A salt-crust ground texture with no road paint for Crusher Yard, calmer Foundry ramps, a bigger arena map in split screen
- **racer**: The announcer calls close calls, big air by height (air_peak over 6 m), blocked missiles, and the battle: the player's pops, last balloon, knocked out, rivals out, the winner
- **racer**: Every scenery and prop generator draws its random numbers in sequence, so native and carts build the same world
- **racer**: Battle handling for a quick getaway (more drive and turn-in at a crawl, battle only), shorter battle spins, scrums pushed apart, ramps with flared sides nothing hangs on, CPU drivers wary of pile-ups; arena floors capped in brightness
- **racer**: Toxic Refinery's shortcut signposted inside the tunnel: lit SHORTCUT gantry boards hung from the roof over its lane, one 100 m before the mouth and one at the exit, and floor arrows drawing that lane across
- **racer**: Battle CPU drivers see with the kart's width, brake for a wall ahead and slide round it, pass a target they are on top of instead of circling it, leave corner boxes; ramps get gentle banked sides; the battle trace names why a kart is slow
- **racer**: Battle arenas opened up and CPU drivers kept on their wheels: Crusher Yard's deck and pinwheel ramps become a drive-over mound with the crusher hung above it, shorter container walls, drums in the corners, kickers and corner boxes moved in; Foundry's tanks and cover boxes become posts with room round them and its booth backs onto the wall; CPU drivers cap their steering at speed and never drift through a lift; battle spins shorter (BATTLE_SPIN_K) with the kart's flip animation matched; the HUD map draws the mound's slopes and top
- **racer**: The arena select counts Crusher Yard's mound
- **racer**: Playtest round: a closer chase camera, kart racer handling and a clean HUD
- **racer**: CPU drivers hold a drift while their line still bends that way and let go as they straighten: drift boosts went from none to up to 22 a race, falls over the 8 maps from 24 to 12; reference laps re-measured
- **racer**: Playtest round 2: no geometry in the lanes, item boxes that show, no fenders, a turbo with a kick
- **racer**: Collision walls keep their drawn height in loops and corkscrews (the extra 1.2 m only on open road); geomcheck checks collision walls to 0.15 m from the lane edge
- **racer**: A wacky 90s radio DJ announcer (am_michael pitched down, a close-miked booth chain, every line rewritten; Whisper hears them all), karts right themselves near the road instead of rolling into the barrier, and the road holds a kart 7 m out in loops and corkscrews (bounced off upside down, karts fell out of Orbit's corkscrews)
- **racer**: Weaker CPU drivers hold fire at a human: a Rookie never fires a missile at one, a Pro only from 25 m or more back (time for the INCOMING warning and a hop); hits on the player over 8 maps 24 -> 19
- **racer**: No more falling out of loops and corkscrews: off the deck in one, a kart stops drifting away from the road and bends back toward the tube's middle, the road's hold reaches 6 m past the edge there, and a mine's blast is only a hop inside a loop (Orbit's corkscrew falls 2-6 a race -> 0; all maps 17-24 -> 11)
- **racer**: No invisible wall at loop mouths: the loop speed cap snapped a kart over 40 m/s to 40 in one tick (10-15 m/s gone at every loop's entrance); karts now ease toward 45 m/s over the last 40 m before a loop, all 16 loops taken at 50 m/s with no fall and at most 1.4 m/s shed a tick. Wall contact costs speed by how square the hit is (a graze next to nothing). RACER_BUMPLOG=1 logs any sudden unexplained stop
- **racer**: RACER_BUMPLOG logs each sudden stop once with the nearest kart's distance; RACER_TEST_AT takes a lateral place (s:speed:lat)
- **racer**: A crooked landing keeps its speed: the slide turns into travel along the nose at the same pace (it threw away a third of the sideways speed each tick, 24 m/s in two ticks off a crooked crevasse landing)
- **racer**: Ace drivers drift the medium bends too (radius under 125 m); falls over 8 maps 11, lead changes 4-19 a race
- **racer**: The map banner says 1 LAP, not 1 LAPS
- **racer**: The battle HUD as the race's: no panels behind the balloons, pops and clock (outlined on the scene, the clock flashing red at the end), no standings list in the round, a smaller see-through arena map
- **racer**: No ground over the road where a road runs along the edge of a big ground cell: a cell now splits when any road point lies inside it (the nearest-road lookup from the cell's middle gave up past ~150 m, so such cells stayed coarse and their triangles rose over half the deck, the Refinery's straight before the line); the ground keeps a bank under every road (just below it, rising 1:1 for 12 m). Dev aids RACER_GROUNDQ, RACER_GROUNDV; geomcheck reports up to 4 m
- **racer**: Playtest round 3 on engine v0.16.0 (rumble live), distance-based catch-up, a wacky announcer, the jungle cleaned up
- **racer**: The menus show the real 3D karts, not 2D drawings
- **racer**: The announcer's line ends gated (the squeeze raised end-of-word breath into an 's': 'baby' heard as 'babies'); the loading screen holds black until the map's lanes are cleared and the race waits for it; no minimap prebuilds during a race (a 40-100 ms hitch every sixth frame at the start)
- **racer**: Capture the Flag and King of the Mountain, two new arenas, and three new weapons
- **racer**: Battle balloons smaller (radius 0.36 -> 0.28 m), half a metre higher and translucent (55% opacity, no shadow) so they mark a kart without hiding the arena
- **racer**: The new mode and weapon announcer lines rebuilt through the gated voice chain, a few reworded so they are heard right
- **racer**: Two long capture the flag maps (Twin Spires, Mesa Run), round-first arena menus, teleports, boost strips and CPU routes
- **racer**: Twin Spires rebuilt as one long asteroid: arena_terrain (a rock top cut to any outline, sampled into a collision mesh with a rim round its edge), two 34 m paths rising 20 m to a crossing in a double X, broad jagged islands, 184 m stone towers with the flag in a lit gatehouse hall, a galaxy and a bigger blue world in the sky
- **racer**: Twin Spires: the towers' light named for the tower
- **racer**: Twin Spires rock tinted olive-brown, flag carrier reads YOU HAVE IT; examples on cartwheel-engine v0.17.0
- **racer**: A diving board off each Twin Spires tower roof (a gap in the battlements, a deck and a kicker angled down the near path, a rock apron where it lands); long arena drops level out in the air and land without a tumble; RACER_BATTLE_AT takes a height
- **racer**: Mesa Run in red sandstone at sundown: the butte, spires and boulders in the strata's own warm colours (no mossy rock texture in the desert), a later sun, warm haze on the horizon
- **racer**: A streaked weathered rock texture (grey-olive and brown bands, dark seams, pale grit) for Twin Spires' ground and rim
- **racer**: Twin Spires' towers heavier: corner buttresses with stepped plinths and sloped caps up every stage (the foot's from 34 m, over the karts), stone string courses round each stage
- **racer**: No falling short of a jump (the ramp carries a kart up to the jump's design speed over its last 14 m); the fairground helix banked and walled tall from the drop, which a boosted fast-class kart leaves at 74 m/s; CPU drivers brake for a bend up to 126 m ahead and fire turbos only with 150 m of open road
- **racer**: Loops and corkscrews keep a kart rolling (at least 14 m/s along the road): one slowed by a bump in Wreck Orbit's corkscrew stalled 3.5 s until put back
- examples on cartwheel-engine v0.18.0 (three.c v0.186.20)
- **racer**: Program tables rebuilt for three.c v0.186.20 (the double-sided water states: water draws were skipped on Drowned Downtown and Spillway Dam)
- **racer**: Twin Spires' paths bend at sharp diamond corners, as the old map's do (were rounded)
- examples on cartwheel-engine v0.18.1: the dual cart only (no GL-only cart tree or SDK), SDK files dated at extraction
- **racer**: Scattered scenery never stands above the ground as drawn (cut down beside the roads), the ground record reset for each map, Frostbite's slope rocks set deeper into the snow; RACER_FLOATLOG lists props over a metre above the ground
- examples on cartwheel-engine v0.19.0
- **racer**: Capture the flag karts wear a rim light in their team's colour (engine v0.19.0), so a kart across the field reads red or blue; program tables through the engine's programs tool, which composes the rim with the clustered lights
- scripts/build.sh refuses a GL-only cart tree (only the dual cart is built; GL is tested by running it on a GL host); README without the GL-only cart
- **racer**: Split screen draws less: a kart far from a view draws as its merged far version (one mesh per material instead of about thirty parts; each view picks, 22 m in split screen, 45 m in one), bigger prop cells and longer track chunks when there is more than one view; 4 players mid-race 20-27% fewer draws
- **racer**: Fewer draws per view: the ground merges 256 m blocks in split screen (128 in one view), and a prop cell is not drawn in a view where its biggest copy would be under two pixels tall (per-view LOD, distance from the prop's size, the view's height and field of view)
- **racer**: A near kart's tyres, rims, arms, forearms and gloves draw as instanced sets (one draw a set, culled as the kart) placed from their part objects each frame; tables for the new states
- examples on cartwheel-engine v0.19.1 (three.c v0.186.21); racer tables with the time-trial ghost's instanced parts
- **racer**: A second's grace after a race spin before another can start; CPU drivers never fire at a human in the first 8 s, and an Ace only from 15 m back (6 left no time for the warning)
- **racer**: Frostbite Pass rocks sit in the snow: sunk by the slope under them (a level rock on a steep face showed its underside and looked stuck on), leaned with it, and dressed as slate with snow on the faces that look up
- **racer**: Spillway Dam at a real midday: a clean azure sky (less air in the sky model, a midday grade from tools/dam_grade.mjs), far mesas and spires in the canyon's sandstone, and no red slab of land through the corkscrew's tube when it rolls
- gitignore the agent worktrees directory
- **README**: The racer's modes, items and a screenshot; CI's nightly gates split by renderer and game so they finish inside the job limit (one job was cancelled at 4 hours every night)
- **racer**: Overgrowth Temple polish: the waterfall's plateau ends behind the falls (it stood 34 m high over the start, the S-bend and the first tunnel: a dark 45-degree bank beside the road and an earth slab through the tube), tunnels stand in a clearing, carved stone tunnel lining lit by the torches, light beams stand on the sun's side of the road and lean away from it
- **racer**: Export presets per platform target like the other games (CI packaged nothing for racer and failed every native job)
- racer tools/run.sh: runs through the machine-wide GPU suite lock (RACER_NOLOCK=1 inside a suite)
- **racer**: Wreck Orbit's spacewalk is 8.8 m wide instead of 6.8 (CPU packs lost one to four karts off its first bend each pass at the Fast class: now 7 falls -> 2 over the three classes' first pass); its time trial reference re-measured (7810 -> 7812)
- **racer**: Drowned Downtown's glass tube reads clear (the sea's underwater view gets 60 m of clarity and softer light shafts instead of a 5 m teal wall); Salt Flats Run soft-edged clouds and a thinner low haze over the pan
- **racer**: A kart is never a ramp (one riding up onto another, a spun kart in the lane, cannot rise or pitch up off it: the player took off 8 m over the Dam's wall before its tunnel), and a hit inside a loop or corkscrew shakes the kart and costs a quarter of its speed instead of spinning it backwards through the tube

## v0.11.0 (2026-10-22)

- Add Liberty Leap bus jump stunt course

## v0.10.7 (2026-10-22)

- Steady BikeMesa corkscrew camera and refine riders

## v0.10.6 (2026-10-22)

- Add BikeMesa bikes, rider, ski jump, and storm clouds
- Pin cartwheel-engine v0.15.0 SDKs

## v0.10.4 (2026-10-21)

- Fix BikeMesa lane visuals and default course route

## v0.10.3 (2026-10-21)

- Improve Enchanted Forest character materials and silhouettes (#3)
- Add Helix Harbor corkscrew course to BikeMesa

## v0.10.2 (2026-10-21)

- Improve Enchanted Forest character materials and silhouettes

## v0.10.1 (2026-10-21)

- Add two Downtown storefronts and ground forest replay scenes (#2)

## v0.10.0 (2026-10-21)

- Prepare six games for the next release
- Build enchanted forest encounters and move opening loop
- Make deer crossings avoidable after braking
- Pin cartwheel-engine v0.14.0 source
- Port games to engine v0.14.0 and deepen forest

## v0.9.0 (2026-10-18)

- **Derby**: Cars scrape and scorch (mesh decals on the bodywork at dents, soot round fires, restamped as dents grow)

## v0.8.4 (2026-10-17)

- cartwheel-engine v0.13.0 SDKs (mesh decals lit like their surface)
- **BikeMesa**: The start line and its flag sit just ahead of the front tyre (the bike started 2 m past them)

## v0.8.3 (2026-10-17)

- **Chromite**: The hatches' iris blades stack at staggered heights (they z-fought round the lamp and flickered)

## v0.8.2 (2026-10-17)

- **README**: Chromite's blurb without the fixed tick (an implementation detail, not a feature)
- Chromite's project.cwproj, so the release packages it for every platform; CI fails when a game in GAMES packaged nothing

## v0.8.1 (2026-10-17)

- CI and releases include Chromite: GAMES and the auto gates list it, and the README counts six carts

## v0.8.0 (2026-10-17)

- **Chromite**: The chrome's live probe is the engine's ReflectionProbe3D, capturing the same stand-in (sky dome, scrolling deck plate, flash glows) on a layer of its own, 128 every other frame at every preset
- **Chromite**: Clustered lighting (LightingSettings CLUSTERED): a light per blast from a pool of 32 that reaches the deck below, and every deck lamp lights the plating round it; cheat.blasts sets off a field of blasts for lighting work
- **Chromite**: Program tables for the clustered and GI shading parts (18 states, from 38: every lit material now draws with cluster_gi)
- **README**: Chromite's reflections are a ReflectionProbe3D, and its blasts and deck lamps light the deck through clustered lighting
- **Derby**: The floodlights and the fires light the bowl, beams in the dust
- **Derby**: Real burn scars under the wrecks
- **BomberDude**: Blasts light the arena, a clustered light over every burning tile
- **Spout**: The paper wraps' loose ends lift in the wind and rain
- **README**: Derby's floodlights, fires and dust, a new screenshot
- **Pugball**: Real floodlights at night, cascaded sun shadows, cloth goal nets
- **Pugball**: The stadium's reverb, and headphones with 3D sound
- Spout's spider clings to the pipe: legs grip it, eyes on top, turns out when washed down
- **BikeMesa**: Real land behind the bank (Terrain3D) in the canyon, alpine, Moon and Mars worlds
- **BikeMesa**: Scary Town's ground mist (volumetric fog and a drifting FogVolume3D)
- **Pugball**: README screenshot (the cat in the back of the cloth net), from the cart in romdev
- **BikeMesa**: The canyon backdrop's boulders only, drawn to 240 m (fewer draws a view)
- **Pugball**: The night's floodlights tuned for cost
- **Derby**: Floodlight shadows follow the Shadows setting
- **Chromite**: The probe shadow workaround goes, cartwheel-engine v0.12 redraws the sun's shadow maps after a probe capture
- cartwheel-engine v0.12.1 SDKs (clustered lighting, GI, volumetric fog, terrain, cloth, HRTF, and the probe shadow fix)

## v0.7.0 (2026-10-16)

- **Chromite**: A vertical shooter after Chromium B.S.U., in three.c
- **Chromite**: Gritty art pass, 3D title logo and instruments, live reflection probe
- **Chromite**: The original's proportions and framing, contrast, a working probe sky, gates
- **Chromite**: README screenshot, from the cart in romdev
- **Chromite**: Tank rebuilt after the original sprite, enemy guns face the player, mirror-chrome hero, cheat.gallery lineup
- cartwheel-engine v0.10.0 SDKs (three.c v0.186.11: PMREMGenerator.fromScene options, which Chromite's reflection probe uses)
- cartwheel-engine v0.11.1 SDKs (many lights and shadows, light bounce and reflections, limbs that reach and plant, cloth, big open worlds, 3D sound, water, fog and more)

## v0.6.0 (2026-10-15)

- Build against cartwheel-engine's prebuilt SDKs (v0.5.2), and native builds for every platform
- cartwheel-engine v0.5.3 SDKs; dev carts by default (RELEASE=1 for release carts, which CI publishes); CI installs libEGL for the replays
- **native-replay**: A real window on macOS (ANGLE cannot back SDL's offscreen driver)
- cartwheel-engine v0.6.2 SDKs (GL on macOS, GLES by soname on Linux, the game shell); native jobs get 4 hours (software rendering)
- cartwheel-engine v0.6.3 SDKs: headless GL on macOS, so the replays run without a window there too
- cartwheel-engine v0.6.5 SDKs; macOS: replays on WebGPU, GL launch-checked (native-replay --smoke), GL on those runners drawing about a second a frame
- cartwheel-engine v0.6.6 SDKs
- cartwheel-engine v0.6.7 SDKs
- CI in minutes: pushes replay each game's first 300 frames on every platform and API, held to Linux x64 GL's state bit for bit; the full gates run nightly
- **CI**: The macOS GL launch check on one game for 10 frames (those runners draw the games' GL at seconds a frame)
- **CI**: 200-frame slices, and one game's slice on the Windows runner (WARP draws on the CPU); the comparison takes runs of some of the games
- Graphics settings page in every game: each setting on its own, Auto by default
- cartwheel-engine v0.7.0 SDKs (graphics settings)

## v0.5.1 (2026-10-15)

- cartwheel-engine 44c086b: three.c v0.186.5 (less renderer CPU per draw)
- cartwheel-engine efc5f62: three.c v0.186.5, and the engine's WebGPU draws into one target share a render pass

## v0.5.0 (2026-10-15)

- cartwheel-engine 714bbad: wasmcart v0.34.3
- cartwheel-engine ad46681: post fusion on WebGPU, the fog inside the SSAO composite, the lens inside FXAA or the tonemap pass

## v0.4.0 (2026-10-15)

- Derby split screen: lighter views
- **Derby**: The chrome details back in their own mesh
- Derby split screen: full detail for the cars close by
- cartwheel-engine f4d38cc: GPU particles in one compute and one render pass per view, small sorts in one dispatch (WebGPU) or two passes (GL)
- Derby's split-screen benchmark: side-by-side pairs
- cartwheel-engine 5e53ba4: a shared post chain's views lay out their scene targets directly (one copy pass fewer per view)

## v0.3.0 (2026-10-14)

- **Derby**: Split screen for up to four players
- **README**: Pugball's split screen for up to four players, and its benchmark
- BikeMesa split screen: one draw per far rider, no grass in quarter views
- **Derby**: Fewer draws a view
- Derby's split-screen benchmark, and the README on Derby's split screen

## v0.2.0 (2026-10-14)

- **README**: A daytime Spout screenshot
- **BikeMesa**: Split screen for up to four players
- BikeMesa split screen: cheap views, pads checked per player, a benchmark
- gates.json: leave Pugball's split-screen gates to Pugball's own commit
- **Pugball**: Split screen for up to four players

## v0.1.0 (2026-10-14)

- Five games built with cartwheel-engine: BikeMesa, BomberDude, derby, pugball and spout
- **CI**: Two jobs, the carts (uploaded as an artifact) and each game's _auto gate on GL and WebGPU; the full gate set runs locally
- Remove tests/ci.mjs; README describes the two-job CI
- **README**: A screenshot of each game
- **README**: One screenshot per game, alt text matching the shots
- **Releases**: A v* tag publishes the ten carts as a GitHub release
- One cart per game: the GL + WebGPU cart (it falls back to GL on hosts without WebGPU)
