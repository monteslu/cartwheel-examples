# BikeMesa: user requirements and work ledger

Updated 2026-10-20. This file preserves the user's requests across context changes.
The full objective is **use the latest cartwheel-engine features to make the games performant and beautiful**, including the other cartwheel-examples games. This is not complete.

## Rules that apply to all work

- Use romdev throughout development: inspect actual screenshots and gameplay, not just successful builds.
- Beauty and performance both matter. The user's approximate target on this machine is a 4 ms frame budget. Account for other programs running on the PC; identify GPU/backend, warm-up, settings and sample window before comparing timings.
- Large scenes should keep multiplayer state resident while efficiently culling offscreen rendering. Use current cartwheel-engine/three.c capabilities; push generally useful conveniences into the engine when appropriate.
- Improve all examples, not only BikeMesa. Do not claim that one game's result proves all examples are done.
- Every map should have at least two distinctive fun things to notice. Spread discoveries through the ride; **do not spoil the interesting things at the beginning**.
- Vary colors, silhouettes, sizes, poses and arrangement, especially circus, candy and toy levels. Avoid endless identical props with a color swap.
- Preserve challenging, long, fun courses. New scenery must not silently shorten maps. Physics stunts must actually be possible, even if difficult.
- User authorized implementation and testing without further permission checks. No commit/push requested.
- Read original reference images before modifying real buildings. Clear signs in photographs override website logos only when reproducible; otherwise use official logos. A screenshot file existing does not prove fidelity.
- Keep this ledger honest: distinguish implemented, visually checked, gameplay checked, and still pending.

## Current build and verification paths

- Working user-test cartridge: `build/whimsy-cart/carts/bikemesa.wasc` (absolute prefix `/home/monteslu/code/cliemu/cartwheel-examples/`). Keep it updated.
- Cart build: `cmake --build build/whimsy-cart --target bikemesa -j 6`.
- Native build: `cmake --build build/whimsy-native --target bikemesa -j 6`.
- Native executable: `build/whimsy-native/project/games/bikemesa/bikemesa`.
- Examples currently build against `build/whimsy-engine`; mirror applicable engine changes to `/home/monteslu/code/cliemu/cartwheel-engine` and the pinned build copy.
- Landmark source images, official logo sources and order: `landmark-references.json`, `mesa-layout.h`, `references/signs/`, `CREDITS`.
- Recent visual evidence: `out/neon-28-venues/` at repository root.

## Immediate outstanding work

### Big Top Tumble — track 14

Implemented and checked in romdev:

- 24 m diameter loop (radius 12), over twice Scary Town's 11.2 m diameter.
- Physical pedal-powered assist with speed-limited thrust. Excessive entry speed previously collapsed suspension and crashed; simply increasing approach boost was not a solution.
- Repeated full-course rides cleared the loop and finished with **loops=1, crashes=0, finished=1**, including after new tent/clown builds. No GPU errors or missing shader variants in those runs.
- Releasing pedals before entry failed to complete the loop: loops=0. No scripted bike movement.
- Regression definition added to `tests/gates.json` as `bikemesa-loop-giant`, control `tests/bikemesa-loop-giant-coast.json`. Full gate passed on native, romdev and Chromium on GL and WebGPU on2026-10-21; see giant-loop gate entry below.
- Wider camera around giant loops implemented. `circus-loop-wide.png` captured but still needs visual review.
- Eight varied tents: curved cloth, continuous vertical stripes, low walls, scalloped trim, poles, guy ropes, entrances, varied footprints and four palettes. Largest tent is broad and two-pole. User explicitly approved the newer tents: “much better tents.”
- Five full-bodied juggling clowns, animated balls, large shoes, ruffs, colorful suits and hats. Removed floating clown heads. Checked `circus-tents-new.png` and `circus-main-tent.png`.

Still requested:

- More circus action, including elephants and lions. Do not mark those done: not added yet.
- More unusual physics in the last levels: user approved ideas such as seesaws/collapsing bridges, rotating hamster-wheel stunts, circus cannon, pendulums, low-gravity transfers and UFO tractor beams. These are a direction, not proof of implementation; assess what works with the real physics.
- **Julius Fučík, Entry/Entrance of the Gladiators music**, compact and loopable. User's “not huge” refers to the music asset, NOT reducing the giant loop.
- Music source found: public-domain U.S. Marine Band performance, circa 1999, Timothy W. Foley. `/tmp/gladiators.oga` downloaded (~7.9 MB, 155.6 s). Not yet edited, integrated, credited or loop-verified.
- Source: https://commons.wikimedia.org/wiki/File:Julius_Fu%C4%8D%C3%ADk%27s_%22Entrance_of_the_Gladiators%22,_performed_by_the_U.S._Marine_Band.oga
- Tent research: https://www.bigtopmania.co.uk/tents/red-rudi-circus-big-top and https://www.flynncreekcircus.com/vintage-big-top-for-your-event/tent-specifications/ . References inspired procedural geometry; no copied photo textures.
- Save v8 resets old circus time/medal after changed track. Save migration test explicitly seeds/asserts circus and neighboring slots; versions3–8 passed on2026-10-21 (see save-migration coverage entry).

### Candy Comet — track 15

User wants a wild confectionery-factory / Willy Wonka feeling:

- More different candy types, not repeated lollipops and identical cupcakes.
- **Translucent jewel-colored gumdrops.** Use real engine physical/transmission features where appropriate; verify shaders and measure cost. Prefer a few meaningful translucent objects over blanket overdraw.
- **Glossy chocolate waterfall and pool**, visibly flowing.
- Planned prop variety: wrapped sweets, gumdrops, gummy bears, donuts, candy canes, layered cakes, existing lollipops/cupcakes in varied colors and sizes.
- First variety pass implemented: wrapped bonbons, iced donuts with sprinkles, chunky gummy bears, and tiered cakes, alongside lollipops/cupcakes. Sizes and color combinations vary; additional types begin after ~75 m. Cart built and romdev track 15 frame 850 checked: x114.84, crashes=0, GPU errors=0, missing shaders=0; `candy-variety.png` captured.
- Translucent gumdrops, candy canes and chocolate waterfall remain unimplemented. Gummy bears currently use opaque candy material, not transmission.

### Clockwork Toybox — track 16

- User called it boring and wants a rethink, with weird physics and much greater prop variety.
- Add toy planes, ducks, rockets, dice, trains and jack-in-the-boxes, mixed with existing robots/blocks. Different colors, proportions and arrangements.
- These new types and the gameplay rethink are NOT implemented yet.

### Christmas Town — track 10

- Santa should be in the middle, not at the beginning.
- Implemented Santa position at half the finish distance and tree clearance around it; screenshot verification still outstanding.

### Powder Line — track 2

- Two abominable snowmen / sasquatches, walking away at an angle, one looking back like the famous old photograph.
- Not near the beginning; discoveries around middle and later sections.
- First procedural implementation in `discoveries.inc`: white walker at 52%, brown walker at 79%, articulated walking limbs, retreating movement, fur tufts and footprints.
- Built and romdev checked at frame ~1200: bike x196.28, crashes=0, GPU errors=0, missing shaders=0. `powder-discovery.png` reviewed.
- Visual still needs refinement: first walker reads too blocky/robotic, backward glance not obvious, footprints too blue, walking loop resets need concealment, terrain contact needs inspection. Do not present this first pass as final fidelity.

### Temple Vines — track 4

- Stonehenge-like weathered standing-stone circle, lintels, fallen stone, vines; smaller ruins along approach.
- First procedural implementation at 64% of finish distance in `discoveries.inc`. Built, not yet inspected in romdev.
- Still needs actual chipped/irregular stone silhouettes rather than plain boxes and appropriate terrain seating.

### Moon Base — track 11

- Crashed Apollo lunar lander and astronaut.
- First procedural implementation at 62% in `discoveries.inc`: tilted foil-colored descent stage, cabin, landing legs/feet, ladder, antenna, astronaut and gold visor.
- Built, not yet visually checked. Needs fidelity/polish and performance review; current model is procedural, not an imported NASA model.

### Red Planet — track 13

- **Find models of Sojourner, Spirit, Opportunity, Curiosity, and Perseverance**, distribute them at different places on Mars level.
- NASA originals downloaded to `references/rovers/`: Curiosity, shared Spirit/Opportunity, Perseverance. URLs and SHA-256 recorded in `sources.json`. Spirit/Opportunity should be separate discoveries with differentiated scene context. No runtime integration yet.
- `tools/prepare-rovers.sh` (glTF Transform 4.5.1) produces review candidates: Curiosity 33,631 triangles/9 primitives/4.12 MB; MER 25,240/9/5.07 MB; Perseverance 61,854/119/2.57 MB. These are not approved runtime budgets or visually verified. Perseverance still needs material/draw-call consolidation and its WebP texture extension checked.
- Sojourner CC0 model by argonius found at https://blendswap.com/blend/15250, but download requires sign-in. Continue looking for an accessible model; do not claim downloaded. NASA legacy VRML URL redirects to mission page.
- NASA model sources found:
  - https://science.nasa.gov/resource/spirit-and-opportunity-rover-3d-model/ (11.36 MB glTF).
  - https://science.nasa.gov/3d-resources/mars-exploration-rover-opportunity-mer-b/ (35.81 KB GLB; inspect quality).
  - https://science.nasa.gov/3d-resources/mars-2020-perseverance-rover/ (4.76 MB GLB).
  - https://github.com/nasa/NASA-3D-Resources (official model repository).
- Find appropriate Sojourner and render-ready Curiosity models; the Curiosity “Detailed” search result was printable STL parts, not yet a suitable runtime choice.
- Inspect geometry/material/texture costs, simplify for runtime while preserving recognizable features, retain provenance/credits, verify in romdev.

### Desert map and other maps

- User specifically suggested aliens and a crashed flying saucer with smoke on a desert map.
- Not implemented yet. Every map needs at least two fun details, discovered progressively.
- Existing red rock, autumn, coast, cosmic, scary, beach and other maps still need a systematic detail/variety audit.

### Level select

- User asked for a bigger, brighter map-selection ring.
- Implemented in `main.c`: constant bright gold outer border, ivory inner border, larger pulsing halo (core does not dim away).
- Built in current cartridge, screenshot/input-focus verification pending.

## Downtown Mesa / Neon Rooftops — track 3

All 28 buildings have first-pass implementations, but “present” does not mean final visual fidelity. Preserve architecture, reference photos and latest order. Course finish is 873 m, total 933 m, 10 checkpoints. User wants long and fun, not tiny facades squeezed together.

Authoritative start-to-finish order:

- Worth Takeaway
- Burritoholics
- BRI Taproom & Arcade
- NCT / The Sacred Pint
- Gus’s World Famous Fried Chicken
- Oro Brewing
- Chupacabra Taproom
- Main Burger
- Arizona Distilling Co.
- Phantom Fox / Goat & Ram
- Que Chevere
- Tacos Chiwas
- Proof Bread
- Espiritu
- The Nile
- HeatSync Labs
- Organic Bean Cafe
- Casa Copal
- Diamond Don’s
- Level One
- Mad Candy
- Mangos
- República Empanada
- Margaritas
- Lost Dutchman Coffee
- Hope’s FryBread
- Pizza Mart
- The Guadalupe on Main

Specific requirements to preserve / verify:

- City-like background with streets, shops and realistic-looking cars, better night lighting, much less mountain backdrop. Asphalt track sections. Avoid toy-car silhouettes.
- Buildings wider and substantial, but preserve gaps and course length.
- Worth and Burritoholics are two adjacent businesses.
- NCT and The Sacred Pint share one building; bar up front.
- Nile and Level One are taller TWO-STORY buildings. User rejected an inaccurate Nile. Nile must match supplied photo; replace temporary “yoga” wording with “Concert.” Level One was raised 15% but still audit reference fidelity.
- Phantom Fox: wordmark only once, Goat & Ram sign too. Widen building without enlarging sign.
- Organic Bean Cafe must fit between trees with whole sign visible.
- BRI sign lettering must not have a white line obscuring its bottom.
- Real neon matters: Chupacabra circular animal-head sign and outer ring both glowing pink; Arizona Distilling Co. neon too. Bright colored actual neon signs should glow; not every sign is neon.
- Iconic projecting/roadside signs: Chupacabra, Oro, Phantom, Gus’s, Que Chevere, Nile.
- Pizza Mart: unique pointy A-frame architecture, iconic yellow sign; second-to-last. Guadalupe MUST remain last.
- Mangos facade is pale green.
- Espiritu expanded into former Harvest: use ESPIRITU's unusual angular wordmark, NOT fish graphic and not Harvest branding. User supplied official animated GIF; first frame currently runtime PNG.
- Espiritu GIF: https://images.squarespace-cdn.com/content/v1/61d7577ac495cf3932c6b5a8/f67ce868-27db-4a6d-b194-7549a3421b82/Untitled_Artwork+3.gif?format=1500w
- Hope's: cream stucco frame, wood fascia, turquoise sign based on HopesFrybread.png.
- Original reference images are under `/home/monteslu/Pictures/Screenshots/`; exact paths retained in `landmark-references.json`, including all 28 buildings and paired businesses. Gus closeup additionally `/home/monteslu/Downloads/gus's2.webp`; early artifact screenshot `/home/monteslu/Downloads/hsl.png`.
- Early pink “laser” line was reported by user; review any remaining fluorescent track-side strips as intentional scenery, not unexplained artifacts.
- Lost Dutchman foliage was changed from rock-like blobs to leaf meshes; missing vertex-color double-sided shader variants were then added and rebuilt. Post-fix Neon visual verification still outstanding.

Recent performance evidence (do not overgeneralize):

- `out/neon-28-facades-perf/results.json`: two RX7600 native GL 1080p High runs, 5000 frames, last 2048 samples include finish screen; avg ~1.90 ms, p95 ~2.33 ms, max ~12–14 ms. Finished, zero GPU/missing shader errors.
- This predates latest foliage/discovery additions and is not a clean old-vs-new comparative benchmark.
- Need new measurements for heavier maps, transmission effects and imported models. Account for other PC activity.

## Shared engine / examples scope still open

Existing work includes font preloading in all six games, visibility/culling fixes, sort/shadow-cache fixes, froxel early rejection, mesh/lightmap fixes, and some Pugball/Derby improvements. Do not restate old isolated tests as proof of full completion. Preserve root/pinned engine consistency and run relevant gates for new changes.

## Next actions, not just promises

1. Verify level-select ring and current discoveries in romdev; improve their visible defects.
2. Finish compact looping circus music and credits; inspect loop camera; complete remaining circus acts.
3. Implement candy/toy variety, translucent gumdrops and chocolate waterfall. These are still unimplemented despite discussion.
4. Obtain/prepare/place five NASA rover models; audit runtime cost.
5. Add remaining map discoveries and unusual playable physics, then audit all 16 maps and all six examples.
6. Re-check Mesa fidelity/foliage and run representative performance measurements. Keep goal active until requirements are actually satisfied.

## Latest composition correction: depth and independent scale

User rejected toys/candy lined along the track like street decorations. Required: a town being ridden through, with depth, mixed sizes, and varied arrangement; not a uniform scale by prop type.

Implemented first composition pass in `whimsy.inc`: six settlement clusters per candy/toy map, three buildings at different depths per cluster, winding terrain-following access paths, props distributed from ~7 to ~57 m behind the course, and nonuniform sizes independent of depth. Candy houses use layer-cake construction; toy buildings use stackable construction blocks with studs and some towers. Existing small candies form scattered clusters instead of one roadside row. Single-player camera pulls back 3 m for both themes.

Romdev visual checks: `candy-depth.png` (first pass), `toy-depth.png` (after path subdivision fix), zero GPU errors/missing shaders and zero crashes at the sampled points. Not a full performance validation. Remaining: improve settlement architecture/terrain seating, broaden toy types, add the specifically requested gumdrops/waterfall and other landmarks. Do not treat this composition pass as completion of those requests.

### Gumdrop and path follow-up

- Added 12 physical-transmission gumdrops in three later-course clusters, four absorption colors and varied sizes. Generated `physical+shadow+senv+transm+l1001` for GL/WGPU after romdev reported the missing state; subsequent runtime reported zero missing/GPU errors. Their final appearance and frame cost still need a focused check; first screenshots were poorly placed or occluded.
- User rejected the settlement paths as weird ribbons. Removed continuous strips; replaced with separate wafer-colored / cream paving blocks seated and tilted to terrain. Needs additional camera views, particularly Toybox.
- User requested real gummy-bear shapes. Replaced separate sphere limbs/black eyes with a single smooth blended mold and raised facial features. Generator: `tools/make-gummy.py` (numpy), output `gummy-mesh.inc`: 3,058 vertices / 6,112 triangles, shared geometry. Romdev `candy-molded-bears.png`, frame850: zero crashes, GPU errors or missing shaders. Still opaque colored candy; gumdrop transmission is separate.

### Chocolate waterfall first pass

Implemented `chocolate_falls` at 66% of Candy Comet, z=-25: layered cocoa/wafer cliff, glossy curved curtain, plunge pool, 16 falling accents and four expanding ripples. Uses existing opaque physical materials; no extra local lights or transparency passes for this landmark. Romdev frame1650: x264.03, crashes=0, missing shaders=0, GPU errors=0; `chocolate-falls.png` visually reviewed. Shape/lighting/flow still merit polish and performance measurement. Screenshot exposed reversed paving slope rotation; corrected both tilt signs and rebuilt afterward (post-fix visual check pending).

### Toy variety first pass

Added propeller planes, retro rockets, rubber ducks and dice to the existing robots/block stacks; mixed palettes, sizes and settlement depths retained. New types appear beyond the opening cluster. Built cart and checked track16 frame1000 in romdev: x159.52, crashes=0, GPU errors=0, missing shaders=0; `toy-variety.png`. Trains, jack-in-the-boxes, more animated acts and the gameplay rethink remain pending. These additions reuse existing geometry/material types, but their performance has not yet been benchmarked.

### New requested Toybox mechanism

User requests lightweight toys strewn on the riding path: very low density so they barely resist the bike, knocked into 3D space, colliding with one another. Implement using the available 3D physics integration (user suggested “box3d”); inspect engine/body collision and existing crash-world bridge before choosing architecture. Preserve multiplayer determinism/residency, readable route and low runtime cost. Do not substitute decorative animation for actual colliding physical toys. Not implemented yet.

### Gummy-bear correction, second pass

User rejected first shape and opaque material. Looked at real gummy candy reference (https://assets.office-discount.at/img/2/7/Zoom_m2418365.jpg): broad integrated ears/head, forward paws, large feet. Updated the mold proportions, impressed smile, 11,564 triangles shared mesh. Bears now use physical transmission .96, IOR1.45, thickness .8 and four absorption colors; no opaque painted eyes. Cart built and romdev frame1550 captured `candy-jelly-bears.png`, zero GPU errors/missing shaders. Visual acceptance and updated performance still need confirmation; previous benchmark predates this mesh/material change.

Candy performance before latest bear correction: `out/candy-depth-perf/results.json`, 1080p High locked native GL, 2200frames/1800samples, two runs avg1.6554/1.6028ms, p952.3684/2.1159ms, max34.7882/4.7292ms. Renderer/system data recorded; inspect those before reporting hardware attribution. No before/after claim.

## Active play mechanics / latest user corrections (2026-10-20)
- Toybox must be messy, not collectible rows. User requested more scattered toys,
  teddy bears, beach/basket/kick/soccer balls, big dice, rubber chickens, and two
  breakable block walls especially beyond jumps. Implemented first pass: 72
  irregularly clustered toys + 56 wall blocks, Box3D compound/sphere/box bodies,
  low density, shared crash scene, kinematic riding-wheel interaction. No reaction
  fed back into 2D bike (intentional negligible resistance). Validate gameplay,
  multiplayer determinism, collider/visual match, wall impact, and performance.
- Sounds: compact authored soft ball bounce, muted block/dice thud, chicken squeak;
  impact speed sets volume, per sound cooldown. Must listen and verify triggers.
- Wrapped bonbons rejected: replaced with striped candy canes.
- Chocolate pool floating: replaced ellipsoid with terrain-conforming surface;
  waterfall endpoint lowered. Needs visual review of joins.
- Two chocolate rivers cross Candyland at 43% and 66%; sticky grounded drag,
  hopping avoids it. Check clearance/elevated paths and tune after gameplay.
- User requires tires carry chocolate beyond crossings, marks persist entire map
  for every multiplayer rider. Implemented Decal3D screen prints (engine batches),
  14m carry distance, opacity tapers along trail; no lifetime or recycle limit.
- Crash into chocolate: three mesh decals on rider/bike, once per rider per map.
  Needs actual crash screenshot, near/far LOD and shader verification; cannot yet
  claim visually accepted. No splash particles yet.

### Expanded Toybox finale and chocolate corrections (latest)
- User asked for WAY more road toys, closer camera, fun breakables, and a huge
  ball-pit after the final finish, watching for several seconds before results.
- Latest code: 180 scattered toys, THREE 28-block walls (116/246/286m), 432
  Box3D pit balls (696 dynamic toys total). Pit uses four instanced meshes;
  clear physical-transmission plexiglass walls, colored rims. Seven-second
  results delay; result locked at line; a non-scored ragdoll tumble at finish+5m.
  Needs current glass/impact/visibility, complete ending and multiplayer tests.
- Removed Toybox's extra +3m scenery zoom. Candy retains its extra framing.
- User specifically wants chocolate splash droplets to leave mess on surfaces,
  rider and bike. Added 24 pooled visual ballistic droplets per rider, Box3D
  raycasts along travel, persistent screen splats on scenery, body hit callback.
  Need verify actual body staining and deposits, not just creation of decals.
- Found engine bug: mesh decals born during pre-render clipped using stale
  render-object origin while snapshotting authored position. Changed mesh_build
  to use cw_node3d_global_transform; mirrored pinned/root engines. Test changed
  0 mesh pieces to real clips. Temporary diagnostic log removed.
- Hidden rider construction meshes consumed decal clipping budget. Added Skin
  receiver node with only merged drawable skins, far first; targeted stains there.
  Added missing standard+map+trans+shadow+skin+senv+l1001+xmeshdecal shader.
  Current build/visual check still pending. Regression helper `chocolate crash`
  spawns a repeatable river tumble; `chocolate` reports decal stats.
- Smooth chocolate normals and corrected pool winding, grounded surface; moved
  crossings from mostly-airborne 43/66% locations to flat 34/73%, drag 1.8/s.
  Romdev solo Candy completed 0 crashes, 62 retained tire prints, GPU errors0,
  missing0 before body-splash additions. Current crash effects need recheck.
- Earlier 128-toy native GL RX7600 1080p High perf: avg1.6236ms both runs,
  p952.1882/2.1448, max3.1672/3.6582. `out/toy-physics-perf/results.json`.
  Predates696-toy expansion; DO NOT use it as current expanded-pit perf.
- Candy prior to crash splashes/skin decals: avg1.6234/1.6201ms,
  p951.9875/1.9959, max7.8069/7.1535ms (occasional spikes), same RX7600 locked
 1080p High. `out/candy-chocolate-perf/results.json`.
- Initial wall verified standing then smashing in romdev screenshots
  `block-wall-impact.png` and `block-wall-smash.png`. Full Toybox pedal-only
  finished1/crashes0, impact counters ball36/block60/chicken3, GPUerrors0,
  missing0. This was the smaller population; rerun expanded map.
- After toys/candy, user explicitly says return to ALL earlier ledger items,
  then creatively enrich boring levels. Do not drop the remaining work.

### Chocolate impact verification (2026-10-20)
- Fixed missing skinned mesh decal shader; latest romdev forced river crash has
  visible brown coating on bike wheels/frame and rider shorts/legs.
- Droplets now queue localized mesh decals at actual Box3D impact positions and
  normals on rider/bike, rather than ignoring hits after first broad coating.
  Bounded to 12 persistent impact stains per rider, four pending per frame;
  each clips at most 768 triangles. Existing broad river stain retained.
- Respawn/teleport re-arms the river splash and resets used droplet slots;
  ground splats remain scene children for map duration.
- Seed1 Candy15, 230 frames then `chocolate crash`, 110 frames: 18 decals,
  11 mesh decals, 16782 clipped triangles, 26 pieces; GPU errors0/missing0.
  Screenshot `out/neon-28-venues/chocolate-impact-settled.png` inspected.
- Remaining limitations: scenery splats require Box3D collision surfaces;
  decorative non-collidable scenery does not stop droplets. Impact stains cap
  at12/rider. Latest native performance and multiplayer still need checking.

### Ball pit visual/ending corrections (2026-10-20)
- Romdev exposed balls spilling through open entrance before arrival. Enclosed
  both pit ends in collider + plexiglass; added bright springboard finish+1m.
- Ordinary crash lift/side throw overshot pit and left bike outside. Finale now
  launches whole 3D rider+bike at (6,8.5,0), after score locks. Existing normal
  crash behavior unchanged. Verified rider lands inside, balls fly over rim.
- Critical rendering fix: set_matrix_at only updates CPU data. Four ball batches
  now use DYNAMIC_DRAW and increment instance_matrix.version once per frame;
  uploads and bounds now follow Box3D. Prior frozen-ball images were invalid.
- Latest full seed1 romdev Toybox: finished1, crashes0, GPUerrors0/missing0.
  `pit-live-balls.png` shows impact scattering, `pit-live-results.png` confirms
  delayed results and frozen 29.10 time / 5160 score. Both inspected.
- Native RX7600 latest live-balls first run avg1.7681ms p952.0637 max5.9057.
  Second run exited -11 after emitting stats; NOT a clean benchmark pair.
  Earlier expanded-pit pair also second exit -11, before instance upload fix.
  Investigating native shutdown with gdb; two runs under debugger normal so far.
  Logs out/toy-pit-live-perf. Multiplayer still pending.
- Follow-up: two-player full Toybox run completes both riders at29.10, zero
  wipeouts, zero GPUerrors/missing; inspected `pit-two-player.png`.
- Native exit trace via temporary /tmp/bikemesa-crash-trace.so identifies
  __libc_free -> XFree -> _XFreeEventCookies -> XCloseDisplay -> SDL_Quit.
  Six debugger runs normal; uninstrumented paired second runs reproduce.
  Do not blame third-party code without checking memory corruption.
  Configured separate build/whimsy-asan with address,undefined RelWithDebInfo;
  build only bikemesa. Logs out/toy-pit-live-perf/asan-build.log.
- Sanitizer target build still running, exec session66970; latest observed
  279/578+ steps. Re-poll that handle before launching anything duplicate.
  It is a separate build; user cart remains latest verified live-ball build.
- Sanitizer build66970 finished successfully. Standalone2800-frame Toybox run
  exit0; paired ASan+UBSan runs also exit0, zero sanitizer diagnostics and
  GPUerrors/missing0. Logs out/toy-pit-asan-check and asan-run.log. Sanitizer
  timings are NOT release performance. Native X11 shutdown crash remains
  intermittent/unresolved; next isolate SDL/X11 event cleanup (captured stack).
- Removed obsolete `lighting` debug command from benchmark (unknown-command
  response added noise). No suppression of nonzero exits or benchmark gates.
- All build/test processes from this turn finished; no live session to poll.

### Circus music and animals (2026-10-20)
- Added music_circus.ogg: full Marine Band Entrance of the Gladiators performance,
  154.848s mono32kHz48kbps, 865405bytes (original7910349bytes). Circular750ms
  tail/head overlap. Generator tools/make-circus-music.py checks source SHA256;
  source/credit/public-domain provenance added to CREDITS. Track generator and
  generated descriptor select the dedicated stream; asset root includes it.
- Romdev streamed beyond one full loop (9445frames), resource ready/no failure.
  After-loop PCM snapshot correlates .9956 with encoded source at2.547s,
  proving the correct music restarted. No claim of subjective listening.
  audioDebug record240 advanced240 but saved only last816frames; don't describe
  its WAV as a full four-second recording. Start snapshot mixed countdown SFX.
- Added adult/calf elephants at36% with blanket/tassels/tusks/toenails, animated
  continuous curled trunks lifting colored balls; two maned lions on colorful
  pedestals at79%, slowly animated heads. Rigid parts batched by material.
  Initial trunk/tail sphere gaps corrected with connecting segments.
- Inspected circus-elephants-final.png and circus-lions-final.png in romdev.
  Full first pass completed loops1/crashes0/finished1 with errors0/missing0;
  final geometry-only correction also clears giant loop (same physics).
- Music/elephants/lions above supersede older unimplemented notes in this ledger.
  Animals are stylized first pass; further visual refinement remains possible.
- Still pending: native shutdown isolation, remaining map discoveries/rover
  integration, candy surface/occlusion polish, broader example regressions.

### Candy terrain contact polish (2026-10-20)
- Root cause of river holes: bank_y is a continuous function, but visible terrain
  uses sparse, triangulated depth rows. Added bank_surface_y using those exact
  triangle vertices/barycentric weights and shared BANK_ZS row definitions.
  Liquid vertices use rendered-surface height; smooth highlight normals still
  derive from continuous bank function. Small .08m overlay separation.
- Removed grass in both river corridors and Toybox finale/pit footprint.
- Moved cake houses and large candy props clear of river/waterfall corridors;
  stepping stones no longer cross chocolate. Fixed fall streak endpoints to
  use actual fall top and pool surface instead of a hardcoded10m drop.
- Paving blocks sample all four ground corners and seat into slope; enlarged
  cupcake cylinders extend down to the lowest perimeter ground sample. Same
  geometry counts, initialization-only sampling.
- `chocolate crash 2` now exercises second river; no arg remains first river.
- Inspected chocolate-surface-fit.png, chocolate-pool-fit.png,
  chocolate-waterfall-clear.png, candy-grounded-final.png in romdev.
  Main streams substantially cleaner, no vegetation through liquid; smooth
  highlights retained. Small distant seam specks can still merit refinement.
- Full Candy pedal-only run after surface/placement edits: finished1 crashes0,
  marks62, GPUerrors0/missing0. Final base-seating geometry-only build also
  second-river crash verified errors0/missing0. Working cart updated.

### Chocolate contact splatter (2026-10-21)
- User requires airborne chocolate to splatter and stain what it hits, including
  player and bike. Existing crash-only burst now also fires smaller riding
  splashes, carrying forward wheel velocity with a radial spread. Reuses the
  fixed 24-droplet pool per rider; persistent decals stay until map reset.
- Added swept sphere proxies for upright posed limbs/frame/wheels (upright bike
  physics is 2D). Broad-phase rejects distant riders; ragdolls still use Box3D
  raycasts, choosing the nearest hit against either representation. These are
  approximate cosmetic collision shapes, not additional physical bodies.
- Ground impact stains use MIX for readable brown marks; robust projector up
  vector for vertical surfaces. Existing mesh decals handle rider/bike stains.
- Fixed crash limb position changes mistakenly triggering teleport resets and
  repeatedly restarting the splash. One crash burst now remains one burst.
- Romdev seed1, Candy track15, pedal1020: splashes1/body contacts10/ground7,
  5 mesh decals/3228 triangles, GPUerrors0/missing0. Then chocolate crash +110:
  splashes2/body11/ground26, 44 total decals/9 mesh/15154 triangles.
  chocolate-contact-final.png visually inspected. Native and working cart rebuilt.
- Decorative objects without collision still cannot receive raycast splashes;
  impact counts count contacts, while per-rider decal quotas limit accepted marks.
  No new frame-time benchmark claimed for this change.

### Desert discoveries and engine particle lifetime fix (2026-10-21)
- Dune Sea has a crashed saucer/alien at 56% and an alien with scanner at81%.
  Metallic hull, broken glowing rim lights, opened hatch, scattered panels,
  terrain-conforming scorch, waving arms and GPU smoke. Vegetation/rocks kept
  clear of saucer footprint. These are implemented first-pass discoveries.
- Fixed engine GPUParticles3D negative-time sentinel: preprocess legitimately
  starts emissions before time0. Explicit has_emitted state now distinguishes
  never-started emitters from prewarmed ones. Live counts and stopped-emitter
  draining now work. Mirrored in root engine and pinned whimsy-engine.
- Regression covers initially disabled, prewarm, stop/half-life, fully drained,
  and restart. Pinned native unit_fx failed3 checks before; passed65/failed0
  after. Logs retained in out/particle-prewarm. Root engine native unit build
  currently blocked by existing Box2D API mismatch in mover2d.c; not claimed
  tested there.
- Updated BikeMesa cart/native. Romdev Dune #25 smoke alive64 initially,
  stopped ->32 after180 frames ->0 after185 more; restarted ->64. Screenshot
  desert-smoke-prewarm-fix.png inspected, GPUerrors0/missing0 atx251.2.
- Earlier desert benchmark out/desert-discoveries-perf/results.json: first run
  exit0 average1.533ms/p951.676ms; second run exit-11 at native shutdown.
  This is NOT a clean paired benchmark; existing SDL/X11 shutdown issue remains.

### Moon lander visual pass (2026-10-21)
- Inspected actual Moon Base encounter at62%. Earlier lander was a tall plain
  box, floating front foot, no crash debris. Refined with faceted octagonal
  descent stage, angular silver/black cabin, triangular windows, side tank,
  hatch, porch, ladder rails, terrain-seated feet and connected bent gear.
- NASA references: https://www.nasa.gov/history/apollos-lunar-module-bridged-technological-leap-to-the-moon/
  and https://www.nasa.gov/wp-content/uploads/static/history/alsj/LM04_Lunar_Module_ppLV1-17.pdf
  Stylized wreck inspired by Apollo, not a precise historical scale model.
- Astronaut gains visor rim, chest controls, red suit bands, gloves, boot soles,
  backpack panel; scattered thermal panels and terrain-conforming footprint
  geometry. Replaced initially raised footprint spheres after screenshot review.
- Static geometry batched; no added simulation, lights, or render passes.
- Romdev seed1 pedal-only Moon ride completed finished1/crashes0/errors0/missing0.
  Final cosmetic fixes inspected in moon-lander-final.png at3540 frames;
  errors0/missing0. Native and working cart rebuilt. Performance not rebenchmarked.
- Still pending: additional separated Moon discovery, NASA Mars rover integration,
  other ledger map details, broader performance gates and native shutdown issue.

### Spirit and Opportunity runtime integration (2026-10-21)
- NASA shared MER source now ships as assets/models/mer.glb (2,245,980 bytes,
  19,580 triangles,9 primitives,256px textures; source11.91MB). Repeatable export
  appended to tools/prepare-rovers.sh; attribution in CREDITS. Source hashes and
  URL remain references/rovers/sources.json. Prepared older candidates unchanged.
- Two cw_gltf_instantiate instances share resource geometry/textures, at28% and
  48% of Red Planet; different yaw/depth, seated with terrain gradient. Replaced
  the old generic box rover. Normal frustum culling enabled; cast/receive shadows.
- Initial render absent/partial: surfaced missing imported material variants via
  pipeline diagnostics. Added five shader states, regenerated GL/WebGPU tables
  (78 states). Temporary culling bypass and diagnostic logs removed.
- Romdev seed1 screenshots mars-spirit-final.png and mars-opportunity-first.png
  inspected. Textured solar panels/mast/suspension visible, errors0/missing0 at
  first encounter. Native and working cart rebuilt.
- Remaining: richer differentiated context/identification for Spirit/Opportunity,
  Curiosity and Perseverance asset integration, accessible Sojourner model,
  runtime LOD/performance audit. Do not claim all five rovers complete.
- Final full Red Planet ride: finished1/crashes0/errors0/missing0 at4194 frames.
  Opportunity screenshot first was taken too late; replay at2000 frames produced
  mars-opportunity-final.png, visually verified the complete second rover.

### Curiosity and current-machine rover timing (2026-10-21)
- Added optimized NASA Curiosity model: assets/models/curiosity.glb,2,103,116bytes,
  25,321triangles/9primitives/256px textures, versus11.86MB source. Export command
  in prepare-rovers.sh; CREDITS and roots updated. Rover table now holds separate
  paths/placements/scales/yaws; resource instances own loaded asset references.
- Curiosity at62%,z-5.5,scale1.3. Initial67% placement coincided with high jump
  that pulled camera away; moved earlier. Added transparent-map shader variant,
  regenerated79-state GL/WebGPU tables. Culling stays enabled.
- Romdev mars-curiosity-final.png inspected at2580frames, x287.8. errors0/missing0.
  Runtime detailed model visible. Terrain seating can still be refined (rigid
  imported suspension on curved terrain); no animation/physics added.
- Native current machine reports Radeon890M/Mesa26.0.8, NOT priorRX7600. Serialized
  before/after pairs,3100frames,last2048samples,1080pHigh/seed1/autoride, GL:
  before avg3.8985/3.9305ms,p957.2188/7.2859; after avg3.9259/3.9251,p957.2523/7.2591.
  All four exit0,render gates passed. Thread CPU ~.47ms. Max55.7–58.7ms in both.
  No meaningful average regression shown; NOT a steady4ms result. Tail/spikes
  remain. Background load/CPU/GPU samples saved with renderer/binary hashes.
- Evidence out/mars-before-curiosity/valid/results.json and out/mars-with-curiosity/results.json.
  Initial /tmp baseline lacked adjacent pack and failed asset validation; excluded.
  Valid baseline uses previous executable with same adjacent current pack (extra
  Curiosity asset unused by baseline), preserving same other assets.
- Native/cart rebuilt. Perseverance and Sojourner remain unfinished, along with
  LOD/further performance work and other map requirements in this ledger.

### Perseverance parked model (2026-10-21)
- Added NASA Perseverance at84% of Red Planet,scale1.5. Runtime GLB2,474,212bytes,
  31,177triangles/28primitives/15meshes. Source252primitives/199,521triangles.
- prepare-rovers.sh exports candidate; finish-perseverance.mjs bakes skin rest
  pose, removes animation channels/samplers, converts WebP to PNG, joins
  compatible geometry. Removing animation objects alone left channels attached
  and prevented joining; corrected. ROVER_NODE_PACKAGE selects tool dependencies.
  Repeated output SHA256 matches e0205fa5a2df9ee8df9a17b01fb979025bd8f2602fa580363d2a9b2c6a1954d4.
- Added six shader variants for imported double-sided/normal/roughness materials;
  regenerated GL/WebGPU programs. Asset root/CREDITS updated.
- Fixed rover orientation order: yaw first, then terrain pitch/roll. Six wheel-area
  terrain samples adjust height. All imported rovers use corrected seating;
  rigid wheels can still depart slightly from curved ground (no suspension sim).
- Romdev mars-perseverance-seated.png inspected at3040frames. Model visible,
  errors0/missing0. Full ride4143 finished1/crashes0/errors0/missing0. Temporary
  checkpoint banner overlaps top during approach; encounter placement can improve.
- Native/cart rebuilt. No performance benchmark for this change yet. Sojourner
  remains unimplemented; broad map/perf ledger is still open.

### Chocolate impact spray (2026-10-21)
- User: chocolate bubbles must splatter and dirty what they hit, including rider/bike.
- Added 64 pooled secondary flecks, four per primary impact. Secondary droplets
  use the same swept Box3D/upright-rider collision and persistent decal path;
  no recursive bursts. Lifetime capped at 0.7 seconds.
- Added bank heightfield sweep for scenery ground without Box3D bodies, with
  terrain-normal oriented stains. Primary and secondary impacts both deposit.
- Shared spray updates even when only a nonzero multiplayer rider splashes.
- Cart/native builds passed. Final romdev seed1 Candy Comet 1100-frame ride:
  splashes1, body contact attempts15, ground deposits105, GPU errors0/missing0.
  Body deposits retain existing 12-per-rider cap (contact counter is not decal count).
- Inspected out/chocolate-impact-spray.png: visible airborne flecks and grouped
  brown deposits across track/bank. No new performance benchmark this pass.
- Remaining: decorative meshes without physics still do not catch drops on their
  silhouettes; waterfall's looping decorative streaks are separate from these
  collision-aware rider splash bubbles. Broader map work remains open.

### Chocolate decorative-mesh contacts (2026-10-21)
- Added query-only copies of stationary candy props near both rivers before
  render merging. Share source geometry/materials; not added to render scene.
  Captures mesh silhouettes including transparent gumdrops, excluding animated
  accents. Rebuild frees previous copies and raycaster.
- Uses three.c raycaster after finite-segment bounding-sphere rejection; nearest
  mesh/physics/upright-rider hit wins. Triangle world normal orients stain/spray.
- Romdev full Candy Comet: finished1/crashes0, two river splashes, 34 scenery
  contacts, 27 body contact attempts, 211 ground/scenery deposits, GPU errors0,
  missing0. Switching Candy -> Powder -> Candy reproduces first-river scenery17,
  no render errors. Screenshot out/chocolate-scenery-contact.png inspected.
- Native 1080p High locked, seed1, 1500frames/700samples, two valid exit0 runs
  each, sequential (no simultaneous romdev stepping/build):
  before average4.0383/4.0486ms, p954.923/4.8524; after4.0299/4.0341,
  p954.8536/4.7717. CPU frame before.534/.5072ms after.6399/.6832ms.
  Thus wall-time unchanged within variability, CPU +~.14ms; not a performance
  improvement claim. Evidence out/chocolate-before-scenery/results.json and
  out/chocolate-scenery-perf/results.json. Both retain final draws108/tris484415.
- Remaining: animated decorative waterfall streaks do not run collision, and
  body stains still have existing 12-per-rider budget. Broader map work open.

### Temple standing-stone refinement (2026-10-21)
- Replaced box stones with six shared 60-triangle beveled/fractured variants,
  uneven crowns, tapered ends and flat face normals. Existing moss/vines retained.
- Each upright seats independently on bank_surface_y; lintel support height
  determines pillar height. Fallen lintel and approach stones also terrain seated.
- First romdev view exposed poor original placement: lintels cropped by camera,
  checkpoint over approach. Moved circle from64%/z-14 to58%/z-8.5, compressed depth,
  reduced pillar height, lightened stone. Full silhouette now readable mid-course.
- Inspected out/temple-weathered-circle.png at1925frames. Entire ruin visible.
  Six shared geometries, existing static batching; no new lights or simulation.
- Final full ride finished1/crashes0/GPU errors0/missing0. Cart/native rebuilt.
  No new performance timing claim. Other discoveries/maps remain unfinished.

### Powder Line creature refinement (2026-10-21)
- Walkers now start retreating when any rider comes within32m. Added read-only
  bike_rider_near helper without changing the selected rider. Removed fmod reset:
  creatures continue away instead of teleporting back. Terrain-sampled body height.
- Rounded fur clumps replace cone spikes, narrower shoulders, rounded hands,
  face moved toward camera-facing shoulder. Scale.8; first52%/z-6.2, second85%/
  z-6.2 (was79%/z-13). New second position avoids checkpoint banner.
- Replaced shiny raised footprint spheres with subdivided matte impressions.
- Romdev inspected out/powder-walker-refined.png and powder-brown-refined.png;
  second screenshot is final2190-frame build. Both creatures readable, later
  one unobscured. Full final ride finished1/crashes0/errors0/missing0.
- Cart/native rebuilt. No performance measurement this turn. Still stylized;
  some footprint edge depth artifacts remain on steep terrain, face detail and
  walking foot contact deserve further polish. Do not claim final creature fidelity.

### Projected snow impressions (2026-10-21)
- Replaced footprint overlay triangles with engine screen-space Decal3D instances:
  12 prints per creature, two soft lobes each (heel/toes),48 total. MULTIPLY blend
  uses rendered depth, so bank triangle creases cannot intersect the print mesh.
- Compared exact2190-frame view: out/powder-decal-prints.png inspected; earlier
  stripe/depth artifacts absent. No raised footprint geometry remains.
- Romdev final full ride finished1/crashes0/GPU errors0/missing0. Native/cart built.
- Sequential native 1080p High locked seed1,2250frames/1200samples,2 runs each:
  before avg3.7254/3.7022ms,p956.9917/6.9716; after3.7324/3.7255,p957.0191/7.0306.
  CPU frame before.6429/.6328,after.5754/.6094ms. Near-identical wall-time; no
  meaningful speedup claim. Spikes remain (~56-61ms max), steady4ms unmet.
  Evidence out/powder-before-decals/results.json and
  out/powder-projected-decals/results.json. Builds and romdev stepping excluded
  from benchmark windows. Broad map requirements and walking contact still open.

### Powder frame-spike investigation (2026-10-21)
- Previous turn was progress (projected footprints + validated comparisons).
- Captured native1080p High seed1 frame1050..2049 engine traces with and without
  GPU timers. Artifacts out/powder-profile/{trace.json,cpu-trace.json,run.log,
  cpu-run.log}. Both native runs exited0; CPU-only errors0/missing0.
- GPU-timed trace main-thread maxima: frame1583 47.56ms (r2d.upload46.92),
  frame1186 42.95ms (particles41.91). GPU queries themselves stall up to16.7ms,
  so profiling GPU timers adds observer cost; do not use that run as baseline.
- CPU-only trace reproduces SAME frame1583 51.92ms (app48.25),frame1186 46.42ms
  (particles45.06). Exact repeat suggests workload-dependent synchronization
  or driver work rather than solely random desktop load. Not yet proven cause.
  Frame average3.7285ms/p957.0158,max58.3823; thread CPU average.5794ms.
- Tried replacing GL decal glBufferSubData with full glBufferData streaming,
  root+pinned; two valid runs avg3.7379/3.7395ms, p956.9358/7.006,max58.8751/
  59.0484. No measurable benefit. REVERTED only that experiment; preserved
  earlier authored-transform decal fix. Native/cart restored and rebuilt.
- Next useful investigation: reproducible frames1186/1583, particle/HUD uploads
  and preceding state changes. No spike-resolution or performance-win claim.

### Upload-stall attribution (2026-10-21)
- Temporary per-GL-call profiling isolated frame1186 to cw__fx_mat4 (~40.56ms)
  and frame1583 to HUD glBufferData (~46.30ms). Particle simulation/draw calls
  themselves were microseconds. Trace out/powder-profile/upload-trace.json;
  focused frame1186 trace particle-detail.json. All native runs exit0.
- Critical control: mesa_glthread=false moves BOTH same-frame stalls into
  pass scene:1186 42.02ms/frame,41.30ms scene;1583 47.49ms/frame,46.77ms scene.
  This strongly suggests downstream uniform/HUD calls were synchronization
  observation points, not original work. Next: scene draw/state/shader variant
  changes at these frames. Do not optimize particle counts on this evidence.
- No-glthread control avg3.6956,p957.0129,max61.3443ms; threadCPU.7514ms, no
  speedup claim. High1080p/seed1/2250frames/1200samples/errors0/missing0.
  out/powder-profile/no-glthread.{json,log}. Driver setting was per-process only.
- Restored exact pre-investigation particle and HUD sources from temporary
  snapshots; removed all temporary markers. Native rebuilt; working cart was
  never built with those markers. Existing unrelated engine fixes preserved.
- Goal remains active; this turn yields causal evidence, not a resolved spike.

### Instanced geometry upload stalls fixed (2026-10-21)
- Traced repeated stalls to use_geometry on a42-vertex instanced mesh. Finer
  probes isolate instance_buffer glBufferSubData when extending/replacing the
  visible instance prefix; ~43-48ms synchronous waits. This is not HUD cost.
- Three.c now replaces GL storage with glBufferData for changed/growing visible
  prefixes, uploads only n instances, tracks actual bytes/items_valid. Unchanged
  or shrinking prefixes skip upload. Initial allocation remains existing path.
  WebGPU path unchanged. Applies to any engine game using this GL instance path.
- Source: cartwheel-engine/deps/three.c/src/renderer.c (also the pinned engine
  dependency symlink), mirrored into /home/monteslu/code/cliemu/three.c.
- Added test/test_instance_stream.c + run.sh hook in both three.c trees. Actual
  private upload path with mocked GL verifies growth, shrink/no-upload, changed
  version/data, prefix byte sizes, and no subdata writes. Both tests exit0.
- Sequential native High1080p seed1,2250frames/1200samples, two valid runs:
  final avg3.6766/3.6989ms,p956.8916/6.8791,max11.2644/12.9819.
  Before out/powder-projected-decals averages3.7324/3.7255,p957.0191/7.0306,
  max60.7759/55.8117. Main gain is removing severe hitching, NOT steady4ms.
  Final evidence out/powder-instance-prefix/results.json. No concurrent builds
  or romdev stepping. Same Radeon890M/Mesa26.0.8, no persistent driver overrides.
- Earlier full-attribute orphaning experiment had no benefit and was reverted.
  Full-capacity instance replacement also worked, then refined to visible-prefix
  uploads to avoid bandwidth scaling with reserved capacity.
- All temporary renderer probes restored before final builds. Cart/native built.
  Romdev GLES full Powder ride finished1/crashes0/errors0/missing0; screenshot
  out/powder-instance-stream-gl.png inspected, scenery/prints/rider intact.
- Remaining p95~6.9ms and11-13ms peaks still exceed target. Other examples not
  yet rebuilt/retested for this specific dependency change. Broad goal open.

### Chocolate bubble collision recheck (2026-10-21)
- Rechecked user's request against existing collision-aware primary bubbles and
  secondary spray; no new implementation change was needed for this request.
- Romdev GLES seed1 forced first-river crash: after40frames, splash1, body contact1,
  ground/scenery deposits9, mesh decals4; inspected out/chocolate-crash-check.png.
- Fresh track reset,200frames then second-river crash,50frames: splash1, body
  contact1, ground/scenery deposits20, scenery contacts16, mesh decals4.
  Inspected out/chocolate-second-crash-check.png. Both checks errors0/missing0.
- Reusing the crash debug command while already crashed does not trigger a fresh
  splash; reset the track between independent forced-crash tests.
- Existing limits remain:12 impact decals per rider plus broad crash coating;
  waterfall decorative streaks are not collision-aware splash bubbles.

### Shared renderer check across examples (2026-10-21)
- Previous goal turn verified chocolate crashes at both crossings; this turn
  resumes cross-example checks after the GL instance upload fix.
- All five other example cart/native targets rebuilt successfully. Focused
  romdev GLES seed1 checks: Bomberdude arena3/300frames (prior pass), Pugball
  stadium3/500frames, Chromite level1/500frames after start, Spout first level/
  500frames after selection. Screenshots inspected in out/*-instance-stream.png.
  Each reports cw.gpu.errors0 and cw.three.missing0.
- Derby arena2/500frames exposed missing state
  standard+map+trans+shadow+senv+l1001+xcluster_meshdecal. Added manifest state,
  regenerated GL/WebGPU tables with double capture validation (57 states), and
  rebuilt derby cart/native. Same arena now errors0/missing0 on GLES AND WebGPU.
  Inspected out/derby-instance-stream-fixed.png. Missing state was a shader-table
  coverage issue; no evidence it was caused by the instance upload change.
- These are focused rendering checks, not all arena/quality/multiplayer gates or
  cross-game benchmarks. No speedup claim for these games. Broad goal remains open.

### Moon Base second discovery (2026-10-21)
- Added listening station at84% finish,z-9, well beyond Apollo wreck62%.
  Five-meter parabolic dish scans slowly; tripod feet terrain sampled, two solar
  wings at different heights, waving alien with handheld display beside it.
- Dish shell768triangles, shared existing materials, rigid parts merged per
  material. Only dish and alien arm animate; no new lights, physics bodies,
  textures, or shader variants. No timing claim without benchmark.
- Initial romdev view found generated boulder clipping right solar wing and
  shard overlapping left. Reserved station footprint in rock generation.
- Final screenshot out/moon-listening-post.png inspected at4274frames: dish,
  feet, solar wings, alien readable; no intersecting rocks. Full pedal-only
  ride5574frames finished1/crashes0/GPU errors0/missing0, GLES seed1.
- Native/cart builds passed; known unrelated mesa.inc fabsf(int) warning remains.
  Broad map/performance work remains open.

### Santa placement and level-select visual audit (2026-10-21)
- Prior turn added and verified Moon listening station (progress).
- Romdev Christmas midpoint exposed generated rocks inside reindeer and feet
  floating above snow. Updated old start-area rock clearance to actual midpoint
  sleigh footprint; excluded nearby tufts. Reindeer now sample terrain at their
  final .75-scaled world positions. No added geometry or runtime animation cost.
- Inspected out/christmas-santa-seated.png at1032frames: all four deer, sleigh,
  waving Santa visible, no embedded rock. Checkpoint banner above encounter;
  individual hoof contact/rigid reins can still be refined on sloped terrain.
- Full Christmas ride2832frames finished1/crashes0/errors0/missing0. Both native
  and cart rebuilt. No performance measurement claimed.
- Verified existing larger bright selection border in romdev using actual menu
  input: title -> rider -> bike -> selection, then right to locked Powder Line.
  Inspected out/select-highlight-check.png and select-highlight-moved.png.
  Gold/ivory core stays obvious on unlocked and locked tiles and follows focus.
  No further selection styling change needed for requested stronger highlight.

### Sojourner source audit (2026-10-21)
- Previous turn fixed Santa terrain seating/clearance and verified selection
  focus (progress). Resumed missing named rover requirement this turn.
- Followed actual download links: Cad Crowd niravj STEP model redirects to
  login; BlendSwap argonius15250 CC0 model (reported9912polygons) download page
  explicitly requires sign-in. Neither file acquired. Pinshape chemteacher628
  model CC BY-ND and login-gated, unsuitable for planned optimized adaptation.
- nraynaud/sojourner GitHub recursive tree has reference material, not an assembled
  rover mesh; fspy.blend is134-byte LFS pointer. Hackaday wglasford project links
  to Printables for parts, index labels BY-ND (original still needs checking).
- New leads: Thingiverse59502 (moriza),4146011 (bonecar solar-panel remix), both
  web fetch403; historical NASA MPF/vrml/vrml.html currently redirects to generic
  page. Archive CDX attempt timed out; do not mistake HTML for model download.
- Saved all candidates/access/license observations in references/rovers/
  sojourner-candidates.json to avoid repeating dead ends. Sojourner NOT added.
- Corrected stale sources.json claiming all NASA rovers unintegrated; inspected
  actual runtime GLBs and recorded SHA256,bytes,triangles,primitive counts and
  instance counts. MER current bytes2217968 (older ledger byte count differs).
- No game/render changes this turn; no new playtest/performance claims. Overall
  goal can continue on other map/performance work while source remains unresolved.

### Current Neon GPU budget (2026-10-21)
- Previous source audit narrowed accessible Sojourner candidates and corrected
  runtime asset inventory (research progress); no downloaded Sojourner yet.
- Four sequential current native GLES1080p High locked seed1 runs,3500frames,
  last1700 samples, no concurrent build/romdev stepping. Radeon890M Mesa26.0.8.
  avg4.1662/4.1751/4.2022/4.2180ms; p955.3812/5.5116/6.0810/5.5904;
  max15.1777/16.1315/18.0089/16.0823; CPUthread1.0522..1.1394ms.
  All exit0/errors0/missing0, same x673.464/checkpoint8; not full-course finish
  coverage. System load/GPU busy recorded. Artifacts out/neon-instance-prefix-current.
- Older Neon28-facades/full-course artifacts used RX7600; NOT comparable as
  before/after to current890M. This is a fresh baseline, not a speedup claim.
- Separate GPU-timed400frame trace1900..2299: scene avg2.039ms, SSAO .822,
  bloom .366, AA .339, tonemap .317. Query collection itself costs2.739ms on
  sampled frames; trace wall times are not baseline timing. Main-thread CPU
  ~1.1ms vs frame~4.2ms supports targeting GPU work next.
- Inspected GTAO: already half-resolution and fog composite fused; do not
  propose those as new fixes. Next investigate scene/shadows and GTAO shader
  work while preserving contact shading. No rendering code changed this pass.
- Steady4ms target remains unmet. Full data/settings/SHA256 and summarized
  pass metrics saved in results.json,gpu-trace.json,summary.json under artifact dir.

### GTAO Gaussian simplification experiment (2026-10-21)
- Previous turn established reproducible current Neon GPU budget (progress).
- Tested combining spatial/depth Gaussian weights in AO upsample from two exp
  calls and multiplication into one exp of their summed exponents. Root+pinned
  GLSL/WGSL edited consistently, sample counts/filter dimensions unchanged.
- Matched romdev GLES seed1/2000frames screenshots neon-ao-before.png and
  neon-ao-after.png are pixel-identical (PIL mean/max channel error all0).
  Candidate GPU errors0/missing0. No WebGPU runtime claim for candidate.
- Preserved baseline native executable+pak in /tmp/bikemesa-ao-baseline.
  Sequential baseline2/candidate2 native High1080p3500frames/1700samples:
  baseline avg4.1814/4.2035,p955.3221/5.4547; candidate4.2297/4.2114,
  p955.5477/5.4460. No benefit; artifacts out/neon-ao-baseline and
  out/neon-ao-combined. No simultaneous build/romdev stepping during timing.
- REVERTED experiment exactly to pre-edit snapshots, root+pinned; rebuilt
  native/cart with restored shader. Restoring old mtimes initially caused Ninja
  no-op; touched restored shader sources and confirmed actual compilation/link.
- No shader optimization retained. Next target scene GPU cost (~2.04ms), not
  additional speculative AO arithmetic changes. Steady4ms and broad goal open.

### Neon shadow diagnostic and depth-prepass rejection (2026-10-21)
- Previous chocolate recheck repeated existing coverage; resumed unfinished
  Neon optimization rather than repeating it again.
- Shadow=false experiment invalid: missing shader variants and only two draws.
  Scene restored. Temporary omission of shadow caster drawing (keeping shadow
  setup/sampling) measured avg4.0898/4.1170ms, versus baseline4.18–4.20ms.
  Caster rendering is not the dominant cost; shader sampling was not isolated.
  Temporary renderer change restored before candidate build.
- Depth prepass native GLES High1080p3500frames/last1700 measured3.9041/3.9003ms,
  p955.7367/5.7984; CPU1.3089/1.2972ms and179draws/340456triangles.
  Artifacts out/neon-depth-prepass. Timing gain is NOT a retained optimization.
- Visual comparison at seed1/2000frames exposed black rectangle around Proof
  Bread transparent sign. Difference image isolates sign background; otherwise
  low average pixel error concealed the regression. Inspected both full images.
- Current prepass_exec draws entire scene, including transparent depth writers.
  Retaining that depth prevents facade behind transparent pixels from rendering.
  Removed depth_prepass from track03; generator never acquired candidate setting.
  A correct opaque-only pass is needed before reconsidering this feature.
  Keep alpha-tested opaque cutouts supported; do not blanket-disable sign alpha.
- Overall4ms steady budget and remaining map/art requirements remain open.
- Rebuilt cart/native after removal. Romdev restored cart seed1/2000frames
  pixel-identical to baseline (RGB difference bounds None), errors0/missing0.

### Opaque prepass engine fix retained (2026-10-21)
- Previous turn isolated and reverted transparent-sign regression (progress).
- Added t3_renderer_render_opaque in engine deps three.c and separate three.c
  checkout. Scoped queue filter excludes transparent/transmission materials;
  ordinary subsequent renders restore all queues, materials are not mutated,
  opaque alpha-tested cutouts keep their normal shader discard. Existing
  render_depth API is shadow-caster-only/opposite-facing and unsuitable here.
- Root+pinned pipeline_post prepass uses opaque API. Updated rendering guide.
  Enabled Neon scene and generator; other scenes retain their settings.
  This is still ordinary opaque shaders, not a new specialized depth shader.
- Cart/native builds passed. Romdev GLES seed1/2000frame Proof Bread capture
  out/neon-opaque-prepass.png exactly matches baseline RGB (all errors zero).
  This specifically regresses transparent depth-writing sign + restored normal
  transparent draw. Full5000frame course reached finished1/checkpoint10/crashes0,
  GPUerrors0/missing0. No WebGPU speedup claim; built-in pass remains GLES only.
- Sequential native Radeon890M High1080p3500frames/last1700: corrected candidate
  avg3.9296/3.9217ms, p955.5766/5.8156, max14.7612/14.8445; CPU1.2642/1.2556.
  Saved baseline rerun afterward avg4.2377/4.1990ms,p955.4577/5.6941,
  max17.0067/16.5573; CPU1.1552/1.1221. About6.9% average improvement;
  p95 not improved. Candidate178draws340454tri vs baseline109draws202782tri.
  No simultaneous builds/emulator steps during timing; system load sampled.
  Artifacts out/neon-opaque-prepass and out/neon-opaque-prepass-baseline.
- Retain this measured average improvement with intact sign rendering. Steady
  4ms target remains unmet; no claim of full all-map visual/performance coverage.

### Temple Vines second discovery (2026-10-21)
- Previous turn retained verified opaque prepass improvement (progress).
- Added moss-covered seated stone guardian at82%finish,z-8, beyond Stonehenge58%.
  Weathered shared stone geometry, carved face/crown/toes, warm emissive eyes,
  separately grounded feet, seven varied ruin markers receding into forest.
  Nearby random trees skip its viewing corridor. Only merged head animates;
  proximity28m wakes a three-second turn, then slowly watches the trail.
  Static parts use existing merge batches; no extra lights/physics bodies/assets.
- First screenshot revealed upper-body cropping. Shortened torso/arms/head;
  final passing view shows entire crown/face (out/temple-guardian-final.png).
  HUD briefly overlaps it as camera passes; no claim of visibility every frame.
- Native/cart built. Romdev GLES seed1 pedal3565frames finished1/crashes0,
  GPUerrors0/missing0. Visual inspected near2450 and2565frames. No new native
  performance comparison this pass. Broader map art/performance work remains.

### Guardian stone finish (2026-10-21)
- Previous turn added/verified second Temple discovery (progress); screenshots
  exposed stretched horizontal texture bands on its wide scaled stone blocks.
- Added carved_stone helper: bake dimensions into vertices/inverse-scale normals
  and face-planar UVs at1.5m repeats. Existing merging still combines geometry;
  triangle count and texture reads unchanged. Temporary geometries released.
  Limited to guardian; Stonehenge's existing concrete geometry unchanged.
- Lightened stone tint and reduced normal strength1.4→.65 for readable carving.
- Romdev GLES seed1/2565frames screenshot out/temple-guardian-stone-uv.png
  inspected against prior matching frame: mottled stone, no horizontal smearing,
  face/toes visible. GPUerrors0/missing0/crashes0. No new frame-time claim.

### Broadleaf silhouettes (2026-10-21)
- Previous guardian texture correction was verified art progress.
- Jungle/autumn broadleaf trees now have five offset irregular leaf clusters,
  three visible branch forks, wider/narrower crown forms and varied proportions.
  Shared detail1 ico replaces detail2; existing bark/leaf material batching stays.
  Tree-local nine RNG draws preserved so forest positions do not reshuffle.
- Leaf repeat2 and normal strength.65 replace repeat1/strength1.2. No animation,
  additional light, shader variant or texture. Geometry calculation: old3*180
  crown+40trunk=580tri; new5*80 crown+4*40branches/trunk=560tri per tree.
  More source meshes merge into existing batches. No frame-time speedup claim.
- Native/cart builds passed. Romdev jungle1800frame inspected crown/branch edges;
  autumn1100/1500/2400frame views inspected. Downhill camera crops many near
  crowns; distant late-course crowns show wider asymmetry. Artifacts
  out/temple-canopies.png,autumn-canopies.png,autumn-canopies-late.png.
  Full autumn4200frame ride checked finished/crash/render fields separately.
  Further foliage artistry and distinctive autumn discoveries remain open.

### Leaf Fall discoveries (2026-10-21)
- Previous broadleaf silhouette work was verified progress.
- Added sleeping curled fox at54%finish,z-5.8, beside a fallen log. Pointed ears,
  closed eye, dark nose, cream muzzle/tail tip. First test buried tail in bank;
  added a low terrain-seated stone shelf and grass clearance. Final fox screenshot
  out/autumn-fox-final.png at1505frames inspected: full silhouette rests visibly.
- Added working plaster/wood windmill at81%finish,z-8, with crates farther behind.
  Four framed canvas sails rotate as one assembly; flattened all blade meshes
  into two material batches. Static props use existing scenery merge. No lights,
  physics bodies or new shader variants. Random trees clear both viewing areas.
- Romdev GLES seed1 final frame2145 windmill screenshot inspected whole rotor;
  different sail orientation than2100 confirms motion. Full4045frame run finished1,
  crashes0,GPUerrors0/missing0. Native/cart builds passed. No new perf timing claim.
- Full objective remains open: remaining maps need discovery/art reviews, broader
  example performance checks, missing Sojourner and other explicit ledger items.

### Renderer-owned prepass colour mask (2026-10-21)
- Previous Leaf Fall additions/build/playtest completed concrete map progress.
- Found external glColorMask(false) in prepass was undone by set_material_state
  for each colour-writing material. Added scoped t3_renderer_render_prepass in
  both three.c trees; root+pinned engine now call it. Opaque queue excludes
  transparent/transmission, shader preserves alpha discard/displacement, material
  colour writes suppressed by renderer; sky/output conversion skipped. GLES only.
  Existing shaders retained (not a specialized cheap depth-only shader).
- Added test_prepass_mask.c invoking actual private material-state function with
  mocked GL mask: normal→prepass→material switches→normal→colourless material.
  Both three.c checkout tests compiled/passed; runners include regression.
- Native/cart builds passed. Romdev seed1/2000frame screenshot pixel-identical
  to opaque-prepass baseline (RGB difference none), full5000frames finished1,
  crashes0,GPUerrors0/missing0. out/neon-masked-prepass.png.
- Sequential no-build/no-emulator-step native890M1080pHigh benchmark3500frames,
  last1700: candidate avg3.9032/3.9186ms,p955.8677/5.6224,max14.701/14.532;
  saved current-baseline rerun avg3.9263/3.9220,p955.8075/5.9485,max14.6302/14.6749.
  Timing difference small/mixed: NO material speedup claim. Candidate177draws/
  338470tris vs baseline178/340454; removes redundant sky draw1984triangles.
  Keep correctness/work reduction, still no steady4ms compliance.
  Artifacts out/neon-masked-prepass and out/neon-masked-prepass-baseline.

### Giant-loop cross-host gate and launcher fix (2026-10-21)
- Previous renderer mask fix/tests/measurement yielded concrete progress.
- Built current bikemesa_unlocked native/cart. Isolated gate project under
  /tmp/bikemesa-loop-gate-qj348q6_ maps build/native→whimsy-native and both cart
  paths→whimsy-cart, tests/out→real repo. Avoids stale standard build outputs.
  Ran deps engine tests/gate.mjs with CW_PROJECT_ROOT pointing there.
- Initial native+romdev GL gate: physics state exact, but image15.476%>16 failed.
  Launcher only set wgpu:false for --dual-gl; ordinary GL could auto-select
  WebGPU for dual-capable carts. Fixed run-cart in root engine, pinned build
  engine and examples/deps engine: explicitly select wgpu=(gpu===webgpu),
  and reject any requested-GL run reporting another backend.
- After fix GL gate PASSED native,romdev,chromium with original5% image bound
  and exact named state. Each loops1/crashes0/finished1,state.hash222275466;
  control loops0/crashes1/finished0,hash2059145808. No missing shaders/GPUerrors.
- WebGPU gate PASSED all three hosts plus against GL with unchanged thresholds;
  native/romdev adapters890M and Chromium DRM pinned0000:69:00.0. Includes
  dual-cart GL comparison and negative control. Artifacts out/gate/bikemesa-loop-giant.*.
  Gate checks final loop completion/finish and state parity; does not prove
  every rider/bike/entry speed can clear it, or full game aesthetic coverage.
- This is correctness/regression evidence, not a frame-time benchmark (hosts run
  concurrently under suite GPU guard). Broader performance/art goal remains.

### Circus save-migration coverage (2026-10-21)
- Previous turn completed GL/WebGPU multi-host giant-loop gate and fixed the
  launcher backend selection (progress).
- Extended tests/bikemesa-save-migration.mjs to seed nonzero circus time1700,
  medal3, trick score16000, completion/visibility/stars and an adjacent track.
  Exercises actual save_check in current native game, not a reimplementation.
- Versions4–7 reset circus time/medal only, preserving trick score/unlocks/stars;
  version8 retains current records; v3 correctly clears nonexistent later slots.
  Neighboring track remains byte-identical; existing Neon/track16/quality/checksum
  assertions retained. All six versions3–8 PASS. Temporary fixtures and live
  debug dumps: /tmp/bikemesa-save-DLKGLd. Real user save files not touched.
- No gameplay/render change; cart remains current. This native test proves save
  loader behavior for these fixtures, not every possible corrupted save or host.

### Chocolate bubble request confirmation (2026-10-21)
- Rechecked the existing collision-aware splash bubbles in the working cart using
  romdev, seed 1, Candy Comet, forced crashes at both rivers, 110 frames each.
- First crossing: 122 decals including 4 mesh decals; second: 121 including 4
  mesh decals. Both report GPU errors 0 and missing shaders 0. Inspected
  out/chocolate-latest-crash.png and out/chocolate-latest-second-crash.png:
  rider/bike coating and ground splats visible. No runtime changes needed for
  this repeated request. Decorative waterfall streaks remain a separate effect.

### Sojourner archived mesh recovered (2026-10-21)
- Previous turn repeated already-covered chocolate checks; resumed missing Mars
  rover rather than repeating those checks again.
- Found SGI's archived sojourner.wrl in https://win98.altervista.org/pathfinder/SGI.zip.
  Read ZIP central directory and selected entry via HTTP ranges after a partial
  full download timed out. Entry is gzip inside ZIP; decoded both layers.
- Preserved source, archived directory listing, geometry review GLB and local
  inspection script under references/rovers/sojourner-research (not shipped).
  Source SHA and dependencies recorded in sojourner-candidates.json.
- three.js VRMLLoader successfully parsed 59 mesh instances / 1582 triangles.
  This is an actual separate rover with wheel assemblies, solar panel and APXS,
  unlike the small simulation.wrl file, which describes the lander scene.
- Remaining before integration: establish reuse provenance, recover or faithfully
  replace eight texture references, visually review orientation/scale, consolidate
  materials, integrate/terrain-seat, then romdev verify. No runtime change yet.

### Sojourner conversion fidelity (2026-10-21)
- Previous turn recovered actual geometry: progress. Continued inspection showed
  a detached assembly in the first render. three.js 0.180 VRMLLoader explicitly
  ignores Transform center and scaleOrientation, both used in this source.
- Updated research converter to patch a private in-memory loader with VRML's
  T*C*R*SR*S*inverse(SR)*inverse(C) transform, leaving dependencies untouched.
  Bake world matrices into static mesh geometry before glTF export to preserve
  shear; retain authored vertex colors. Same 1582 triangles / 59 mesh instances.
- Reconverted and rendered with Blender 4.2.9; inspected geometry-review.png.
  Floating pieces now join the assembly. Bounds changed from z minimum -1.0032
  to -.4965; corrected bounds recorded in candidate ledger/inspection JSON.
- Inspected official archive ZIP central directory and README: this is terrain
  reconstruction/lander data, not the missing rover texture source. Missing
  textures and reuse provenance remain unresolved; no runtime asset shipped.

### Red Rock discoveries (2026-10-21)
- Prior Sojourner turn improved conversion fidelity; its unresolved source work
  remains tracked. Continued broader map improvements with two new canyon sights.
- Crystal mine cart at 49% of finish distance: open plank sides, four wheels/hubs,
  rails/ties and varied blue/gold mineral cargo on a terrain-seated sandstone shelf.
  Lowered/advanced bank placement after first screenshot hid cart under timer.
- Exposed giant reptile fossil at 78%: terrain-following vertebrae, paired curved
  ribs, long skull, dark sockets, separate jaws and pointed teeth. Rocks avoid
  both footprints. Static geometry joins existing material/spatial batches;
  no physics bodies, lights, animation or new render passes added.
- Cart and native builds passed (existing unrelated mesa.inc fabsf warning).
  Romdev GLES seed1 track1: inspected out/canyon-cart-final.png at frame1200,
  out/canyon-fossil-first.png around1854. Final build 3000-frame pedal run:
  finished1/crashes0/GPUerrors0/missingShaders0. No frame-time speedup claimed;
  new landmarks still need inclusion in the broader performance audit.

### Red Rock landmark cost and rejected prepass (2026-10-21)
- Previous turn added/verified discoveries: progress. Measured the actual addition
  against a native control omitting only canyon_discoveries; rock clearances and
  all other code/assets/settings retained. Temporary source edit restored and
  current native rebuilt before timing. Saved binaries/paks under
  /tmp/bikemesa-canyon-comparison, hashes/settings in result JSON.
- Sequential 1080p High/locked GLES AMD890M, seed1, track1,2400 frames/1700 samples:
  additions A avg4.1682/4.2052ms,p957.7864/7.9095;
  control avg4.1657/4.1869,p957.6303/7.7474;
  additions B avg4.1930/4.2287,p957.8611/7.8609.
  Combined mean addition ~.023ms (~.5%), within observed variation; modest p95
  difference ~.17ms. CPU/load sampled, other desktop applications left running;
  no simultaneous build or romdev stepping. Errors0/missing0, all exit0.
  Load1 means1.29–1.80. Evidence out/canyon-discovery-perf-{a,control,b}.
- Separate diagnostic GPU trace (NOT benchmark) frames1400–1699: scene avg1.788ms,
  SSAO .897, AA .439, bloom .421, tonemap .346. Profile itself stalls in timer
  collection; use pass costs for direction, not end-to-end performance claims.
  out/canyon-gpu-trace.json and canyon-gpu-profile.log.
- Tried existing opaque depth prepass on canyon. Romdev frame1850 seed1 matches
  baseline exactly: 0 changed RGB pixels. Native avg4.1686/4.2013,p957.9894/7.9416;
  final draws155/tris653004 vs91/351590. No convincing win; reverted scene property,
  rebuilt cart/native. Generator unchanged; Neon prepass remains enabled.
- Steady4ms requirement remains unmet. Next render optimization should consider
  scene shading and SSAO/post costs; do not repeat this failed canyon prepass test.

### SSAO duplicate sample experiment rejected (2026-10-21)
- Prior turn measured landmark cost/profiled GPU and rejected canyon prepass:
  progress. Investigated SSAO (~.897ms GPU in that separate diagnostic trace).
- Trial GLSL/WGSL cached previous sampled depth texel for each horizon side and
  skipped consecutive duplicates; identical samples cannot change max horizon.
  Radius, power, slice/step counts, pattern, filtering and resolution unchanged.
- Native/cart built. Romdev GLES seed1 Red Rock frame1850: exact RGB match to
  pre-experiment reference (0 changed pixels), errors0/missing0.
- Native 1080p High/locked, track1 seed1,2400frames/1700samples: avg4.1773/4.2273ms,
  p957.8474/7.9232, final draws91/tris351590. No reliable improvement over prior
  current runs4.1682–4.2287ms. Rejected: restored both pinned engine source files
  and rebuilt native/cart. Rootengine never changed for this experiment.
- Retained research patches only under out/ao-dedup-experiment-* and evidence
  out/canyon-ao-dedup-perf + canyon-ao-dedup.png. Do not repeat this failed branch
  optimization; steady4ms budget remains open. No visual quality reduced.

### Non-city spatial batching experiment rejected (2026-10-21)
- Previous turn rejected SSAO duplicate sample skipping after measured no win:
  progress. Inspected procedural rocks; before reducing visible mesh fidelity,
  tested smaller static scenery merge cells. No geometry/materials/physics changed.
- Candidate32: non-city x32/z32 instead of x256/no z subdivision. Neon unchanged.
  Romdev GLES seed1 Red Rock1850: exactly same RGB as baseline, errors0/missing0.
  Native Red Rock1080p High/locked,2400frames/1700samples: avg4.1724/4.1774,
  p957.8364/7.874; endpoint94draws321170tris vs baseline91draws351590tris.
- Powder Line2700frames/1700samples candidate32 avg3.7395/3.7522,
  p957.2533/7.1962,83draws189719tris. Same-session original256 control:
  avg3.7856/3.7739,p956.9394/6.9792,73draws230974tris.
- Candidate64 (x64/z32) Powder avg3.7728/3.7914,p957.4343/7.5006,
  79draws194704tris. Lower submission count did not improve tail latency.
- Rejected both candidates: restored x256/no depth cells for non-city and rebuilt
  native/cart. Existing Neon x32/z24 unchanged. All benchmarks exit0/errors0/
  missing0; sequential, no concurrent build or romdev stepping, system load saved.
- Evidence out/canyon-spatial32-perf, powder-spatial32-perf,
  powder-spatial256-control, powder-spatial64-perf, canyon-spatial32.png.
  Avoid claiming general speedup from triangle reduction or repeating these cell
  widths without a new bottleneck diagnosis. Fullscene geometry still benefits
  from tighter bounds for scale, but this particular change fails current p95 goal.

### Storm Cliffs discoveries (2026-10-21)
- Previous turn rejected smaller static batches based on p95 measurements:
  progress. Continued unfinished visual/discovery coverage on Storm Cliffs.
- Lighthouse55%finish: tapered red/cream tower, door/window, gallery railing,
  open lantern chamber, rotating opposed lamps and a three-gull circling flock.
  Two animated group transforms, static sections merged by material. No extra
  lights or volumetric passes. Sampled bank footprint seats stone foundation.
- Wreck82%finish: curved wooden hull staves with missing sections, ribs, snapped
  mast and red lifebuoy. First version looked like a raft; replaced round rails
  with authored hull panels, lowered keel. Double-sided material initially lacked
  a cart shader: replaced with separate inner/outer surfaces (real thickness),
  using existing material variant; final missing shader count zero.
- Cleared random rocks/pines from both discovery footprints. These skips affect
  subsequent scenery RNG placement, not track geometry or physics.
- Native/cart builds passed. Inspected out/coast-lighthouse-close.png (~1500)
  and out/coast-boat-final.png (2100), seed1. Final romdev3300-frame pedal runs
  on GLES AND WebGPU: finished1/crashes0/GPUerrors0/missing0.
- Working cart updated. No performance claim: include these new objects in the
  broader frame-time audit. Lantern/flock use kind12, opaque shared materials;
  hull is low-poly, no new rigid bodies. Broader visual polish remains open.

### Storm Cliffs performance check (2026-10-21)
- Prior turn added and visually verified lighthouse/wreck on GL+WebGPU: progress.
  Completed the pending native timing comparison, no runtime changes this turn.
- 1080p High locked, GLES AMD890M/Mesa26.0.8, seed1,track7,2600frames/1700samples;
  sequential control/current/control (two runs each), no concurrent build or
  romdev stepping. Asset pak hashes identical. Control is saved pre-coast binary
  /tmp/bikemesa-canyon-comparison/with-discoveries/bikemesa; includes Red Rock work.
- ControlA avg3.7893/3.8151ms,p955.0508/5.1862,max12.0156/12.0611;
  current avg3.8363/3.8301,p955.2528/5.4115,max13.0130/14.6358;
  controlB avg3.7976/3.8231,p954.9376/4.9263,max12.1750/12.0676.
- Current mean3.8332 vs pooled controls3.8063: +.0269ms (~.7%). Current p95
  mean5.3322 vs controls5.0252: +.307ms (~6.1%). Report this cost honestly, not
  as zero impact or a speedup. New scenery retained for visual value; steady4ms
  still not met. This includes rock/tree footprint clearances, not just adding
  isolated geometry. Final draw count71 both, triangles211852 vs215062 because
  scenery random placements change after clearance skips; endpoint != average.
- All six runs exited0, GPUerrors0/missingShaders0. Mean load1 across runs1.36–1.66;
  other desktop apps remained running, so no isolation claim. Binary/settings/
  load samples retained under out/coast-discoveries-{control,current,control-repeat},
  compact comparison out/coast-discoveries-comparison.json.

### Star Road discoveries and framing (2026-10-21)
- Previous turn repeated already-covered chocolate confirmation (no new progress).
  Resumed unfinished Star Road work and completed placement/runtime checks.
- Mechanical orrery at 47% finish: three thin emissive tilted rings with independently
  orbiting colored planets/moons, glowing central sphere and metal pedestal.
- Waving brass service robot and exploration pod at 75%. Robot's head turns toward
  the approaching rider; raised arm waves. No new lights or physics bodies.
  Static details merge; animated rigid groups merge independently.
- Initial 51%/82%, farther-bank placements clipped behind HUD. Moved both onto
  nearer, lower bank at z=-4.2 and reduced orrery radius. Independently sampled pod
  ground height fixes buried hull. Rock clearance follows new placements.
- Inspected out/cosmic-orrery-placement.png (1260 steps) and
  out/cosmic-robot-placement.png (1970): full silhouettes visible below HUD.
- Native/cart builds pass. Romdev seed1, track8, pedal3470, GLES and WebGPU both:
  finished1/crashes0/GPUerrors0/missingShaders0.
- Native 1080p High locked GLES, seed1,2500frames/1700samples, two current then two
  saved pre-discovery control runs, identical asset pak hashes; no concurrent
  build/emulator stepping. Current avg3.5081/3.5110ms,p956.2393/6.3507;
  control avg3.5137/3.5297,p956.2931/6.2720. Essentially unchanged within variation;
  not a demonstrated speedup. 4ms tail budget remains unmet. Desktop apps remain
  running; system load samples included. All runs exit0/errors0/missing0.
- Evidence out/cosmic-discoveries-{current,control}/results.json. Endpoint71draws
  both,202948vs204140triangles (clearance affects random scenery, not a whole-frame
  geometry measure). Broad map polish/performance objective remains unfinished.

### Pier Pressure discoveries (2026-10-21)
- Previous turn finished Star Road placements and timing comparison: progress.
- Added beach_discoveries: large sandcastle at48%finish, four unequal turrets,
  crenellations, arched dark entrance, red/blue pennants, bucket/handle and spade.
- At80%, open wooden treasure chest with brass bands and coins, two red crabs at
  different depths/scales with eyes, articulated legs and swaying raised claws.
  Static details use merged shared geometry/materials; four animated claw groups.
  No added lights or physics. Crab feet sample actual terrain per endpoint.
- Cleared random rocks/palms from landmark footprints after first screenshots
  exposed a rock in the castle entrance. Out/beach-castle-final.png and
  beach-crabs-seated.png retain visual evidence; inspected castle first/final
  approach and both crab passes. Tall rear tower briefly overlaps HUD on approach,
  full castle visible during the jump alongside it (1250 steps).
- Cart build passes. Native initial build passed; final leg-seat rebuild run too.
  Romdevseed1track12,pedal3130: GL before final foot-only correction and WebGPU
  after correction both finished1/crashes0/GPUerrors0/missingShaders0.
- Measured native performance comparison still pending; do not claim cost-free.
  Broader map aesthetics, downtown fidelity, Sojourner and sustained4ms remain open.

### Pier Pressure performance comparison (2026-10-21)
- Previous turn added/verified beach landmarks: progress. This turn completed the
  pending native comparison, retaining the scenery; no runtime code edits.
- GLES AMD890M/Mesa26.0.8,1080p High locked,seed1track12,2450frames/1700samples.
  Sequential control/current/control, two runs each, no concurrent build or
  romdev stepping. Saved pre-discovery control has identical asset pak SHA256.
- ControlA avg3.7130/3.7308ms,p954.4512/4.4979,max11.1939/11.0990;
  current avg3.7522/3.7336,p954.5387/4.4608,max9.4027/8.6888;
  controlB avg3.7147/3.7417,p954.5318/4.7420,max11.6828/12.2770.
- Pooled controlmean3.72505/current3.7429: +.01785ms (~.48%), within run variation.
  Mean p954.555725control/4.49975current; no demonstrated speedup or zero-cost claim.
  The4ms tail target remains unmet. Current max lower in this sample is not proof
  of hitch improvement. Endpoint78draws both,234762vs235250triangles; clearance
  reshuffles random scenery and this is not a frame-average geometry statistic.
- All six exit0,track12,finished1,GPUerrors0/missing0. Desktop apps remained active;
  meanload1 ranged1.93–2.46. Results preserve renderer/binary/settings/system samples.
- Evidence out/beach-discoveries-{control,current,control-repeat}/results.json,
  summarized in out/beach-discoveries-comparison.json. Next work should address
  outstanding map/fidelity or measured rendering costs, not repeat this benchmark.

### Powder Line walker ankle placement (2026-10-21)
- Previous goal turn completed the pending beach performance comparison: progress.
  Addressed the recorded snow-walker foot placement limitation next.
- Replaced leg pendulum animation with a 2.8s cycle: planted half follows opposite
  body travel, lifted half returns with smoothstep and a .22-local-unit sine lift.
  Body yaw now aligns exactly with retreat (.6,-.8). Stance .56local-unit travel
  over1.4s atscale.8 matches body .32m/s, reducing horizontal sliding.
- Feet are independent body children, remaining level while leg groups rotate and
  scale to ankle endpoints. Each foot samples bank triangles at center/toe/heel/
  sides to avoid snow penetration. Four legs solve after parent body transforms.
  Initial staggered stance matches first moving frame to avoid trigger pop.
- Uses existing motion-list lifetime/reset, no new physics, geometry assets or
  materials. This is lightweight ankle placement, not a full two-bone knee rig;
  conservative footprint clearance can leave small gaps on steep irregular snow.
- Inspected out/powder-walker-step.png and powder-walker-close.png (white walker),
  out/powder-second-step.png (brown at2150steps). Older powder-second-feet*.png
  missed the walker; not visual evidence for its placement.
- Cart/native builds pass. Romdev seed1 track2 pedal: GL initial gait completed
  finished1/crashes0/errors0/missing0; WebGPU final start-pose correction through
 3350steps also finished1/crashes0/errors0/missing0. No performance claim made;
  current change adds20 terrain samples/frame across four feet. Broader goal open.

### Downtown fidelity audit: Nile / Level One (2026-10-21)
- Previous turn corrected snow-walker ankle placement: progress. Resumed downtown
  reference audit, opening user's TheNile.png and LevelOne.png before edits.
- Nile already has red brick, nine grouped sash windows/curtains, four pilasters,
  stepped parapet, glass-block transom, projecting copper crest/marquee and Concert.
  Runtime exposed squat NILE wall lettering and an obsolete black rectangle
  protruding beyond crest shoulders. Raised label height1.45->2.15 and removed box.
- Level One already has tall white facade, paired sash windows/suspended shades,
  top logo, recessed patio/red stools/railing. Roof/logo clipped by camera.
  Moved facade z-14.5->-22.5 preserving its11.5m height and width/proportions;
  added shallow forecourt to reconnect sidewalk. This setback is a game staging
  adaptation, not a claim the real building has this forecourt.
- Inspected out/nile-audit.png vs nile-tall-audit-fixed.png and
  levelone-audit-centered.png vs levelone-setback-final.png. Roof/logo now fit;
  nearby checkpoint notification still briefly overlaps facade. Other28-venue
  fidelity audit remains incomplete; these are targeted corrections, not signoff
  on every detail (Egyptian crest remains a simplified geometric approximation).
- Native/cart builds pass. Romdevseed1track3pedal6020 GLES and WebGPU both:
  finished1/crashes0/GPUerrors0/missingShaders0. No new lights/physics/materials;
  removed one static box and added one forecourt box. No new perf benchmark needed
  for this geometry-neutral edit; no speedup claim. Sustained4ms goal still open.

### Downtown sign audit: Chupacabra / Arizona Distilling (2026-10-21)
- Previous turn corrected Nile/LevelOne staging: progress. Opened both user photos
  and checked live signs before changes. Chupacabra animal artwork/ring already pink
  HDR; Arizona main fascia still used rounded Fredoka while smaller duplicate logo
  used the distinctive serif artwork matching the photo.
- Replaced Arizona main generated lettering with existing credited azdistilling.png,
  tinting its alpha artwork coral (255,130,100) with4x HDR for existing bloom.
  Removed duplicate lower logo. Width6.8,height.62 before frontage scale leaves
  more fascia margin, closer to photo. No extra lights/pass; one fewer sign plane.
- Official site azdistilling.com now serves a dynamic shell; did not establish a
  higher-res source this turn. Existing source220x20 remains a sharpness limitation.
- Chupacabra sign was stretched by generic frontage scale1.22x/1.08y. Corrected
  face/ring horizontal scale to1.08 and cylinder's rotated vertical axis (also
  affected by depthcompression.62). Backing now48segments instead of10 to avoid
  polygonal outline. Storefront width/positions unchanged.
- Inspected out/chupa-audit-centered.png -> chupa-round-final.png and
  az-audit.png -> az-sign-final.png. Latter's streetlight briefly overlaps right
  lettering; relocation/presentation remains possible polish. Chupacabra facade
  remains simplified vs patio/reference; this is a sign audit, not full28 signoff.
- Native/cart builds pass. Romdev seed1track3pedal1480 GLES (before backing segment
  increase) and WebGPU final both GPUerrors0/missing0. No performance claim or
  full-course rerun needed for this localized material/geometry correction.

### 2026-10-21 — compact checkpoint notification
- Replaced the obstructive central CHECKPOINT banner with a gold highlight behind
  the existing upper-left checkpoint counter; same duration, final15tick fade.
  Other banners unchanged. Touch offset and per-player viewport retained.
- Romdev GL track3: out/checkpoint-counter-notice.png at3000frames,
  checkpoint-counter-expired.png after80more, checkpoint-counter-splitscreen.png
  with two riders at1430frames. Inspected highlight, fade and split placement.
  Native/cart builds subsequently passed with the bubble changes below.

### 2026-10-21 — interactive chocolate bubbles
- User requires chocolate bubbles to burst and dirty contacted scenery, rider and
  bike. Existing crash droplets already had swept contacts; river bubbles were
  missing. Added16 reusable chocolate bubbles across both river crossings.
- Deterministic launches every12ticks while a rider is within18m; ballistic motion
  uses the existing Box3D, upright rider/bike proxy, scenery and bank queries.
  Contact bursts into4 secondary flecks using the existing64-element pool and
  creates persistent surface decals or model mesh stains. No additional lights.
- Bubble pool separate from flecks prevents burst recycling the current bubble.
  Ambient emission capped512 per scene to bound additional persistent deposits;
  existing rider stain cap12 remains. Decorative waterfall streak animation is
  still separate; this change covers river bubbles, not every waterfall streak.
- Added chocolate ride [1|2] fixture alongside crash and bubble/body hit counters.
- Native/cart builds pass. Romdev seed1, track15, ride1+brake360frames GL:
  30emitted,25bubblecontacts,1directbubblebodycontact,391decals/11meshdecals.
  WebGPU ride2 same360frames:30/25/1,393decals/11meshdecals.
  Both GPUerrors0/missing0. Inspected out/chocolate-bubbles-rider.png and
  chocolate-bubbles-impact.png (earlier100frame crash test). No timing claim.
- Working cart: build/whimsy-cart/carts/bikemesa.wasc.

### 2026-10-21 — bounded persistent chocolate coverage
- Previous goal turn made verified progress: river bubbles, direct impact stains,
  native/cart builds and GL/WebGPU checks. Follow-up screenshot revealed redundant
  overlapping deposits; emission cap also eventually stopped both rivers.
- Removed512 lifetime bubble cutoff. Fixed16 bubble/64fleck pools continue emitting
  near riders throughout a race. Persistent ground/scenery coverage now admits
  two splats per40cm 3D cell and dominant surface face, using a2048entry hash table.
  Separate512-deposit budgets reserve coverage for each river. Existing marks are
  never evicted. Rider/bike stains and tire tracking retain their existing paths.
  This bounds splash deposits at1024; it does not cap all tire-track decals.
- Darkened ground and model impact/coating color from(.22,.055,.012) to
  (.055,.012,.004): prior tonemapped result read orange. Screenshot inspected:
  out/chocolate-dark-deposits.png. Ground marks now visibly dark chocolate.
- Native/cart builds and diff whitespace check pass. Romdev seed1 GL ride1+brake
  360frames:306 total decals vs391 before (85 redundant suppressed), same25 bubble
  impacts/1bubble-body hit,11mesh decals. +6200frames:338 deposits,2498suppressed,
  522bubbles/517impacts,385total decals (includes tracks). Ride2 without scene reset
  +360:571deposits,552bubbles/547impacts,630total decals. Thus second river still
  emits/deposits beyond old global cutoff and retains first river marks.
- Final WebGPU ride2+brake360:297deposits/83suppressed,309total vs393 before,
  same25bubblecontacts/1bubblebodyhit/11mesh decals. All checks GPUerrors0/missing0.
  Counts prove less redundant geometry, not a measured frame-time improvement.

### 2026-10-21 — Scary Town discoveries
- Previous goal turn made progress: bounded persistent chocolate coverage and
  continued bubble emission verified. Returned to outstanding map variety audit.
- Scary Town audit showed repeated scarecrows, houses, grave markers and fog;
  added three differently sized hovering sheet ghosts at48% of finish distance,
  with round heads, fluted hems, eyes/mouths and a grave/coffin prop below.
- Added giant eight-legged spider with four emissive eyes, downward fangs and
  polygonal silk web between timber posts. Initial77% placement was obscured by
  loop and tree; final85% placement verified unobstructed during normal riding.
- Shared opaque materials, static scenery batching and merged rigid animated
  groups: only3ghost groups and1spider group bob. No new lights/physics bodies.
  Removed nearby scarecrows and cleared local rock/dead-tree footprints.
- Native/cart builds pass. Romdev seed1 track9 pedal: GL first placement2850frames
  finished1/crashes0/GPUerrors0/missing0. Final WebGPU2990frames same result.
  Inspected out/scary-ghosts-first.png at1400frames and scary-spider-final.png at
  2190frames. Initial obstructed view: scary-spider-approach.png.
- No performance timing claim; separate controlled cost comparison remains.
  Ghosts are opaque stylized cloth figures, not physically translucent volumes.

### 2026-10-21 — Scary Town discovery cost and GPU profile
- Previous goal turn made progress: discoveries and normal-course visibility verified.
- Saved current/control native binaries with identical cwpak under
  /tmp/bikemesa-scary-comparison. Control omits only scary_discoveries call, keeps
  clearance changes. Source restored and production native rebuilt before timing.
- Six sequential runs: control2/current2/control2, GLES AMD890M Mesa26.0.8,
  1080p High locked, seed1 autoride track9,2750frames/1700sample window. No builds
  or emulator stepping concurrently. Desktop applications remain active;
  load1means1.83–2.32, so not an isolated machine benchmark.
- ControlA avg4.4784/4.5015,p958.6642/8.7135; current avg4.5095/4.4913,
  p958.3192/8.3857; controlB avg4.5441/4.4938,p958.8460/8.7298.
  Pooled avg4.50445control vs4.5004current: difference-.00405ms within variation,
  not a speedup claim. Endpoint111draws both;247137vs247217triangles (not means).
  All six exit0,finished1,GPUerrors0/missing0.4ms steady budget remains unmet.
- Evidence: out/scary-discoveries-{control,current,control-repeat}/results.json
  and out/scary-discoveries-comparison.json.
- Separate diagnostic GPU timer trace, frames1300–1599, at
  out/scary-gpu-trace.json: scene1.3905ms,SSAO.8345ms,volumetric grid.6576ms,
  volumetric composite.3151ms,bloom.3178ms,tonemap.2896ms,AA.2803ms.
  GPU timer query stalls distort CPU wall timings (r2d upload2.73ms in diagnostic);
  do not call that CPU work or mix profiled timing with unprofiled benchmark.
  Volumetric fog together~.973ms is a concrete next optimization candidate while
  preserving Scary Town atmosphere. Discovery geometry not the primary bottleneck.

### 2026-10-21 — rejected shared fog-ray experiment
- Previous goal turn made progress: controlled Scary Town cost comparison and GPU
  profile isolated volumetric fog as a candidate. Read actual pinned/root fog
  sources (identical): GL already uses host-supported8 MRT outputs where possible,
  WebGPU already compute; no trivial missing batching switch.
- Tried hoisting the world viewing ray and normalization out of GL's per-slice
  vf_inject calls into generated fragment main, sharing it across MRT slices.
  Kept grid resolution, lighting, noise, history and integration unchanged.
- 1080pHigh GLES AMD890M, seed1track9,2750frames/1700samples: candidate
  avg4.4682/4.4953,p958.2930/8.3886; saved same-content original immediately after
  avg4.4936/4.5119,p958.5447/8.4077. Mean difference-.021ms (~.47%), comparable
  to run variation and too small to justify retaining changed numerical ordering.
  All4runs render errors0/missing0 and complete. Not claimed a speedup.
- Reverted pinned volumetric_fog.c byte-for-byte (cmp to /tmp/vfog-before.c),
  rebuilt production native. Root engine/WebGPU/cart never changed. Retained
  experiment patch only at out/scary-fog-shared-ray-rejected.patch and results
  in out/scary-fog-shared-ray{,-control}/results.json. No visual acceptance claim
  needed for this rejected candidate.4ms target remains open.

### 2026-10-21 — Organic Bean / Phantom Fox reference audit
- Previous goal turn produced evidence rejecting an ineffective fog optimization;
  resumed incomplete28-venue fidelity audit using original OrganicBeanCafe.png
  and PhantomFox.png photographs (opened, not inferred from venue names).
- Organic Bean at2603frames seed1track3: full fascia lettering visible between
  existing trees. out/organic-audit.png confirms requested clearance; no edits
  needed for that requirement. Facade remains stylized vs narrow photographed cafe.
- Phantom already has wider15m source frontage, two side bays, one main wordmark
  and Goat&Ram patio sign. Kept both signs' existing sizes. Replaced12 rectangular
  shade strips with two tessellated sagging triangular fabric sails, two supports,
  charcoal arched spandrels over openings, and two picnic tables/benches.
- Cloth has explicitly reversed underside triangles/normals for overhead and
  low viewing angles, reuses existing gold material; static city batches, no new
  lights/material variants/animation. Triangle sails256triangles vs prior144box
  triangles, plus64arch triangles and simple table/support geometry.
- Native/cart builds pass; GL1550/1600 views (first sail heights) GPUerrors0/missing0.
  Final sag/height WebGPU1600 also0/0; inspected out/phantom-patio-final.png.
  Prior views: phantom-audit.png, phantom-patio-approach.png,
  phantom-patio-overhead.png. Existing jump camera puts patio near screen bottom;
  this is retained course staging, not full building/viewpoint fidelity signoff.
- No performance claim or redundant full-course gate for static local geometry.

### 2026-10-21 — Gus / Pizza Mart reference audit
- Previous turn made progress: Phantom patio correction and Organic sign clearance.
  Opened user's gus's2.webp and PizzaMart2.png for this audit.
- Gus at860frames seed1track3 retains projecting yellow/red chicken sign, outlined
  lettering/green WORLD FAMOUS, stepped white/brick parapet, striped awning and
  patio umbrellas. out/gus-audit.png; no Gus change required by this pass. Sign is
  reconstructed artwork, not an exact photo texture; left facade partly outside
  frame at this sampling point, so not claiming full architectural signoff.
- Pizza Mart at4050frames: pointed entry and both yellow signs read, but walls
  incorrectly inherited red brick. Replaced with dedicated128px muted block map,
  roughly40x20cm units via existing world-scaled masonry UVs. Adds~64KiB base
  texture plus mipmaps; material already existed, no new material category.
- Roof courses now follow the rotated main roof surface rather than approximate
  coordinates floating above it. Added18 courses on each steep entrance wing
  (36boxes/432triangles, statically batched), using the existing brown material.
- Native/cart builds pass. GL4050 pre-final block scale and WebGPU4050 final both
  GPUerrors0/missing0. Inspected pizza-audit-centered.png -> pizza-masonry-final.png
  -> pizza-blocks-final.png. pizza-audit.png missed venue and is not evidence.
  No timing claim. Working cart rebuilt;28-venue full fidelity audit remains open.

### 2026-10-21 — new complete Sojourner candidate
- Previous turn made progress: Pizza Mart masonry/roof corrections verified.
  Returned to the explicitly requested fifth rover; did not repeat prior login
  gated CAD/BlendSwap download attempts.
- Found Celestia Mars Pathfinder addon by DaveBowman2001, downloaded successfully
  from catalog6237BAE7-2CE9-F379-14F1-C88B814BD767. ZIP contains278742-byte
  sojourner.cmod plus rover1alpha.png,ROVERARM.png,rovertire.png and other lander
  files. Unlike SGI candidate, textures are supplied; conversion not yet attempted.
- Original author submission found via GitHub API:
  https://github.com/celestiamobile/celestia.mobi/issues/2#issuecomment-1000981871
  confirms2021-Nov-11 release. Follow-up comments describe case-sensitive filename
  fixes and SSC updates, not reuse terms. No README/license/credits file in ZIP;
  catalog/submission also do not state asset license. Do not infer GPL from app.
- Saved SHA/file inventory/original source in sojourner-candidates.json and
  references/rovers/sojourner-research/celestia-source-review.json; downloaded ZIP
  /tmp/sojourner-celestia.zip. Sojourner still not in runtime; four other rovers
  remain. No build needed: research metadata only. Other active work unblocked.

### 2026-10-21 — chocolate bubble impact confirmation
- User reiterated bubbles must splatter and stain what they hit, including rider
  and bicycle. Existing implementation already sweeps bubble/fleck collisions
  against scenery, upright rider/bike proxies and ragdoll Box3D bodies; bursts
  spawn secondary colliding flecks and persistent screen/mesh decals.
- Rechecked working cart with romdev GL seed1: Candy track15, ride river1,
  brake360 frames:30 bubbles,25 bubble impacts,1 bubble/model impact,45 total
  model impacts,294 scenery deposits,11 mesh decals. GPUerrors0/missing0.
- Then crash river2 +180 frames:40 cumulative bubble impacts,451 deposits,
 14 mesh decals; GPUerrors0/missing0. Screenshots chocolate-bubble-confirm.png
  and chocolate-bubble-crash-confirm.png inspected. Scenery mess is prominent;
  individual model stains remain subtler at gameplay camera scale. No new
  performance claim or code change required for the requested collision behavior.

### 2026-10-21 — arena variants and shared host texture fix
- Previous turn completed chocolate collision verification. Resumed other examples:
  existing bomberdude/pugball/derby cart build session37838 finished exit0.
- Romdev GL400 frames each: Bomber8 arenas and Derby2 arenas errors0/missing0.
  Pugball stadium1->2 reproducibly throws Invalid typed array length524288;
  direct load2 succeeds. Later failed GL entries in arena-variant-checks.json
  inherit the broken host and are NOT independent arena failures.
- Chromium reproduced same transition; stack identified bind_texture ->
  glTexImage2D -> Uint32Array. wasmcart _bytesPerPixel omitted integer RED/RG/RGB,
  defaulting to4channels, overreading valid single-channel integer payloads.
  Fixed formats in standalone wasmcart/src/webgl_imports.js and engine host copies
  deps/wasmcart and deps/wasmcart-wgpu (targeted edit, not whole-file overwrite).
  Added4 boundary regression tests covering image/subimage with exact end-of-memory
  uint32 payloads. All pass. Temporary renderer diagnostics fully reverted.
- Chromium used vendored host, so first post-fix run still failed before the
  vendored copies were patched. Final patched run2020frames with all5stadiums
  transitions completed exit0; state pugball-stadium-transitions-gl.json.
- Romdev WebGPU all8Bomber,5Pug,2Derby variants400frames each errors0/missing0.
  Derby players4 checked both variants; derby-four-views-wgpu.png inspected.
- New visual issue: pugball-stadium5-gl.png after sequential transitions shows
  camera at pitch level/player not visible. Investigate camera/physics state next;
  no full visual pass claimed. Main romdev process may retain pre-fix GL JS module
  until restart; fresh Chromium tested actual patched host. No timing claims.

### 2026-10-21 — Pugball replacement-camera fix
- Previous turn made progress: shared integer texture upload fix plus arena checks.
- Direct stadium5 starts frame400 show proper chase view. Replacing with stadium4
  reproduced floor-level camera on WebGPU too (pug-camera-transition-before.png).
- Cause: load_scene replaces the camera and enables tick interpolation, but leaves
  G.chase_on true. camera_tick skips its chase-entry interpolation disable, so
  snapshots overwrite the per-render-frame pose. Reset chase_on/cf_live on scene
  load; explicitly disable interpolation for extra split cameras as well.
- Native/cart pugball builds pass. Romdev WebGPU stadium5->4->2,400frames each,
  errors0/missing0; inspected pug-camera-transition-fixed.png. Fresh Chromium GL
  stadium1->5 at frame403,810 total, errors0/missing0; pug-camera-fixed-gl.png
  shows player and normal chase framing, unlike previous floor-level screenshot.
- Joined four players through normal team-sheet pad input, then stadium1->4,
  400frames each WebGPU: cam.views4/errors0/missing0. Screenshot
  pug-camera-four-fixed.png inspected. No timing claim; broader aesthetics/perf
  objective remains incomplete.

### 2026-10-21 — fog slice-distance experiment rejected
- Previous turn fixed Pugball replacement cameras with GL/WebGPU/split evidence.
  Returned to steady4ms goal using scary-gpu-trace.json: GPU fog grid~.658ms.
- Tested computing96 depth slice positions/lengths once per frame on CPU and
  uploading vec2 array to GL injection/integration shaders, avoiding per-froxel
  powers without changing grid size/samples/lights. Pinned engine only; root,
  WGSL and cart unchanged. Saved experiment at out/fog-slices-experiment.patch.
- Six sequential native GLES High1080p seed1 track9 runs2750frames/1700samples,
  GPUtimersoff; control2/candidate2/control2. Controlavg4.4426/4.5007 then
  4.4668/4.5021ms; candidate4.4739/4.5121ms. Pooled candidate+.01495ms,
  no improvement. All6 exit0/errors0/missing0; draw111/tri247217 at endpoint.
  Machine-load samples and renderer recorded by benchmark in out/fog-slices-*.
- Reverted source byte-for-byte (cmp passes) and rebuilt production native.
  No visual approval needed/claimed for rejected candidate. Evidence and decision
  out/fog-slices-comparison.json. Do not repeat this experiment or shared-ray
  hoist without new evidence. Steady4ms and long-frame tail still unresolved.

### 2026-10-21 — gummy-bear silhouette and surface polish
- Previous turn produced evidence against fog slice precomputation; reverted it.
  Returned to explicit gummy shape/translucency requirement. Current screenshot
  gummy-current-audit.png showed merged torso/head/paws despite real transmission.
- Reshaped shared mold: narrower belly/neck, distinct round paws and ears,
  separated feet, smaller head height. Smooth-union radius.19->.10. Removed
  sub-voxel smile grooves after screenshots exposed tooth-like refraction holes;
  continuous muzzle now renders cleanly (gummy-muzzle-final.png).
- Material keeps physical transmission, now.9, roughness.24,IOR1.35,thickness.45;
  base color matches absorption hue instead of white. Saturated jelly with softer
  reflections. No extra objects/materials/lights; shared mesh10956tri vs11564
  before (~5.3% fewer),5480vertices. No frame-time improvement claimed.
- Native/cart builds pass. Romdev final GL1620seed1track15 crashes0/GPUerrors0/
  missing0. WebGPU1620 material+silhouette before final smile removal also0/0/0;
  final screenshot inspected. Candy full visual acceptance remains subjective;
  working cart includes final mold/material.

### 2026-10-21 — terrain-seated village paving
- Previous turn improved gummy mold/material. Screenshots still showed settlement
  paving as tall broken gold walls on slopes (related to user's ribbon complaint).
- Replaced horizontal boxes sized to highest corner with thin tiles clipped to
  the actual bank mesh triangles,4.5cm above surface,16cm buried perimeter skirts.
  A first corner-only quad attempt cut into terrain at internal diagonals; rejected
  that intermediate and added exact triangle/rectangle clipping instead.
- Paving generated once per branch, then statically batched. No per-frame ground
  queries, new materials/lights/physics. Branch paths and river clearances retained.
- Native/cart builds pass. GL Candy1620 errors0/missing0; screenshot
  candy-paving-clipped.png inspected versus gummy-muzzle-final.png and intermediate
  candy-paving-follow-ground.png. WebGPU Toybox1350 errors0/missing0;
  toy-paving-clipped.png inspected. No frame-time improvement claim.

### 2026-10-21 — seat settlement foundations on slopes
- Previous turn fixed paving; its Toybox screenshot exposed daylight underneath
  the large left house. Existing foundation was fixed1.8m tall at centre height.
- Keep floor/top elevation and footprint; sample rendered bank around all four
  foundation edges at<=40cm intervals, extending bottom25cm below lowest sample.
  Applies to all18houses in each of Candy/Toybox. Same boxes,materials,triangles;
  load-time queries only, no additional runtime simulation or draw objects.
- Native/cart builds pass. Romdev GL Toybox1350 and WebGPU Candy1620seed1:
  GPUerrors0/missing0. toy-foundations-seated.png and candy-foundations-seated.png
  inspected; former floating left foundation now meets the hill. No timing claim.

### 2026-10-21 — Espíritu and Guadalupe reference audit
- Previous turn seated Candy/Toybox foundations. Read user original Espiritu.png
  and GuadalupeOnMain.png before auditing GL Neon2120/4251 (~x372.3/830.97).
  Espíritu uses supplied angular wordmark (not fish), three arched patio bays,
  railings/closed umbrellas/string lights; inspected espiritu-audit.png. No edit.
  Pizza Mart retains previous masonry/roof-course fixes and second-last position.
- Guadalupe is correctly last at828, but audit showed missing glazed triangular
  transom and round onion-like shrubs, unlike reference's desert garden.
  Added triangular entrance glass with3 mullion/frame boxes using existing
  roof/glass materials. Replaced10 round/cone clusters with shared9leaf agave
  mesh,90tri per plant, different rotations/scales. No new lights/materials.
  Official sign unchanged. Source/materials remain stylized, not full fidelity signoff.
- Native/cart builds pass. WebGPU4210seed1Neon x822.10 errors0/missing0;
  guadalupe-transom-agave.png inspected. Original before guadalupe-audit.png.
  No frame-time improvement claim. Full28venue audit remains open.

### 2026-10-21 — chocolate bubble impact recheck
- User reiterates bubbles must splatter on scenery, player and bicycle. Existing
  swept contacts, secondary bursts, persistent scene decals and attached model
  stains already implement this; no duplicate effect added.
- Fresh romdev GL seed1 Candy: chocolate ride1 + brake360 produced30 bubbles,
  25 bubble impacts including1 rider/bike hit,45 total model contacts,293 deposits.
  Then crash2 +180:40 bubble impacts,453 deposits,14 mesh decals/56 pieces;
  GPU errors0/missing0. Inspected out/chocolate-impact-recheck.png: ground splats
  and airborne drops visible; body coating remains subtle at this camera distance.
- Stains are bounded for performance (12 targeted model impacts per rider plus
  broad crash coating; spatially limited scene deposits), retained for scene life.

### 2026-10-21 — Margaritas reference detail pass
- Previous turn verified chocolate contacts/stains; no gameplay change needed.
- Compared original Margaritas.png with GL Neon3560: correct two stories,
  five upper windows, balcony doors and readable official wordmark. Missing
  reference patio strings and balcony side rails.
- Added two sagging strings with24 warm bulbs,72 short wire segments,4 front
  support posts, and balcony side rails. Existing trim/lamp materials, static
  batches; no new dynamic lights, textures, simulation or transparency.
- Native/cart builds pass. WebGPU3560 before final support posts and GL3560
  final both errors0/missing0. Inspected margaritas-patio-final.png and
  margaritas-patio-supported.png against margaritas-audit.png. No timing claim.
- Hope's/Lost Dutchman reference audit and wider fidelity/performance work remain.

### 2026-10-21 — Hope's signage / Lost Dutchman audit
- Previous turn improved and verified Margaritas. Compared original photos with
  GL Neon3733/3903: Lost Dutchman canopy/name/door/shrubs readable, no edit.
  Four modeled columns vs five in reference, and canopy course relief still
  simplified; audit is not a complete architectural fidelity signoff.
- Hope's had rectangular-block emblem and short FRYBREAD letters. Replaced five
  boxes with22triangle angular emblem matching the notched photo silhouette;
  increased lettering plane height1.12->1.65. Main sign remains painted teal.
- Added missing pink OPEN window lettering with cyan chevrons using basic HDR
  materials and existing bloom. Four bars, one text plane; no new point lights.
  HOPE'S font remains sans-serif approximation rather than the photo's script.
- Native/cart builds pass without prior fabsf(integer) warning (removed old
  emblem loop). WebGPU3900seed1 errors0/missing0. Inspected hopes-sign-final.png
  against hopes-audit.png; lost-dutchman-audit.png also inspected. No timing claim.

### 2026-10-21 — Lost Dutchman roof and porch fidelity
- Previous turn improved Hope's; identified4vs5 porch columns and flat-shaded
  Lost Dutchman tile courses. Fixed those concrete discrepancies this turn.
- Restored5columns with beams/braces. Roof now uses analytic curved normals and
  4.5cm front lips at each overlapping course. Reduced tile subdivisions from
  64columns/6facets to48columns/4facets so adding lips keeps3072 roof triangles.
  One static mesh, existing materials, no extra lights or per-frame processing.
- Native/cart builds pass. Romdev GL Neon3730seed1 GPUerrors0/missing0;
  lost-dutchman-roof-final.png inspected against lost-dutchman-audit.png and
  user's original photo. Tile courses clearly visible; sign remains unobscured.
  No frame-time improvement claim. Broader example performance/fidelity ongoing.

### 2026-10-21 — current Neon renderer-aware performance audit
- Previous turn improved Lost Dutchman and verified GL. This turn measures current
  visual work without modifying geometry; no builds or other benchmark in parallel.
- Native1080pHigh locked,seed1,autoride,queries off, two runs per configuration.
  out/neon-facade-audit-early:2300frames,last1700,890M offscreen,
  avg4.0114/4.0045ms,p955.9401/5.9851,endpointx414.19.
  out/neon-facade-audit-late:4400,last2048,890M offscreen,
  avg3.9304/3.9255,p955.3168/5.6578,max46.12/15.08,endpointx859.91.
- Explicit DRI_PRIME=pci-0000_67_00_0 with offscreen still selected890M;
  out/neon-facade-audit-rx7600 is MISNAMED, actual renderer in JSON is890M!
  avg3.9345/3.9455,p955.6959/5.4746. Do not label these discrete results.
- sdl_common.c automatically chooses offscreen for CW_FRAMES without explicit
  SDL_VIDEODRIVER. SDL_VIDEODRIVER=x11 plus same DRI_PRIME selected RX7600.
  out/neon-facade-audit-x11:4400,last2048,avg1.6542/1.6532ms,
  p951.8322/1.8509,max8.1492/8.1109;CPUavg.968/.9836ms.
  Endpoint164draws317392triangles (snapshot,not whole-run average).
- All8runs exit0/GPUerrors0/missing0; same deterministic endpoint within each
  range. Sampled1min load~2.18–2.70, applications left running. All records include
  CPU/GPU utilization,actual renderer,binary hash. No attribution of spikes to
  other applications, and no before/after speedup claim across different hardware.
- Late Neon section meets average/p954ms on RX7600, not worst-frame bound.
  Need default interactive renderer and remaining map/early-course hardware-matched
  measurements before generalizing. Earlier890M scene/fog results remain valid for
  that renderer, but cannot be used as discrete RX7600 budget regressions.

### 2026-10-21 — benchmark renderer guard and RX7600 follow-up
- Previous turn identified default offscreen890M vs explicitX11 RX7600 and
  measured late Neon. Added --expect-renderer substring guard to bench-neon.py.
  Each run prints actual GL renderer; JSON includes expectation/match verdict.
  Mismatch retains raw evidence but exits nonzero before accepting timings.
- Live negative20frames expected NOT_A_REAL_RENDERER rejected890M,exit1;
  out/renderer-guard-negative records renderer_matches:false. No second run.
- Sequential1080pHigh seed1,queriesoff,X11 DRI_PRIME=pci-0000_67_00_0,
  --expect-renderer 'RX 7600',two runs each, no concurrent builds/benchmarks:
  out/neon-early-rx7600-guarded (2300frames,last1700):avg1.7014/1.6985ms,
  p951.8842/1.8661,max5.306/4.3201,endpointx414.19.
  out/scary-rx7600-guarded (2750frames,last1700):avg1.8159/1.8205,
  p952.1514/2.155,max5.7857/7.2044,finished1 endpointx479.25.
- All4 positive runs renderer_matches:true,exit0,errors0/missing0;
  observed1min load2.02–2.46. Existing sampled CPU/GPU load evidence retained.
  Scary's previous~4.48ms/8.4p95 concern was890M, not RX7600 regression.
  These data do not prove worst-frame4ms, multiplayer or all-map performance.
  Continue visual/gameplay fidelity work and target future profiling to remaining
  demonstrated bottlenecks; don't repeat rejected fog micro-optimizations.

### 2026-10-21 — NCT / Gus's scalloped fabric awnings
- Previous turn added renderer guard and established RX7600 earlyNeon/Scary
  timing. Returned to reference fidelity: NCT/Gus striped awning shared helper
  ended in rectangular blocks, unlike supplied photos' scalloped fabric.
- Replaced per-stripe two boxes with thin sloped cloth and4segment scalloped
  valance. Explicit reverse faces/normals keep existing opaque red/cream materials.
  Two construction meshes per awning, same static batching;20tri/stripe vs24
  before (16.7% fewer), no new lights/materials/textures or runtime processing.
- Native/cart builds pass. Romdev WebGPU Neon810seed1 x111.24 errors0/missing0;
  gus-awning-final.png inspected. Existing projecting chicken sign readable,
  scallops distinct from the previous rigid edge in gus-nct-audit.png.
  No frame-time improvement claim; full venue fidelity audit still ongoing.

### 2026-10-21 — Sojourner official repository audit
- Previous turn improved shared awnings. Revisited outstanding fifth Mars rover.
- Queried NASA-3D-Resources complete recursive master tree (truncated:false): no
  Sojourner/Mars Pathfinder model; only CALIPSO contains Pathfinder substring.
  Recorded exact API source/result in sojourner-candidates.json.
- Search reconfirms argonius BlendSwap15250 CC0,9912polygons,author-created textures,
  but known sign-in download restriction remains; did not repeat gated downloads.
  Celestia and SGI candidate terms still unresolved, not integrated.
- Added official JPL dimensions65x48x30cm and NASA visual references (Moe APXS
  photograph and rover drawing) to candidate metadata. These support an original
  accurate reconstruction if no downloadable reusable source can be obtained.
  No new rover shipped; four sourced rovers still in runtime. Goal remains active;
  this asset gap does not block other fidelity/performance work.

### 2026-10-21 — original Sojourner reconstruction integrated
- Previous turn established official asset absence and recorded NASA references.
  Authored original sojourner.inc rather than reuse unlicensed candidate meshes.
  CREDITS and candidate metadata explicitly distinguish original reconstruction
  from downloaded NASA rovers; downloaded candidates remain unintegrated.
- Local dimensions approximate65x48x30cm, display scale3 for discovery visibility.
  Six wheels with metal grousers/hubs, rocker-bogie links, gold chassis,96 blue
  solar cells, low stereo pair, rear APXS head/arm, whip antenna. Simplified model,
  not exact scientific reconstruction. One new opaque solar material; existing
  metal/foil/dark reused. Direct children flatten into existing scenery batches.
- Placement39%finish (x183.3),z-4.6 between Spirit28% and Opportunity48%, away from
  start. Terrain slope/cross-slope plus six contact samples seat the whole rover.
  Existing four sourced rovers and their indexing unchanged. No per-frame work.
- Native/cart builds pass. Romdev GL1560+/1620+ and WebGPU1620 errors0/missing0;
  sojourner-placement.png/sojourner-close.png inspected. Clear on approach, nearer
  bottom edge when camera follows rider's jump. sojourner-wgpu.png captured.
  No timing claim. Further fidelity review and broader goal work remain active.

### 2026-10-21 — Toybox ball-pit finale audit and measured budget
- Previous turn integrated original Sojourner. Rechecked current Toybox finale
  against explicit requests, not just presence of pit geometry.
- Romdev WebGPU autoride track16:1980frames then90. Screenshots
  toy-pit-flight-audit.png and toy-pit-impact-audit.png inspected: rider launches,
  balls visibly scatter above walls/outside pit, transparent plexiglass shows
  packed balls. Results absent during tumble; later3300 screenshot shows results.
  Finished1/crashes0, GPUerrors0/missing0. Code confirms420tick (7s) delay and
  .12density Box3D balls;432pitballs,264other toys remain simulated.
- No speculative distance-based render suppression added: physics and visibility
  are working, and actual timing was needed before changing multiplayer behavior.
- Guarded RX7600 X11 native1080pHigh seed1,2350frames,last800 covers final approach,
  launch,pit and results. Two sequential runs avg1.8174/1.8169ms,
  p952.1152/2.1311,max5.3646/5.2622;sim threadCPUavg.5678/.5705ms.
  out/toy-pit-rx7600-guarded/results.json retains renderer/load/hash/settings.
  Both exit0/renderer_matches:true/errors0/missing0. Target4ms met atp95 for this
  single-player window; not a worst-frame guarantee or multiplayer budget proof.

### 2026-10-21 — remaining example WebGPU effect checks
- Previous turn verified Toybox finale/perf. Resumed all-six-example scope:
  rebuilt Chromite and Spout carts, exit0. Earlier entries covered GL first
  scenes; this pass exercises WebGPU with current shared renderer/host changes.
- Chromite: initial south held before first frame did not launch menu; caught
  game.state0 rather than treating400frames as gameplay. Released/repressed,
  confirmed game.state1. Gallery exposes both boss/five enemy forms;16 debug
  blasts produced13 active clustered flash lights, live reflection captures206+
  and GPUerrors0/missing0. Inspected chromite-wgpu-gallery-audit.png and
  chromite-wgpu-blasts-audit.png. Debug gallery/effects checks, not a full level run.
- Spout: start30/release34/confirm50 then up500, game.state2/game.level0,
  spider.pos.y9.6333; GPUerrors0/missing0. spout-wgpu-climb-audit.png inspected:
  spider/pipe shadows, paper textures, HUD and sunny weather render correctly.
- No runtime code edits or speedup claim this turn. Remaining scope includes
  later Spout levels/weather, Chromite actual progression, and hardware-matched
  performance windows beyond the already verified BikeMesa sections.

### 2026-10-21 — chocolate bubble contact recheck (WebGPU)
- User reiterated bubbles must splatter and mark objects, rider and bike.
  Confirmed existing swept Box3D / upright limb-and-wheel proxy / scenery
  contact path emits impact flecks and persistent scene or skin mesh decals.
  Visible rider and bicycle skins share the decal receiver. No duplicate
  effect implementation added. Fixed pools and bounded deposits retained.
- Romdev current cart, WebGPU seed1, Candy track15: ride river1 + brake360
  produced30 bubbles,25 bubble contacts,1 bubble rider/bike contact,45 total
  body contacts and294 deposits. Crash river2 +180:45 bubbles,40 contacts,
  46 body contacts,452 deposits;14 mesh decals /56 pieces. GPU errors0,
  missing0. Inspected out/chocolate-bubble-current.png: airborne bubbles
  and scattered brown deposits visible. Body stains are subtler at gameplay
  distance; counters prove contact/mesh generation, not artistic prominence
  on every limb or every possible moving prop. No new performance claim.

### 2026-10-21 — posed crash chocolate coating
- Prior turn yielded runtime contact evidence but identified subtle rider mess.
  Found broad crash stamps all centered on bicycle frame, missing separated
  ragdoll clothing. Added torso and both thigh pose-centered stamps alongside
  the three bicycle stamps; existing skinned mesh decals follow motion.
- Six2048-triangle caps replace three4096 caps: same12288 total broad-coating
  ceiling; three additional one-time projector nodes, not per-frame spawning.
  Existing12 impact-stain cap and scene coverage limits unchanged.
- Native/cart builds pass. Fresh romdev WebGPU seed1 track15 crash river2,
  180frames:7 mesh decals,11862 triangles,29 pieces,errors0/missing0. Inspected
  out/chocolate-posed-coat.png: brown clothing patches now visible on the
  near leg/underside of torso as the rider lies separated from the bike.
  Dark shirt still has lower contrast; no measured speedup claim.

### 2026-10-21 — chocolate crossing budget evidence
- Previous turn changed and verified pose-centered crash coating (progress).
  Measured current native Candy Comet on guarded RX7600 X11,1080p High,
  seed1,queries off,other applications left running. No concurrent builds.
- Full-course2900frames,last2048:avg1.6700/1.6515ms,p951.9276/1.7952,
  max6.1530/8.4847;finished1 checkpoint3 both. Saved
  out/candy-chocolate-rx7600/results.json.
- Added end-of-run chocolate stats/counters to bench-neon.py for track15.
  Crossing-focused2350frames,last1700:avg1.6711/1.6356ms,
  p951.8670/1.7962,max8.1107/7.0133. Both atx408.58 checkpoint3,
  309 persistent deposits,31 bubble hits,25 body hits,2 splashes,
  382 total decals /12mesh /7431triangles /34pieces. Identical workload
  counters between runs. Renderer guards pass,exit0,errors0/missing0.
  out/candy-crossings-rx7600/results.json includes load1.50–2.18.
- P95 under4ms for these single-player windows, not worst-frame guarantee;
  maxima remain above4ms and multiplayer crossing budget remains unproven.
  No before/after speedup claim and no attribution of tails to other apps.

### 2026-10-21 — four-rider chocolate crossing evidence
- Previous turn established solo timing; extended benchmark with --players1..4
  using existing CW_START_CMDS players/track and CW_INPUT scripted pads.
  No gameplay autopilot change. End command verifies requested player count.
- Romdev WebGPU four south-held pads:1200then900frames,all four riders pass
  both rivers,checkpoint3.437 deposits,119 body contacts,31 bubble hits,
  44meshdecals. Inspected candy-four-riders-crossing.png /late.png: four
  rendered viewports and riders, geometry/HUD correct; errors0/missing0.
  Screenshots after crossings do not prove every stain's attachment individually.
- Native guarded RX7600 X11,1080pHigh,seed1,2350last1700,queriesoff:
  averages1.9989/2.0352ms,p953.8332/3.6500,max13.4482/15.8416.
  Both players4,x406.69,checkpoint3,734decals,44mesh,437deposits,8splashes,
  119bodyhits;errors0/missing0,exit0. Load2.29–2.59,other apps left running.
  out/candy-four-rx7600/results.json retains workload/renderer/settings.
- Four synchronized riders within4ms atp95, much narrower margin than solo;
  tail stalls worse. Divergent cameras and heavy simultaneous ragdolls remain
  separate workloads to measure. No speedup or worst-frame guarantee claimed.

### 2026-10-21 — GL decal viewport upload storage
- Previous turn established four-rider workload (progress). Inspected shared
  engine decals_exec: every viewport reused offset0 of one instance VBO via
  SubData, storage only replaced on growth. Orphan storage with BufferData
  NULL/STREAM_DRAW before each visible upload, retaining grow-only capacity.
  Root cartwheel-engine/src/fx/decals.c and actual pinned engine updated;
  WebGPU unchanged. No appearance/coverage reduction.
- Native/cart builds pass. Saved pre-change native exe+cwpak in
  out/candy-decal-before. Guarded RX7600 four-player2350last1700 runs:
  after averages1.9709/1.9611,p953.4611/3.6957; before recheck
  2.0450/2.0086,p954.1807/4.1667; after recheck1.9991/1.9808,
  p953.8867/3.5048. Prior baseline1.9989/2.0352,p953.8332/3.6500.
  Modest average improvement in these samples, p95 variable; not a universal
  speedup claim. Worst tails still13–20ms, not fixed. All workload counters
  identical437deposits/119bodyhits/8splashes;264draws/778620triangles.
  Logs in out/candy-decal-stream-{after,before-recheck,after-recheck}.
- Romdev GL four pads995frames,inspected candy-decal-stream-gl.png:
  river splats visible consistently in four views,33deposits at capture,
  errors0/missing0. This shared engine upload change applies to screen decals
  in other games when rebuilt; no broader game speedup claim.

### 2026-10-21 — Spout weather HUD and shared-engine rebuild
- Previous turn improved shared GL decal upload (progress). Rebuilt all six
  primary native/cart targets with that engine: bikemesa,bomberdude,chromite,
  derby,pugball,spout. Both build processes exit0; logs
  /tmp/all-{native,cart}-decal-weather.log. No new warnings/errors in logs.
- Spout winter HUD had rain strokes and next-rain label; city night predicted
  sun. Added three small snowflakes using existing batched2D lines,theme-aware
  next snow/rain/clear/sun text. No assets or new passes.
- Existing debug level1..5 command loads via pending normal start_level path,
  not unlock mutation. Used to verify WebGPU winter820frames snowing with
  snowflakes;city820 raining/clear-in7;winterreset sunny/snow-in8. Screenshots
  spout-snow-forecast.png,spout-night-forecast.png,spout-snow-next.png all
  inspected;errors0/missing0,levels3/1 zero-based,statePLAY.
- Earlier later-level captures spout-level2-weather.png through level5 exist
  from weather checks; no full-level progression or speedup claim. Other games
  rebuilt this turn, not a substitute for broader gameplay/runtime audits.

### 2026-10-21 — four-rider ball-pit finale
- Prior turn corrected Spout weather and rebuilt six games (progress). Tested
  current Toybox four-player finale, not inferred from single-player success.
- Romdev WebGPU four pedaling pads2080frames:all finish,balls airborne well
  above plexiglass and spilling outside;results hidden during impact. Further
  500frames shows four-player results,all0wipeouts,29.11/29.11/29.78/29.78.
  Inspected toypit-four-impact.png and toypit-four-results.png. Code retains
  420tick delay;errors0/missing0. Riders buried by balls at impact capture.
- Native guarded RX7600 X11 1080pHigh seed1,2350last800:avg1.9538/2.1611ms,
  p952.7443/3.5085,max5.9925/6.3757. Sim threadCPUavg.6873/.7421ms.
  Both players4,finished1,checkpoint3,errors0/missing0,exit0;172draws and
  1016376triangles endpoint. Load2.18–3.43 recorded with other apps untouched.
  Evidence out/toypit-four-rx7600/results.json. No speedup claim.
- Four-rider landing window meets4ms atp95 in these runs, not every frame;
  this verifies the requested spectacle and current light-ball physics under
  multiplayer without reducing ball count or plexiglass quality.

### 2026-10-21 — Nile/Level One reference comparison
- Previous turn verified multiplayer pit (progress). Reopened user's original
  TheNile.png and LevelOne.png and compared current WebGPU ride captures
  nile-fidelity-current.png /levelone-fidelity-current.png. Tall silhouettes,
  Nile nine sash windows/stepped brick front/projecting Concert marquee and
  Level One paired upper windows/suspended shades/recessed patio represented.
  This is a simplified reconstruction, not a blanket28venue fidelity signoff.
- Nile crest previously vertical bars plus oval; replaced bars with tapered
  horizontal striped headdress silhouette and small facial features based on
  photograph.24 planar stripe triangles+four tiny boxes replace11boxes+eye
  bar;72 fewer triangles,shared existing materials,static scenery batches.
  No new lights/textures or per-frame animation.
- Native/cart builds exit0. Romdev WebGPU track3 seed1 heldpedal2270frames,
  errors0/missing0;inspected nile-crest-shaped.png at matching camera.
  Distinct striped peaked crest now visible; remains a stylized approximation
  of the detailed Egyptian portrait. No performance measurement claimed.

### 2026-10-21 — storefront lettering cleared of checkpoint pennants
- Previous turn improved Nile crest (progress). Reopened OrganicBeanCafe
  reference and captured current approach:trees framed fascia correctly, but
  track checkpoint pennant crossed ORGANIC lettering.
- Neon-only checkpoint poles reduced3.2to1.9m with pennants lowered in tandem.
  Checkpoint positions/triggers/count/flag width retained. Other maps unchanged.
  Geometry/material/draw counts unchanged; no new per-frame work.
- Native/cart builds exit0. Matching seed1 WebGPU2576-frame approach inspected
  organic-fidelity-current.png vsorganic-clear-pennant.png: full fascia readable,
  bright flag still conspicuous beside rider. Errors0/missing0. No timing claim.

### 2026-10-21 — current all16course completion sweep
- Previous turn removed storefront pennant obstruction (progress). Inspected
  existing giant-loop gate expectations and ran current native primary build
  through all16tracks with pedal-only autoride,seed1,1080pHigh,RX7600X11.
  This is gameplay completion evidence,not timing or cross-host determinism.
-4000frames each exceptNeon5200:15tracks finished,no crashes,all endpoint
  render errors0/missing0. Moon4000 remainedx308.88 checkpoint2,crashes0;
  extended8000 finishedx475.42 checkpoint3,crashes0. Slow lunar pace,not
  obstruction. Existing gravity/traction/course unchanged.
- Neon all10checkpoints;tracks4,9,14 each loop1;circus4checkpoints andfinish.
  All16 pass after Moon extension. Results/logs plus binarySHA saved
  out/all-tracks-current/results.json; every initialrun actualRX7600 verified.
  Does not prove every optional route,rider choice,multiplayer,or art fidelity.

### 2026-10-21 — other five examples hardware-guarded baselines
- Previous turn proved all16BikeMesa finish routes (progress). Extended
  tools/bench-examples.py with current comparison mode,renderer guard,
  gameplay state validation,unique run filenames,Spout climb/Chromite fire
  input. Existing sort/crowd fixtures preserved. Captured end screenshots.
- Two sequential current runs each, RX7600X11,1080pHighlocked,noadaptive,
  1200frames,last800,seed1,queriesoff. Actual1920x1080 verified from images
  and quality replies;all correct gameplay states anderrors0/missing0.
  Derby avg3.5290/3.5063,p953.9902/3.8496;Pugball3.0201/3.0569,
  p953.2309/3.4539;Bomberdude2.7153/2.7305,p953.3931/3.4362;
  Chromite2.5921/2.5903,p953.3069/3.2801;Spout2.1838/2.1716,
  p952.6297/2.6255ms. Raw maxima vary3.23–12.64ms.
- Inspected one screenshot/game:Derby six active cars/smoke;Pugball match;
  Bomberdude fourCPUfight/explosion;Chromite shots/enemies;Spout climbing
  through rain. Current gameplay fixtures,not titles. Derby dark but readable
  in this capture;other levels/multiplayer still independent coverage.
- Evidence out/other-examples-rx7600-current/results.json plus images/logs.
  No before/after speedup claim. All sampledp95 below4ms with Derby closest.

### 2026-10-21 — Derby fairground depth prepass
- Prior turn gave other-game baselines (progress),Derby closest to4ms.
  Inspected cars/shared geometry,incremental dent uploads,skid ranges: already
  optimized. Tested engine depth_prepass in arena1PostProcess for pileups.
- Saved native executable/pack out/derby-prepass-before. GuardedRX7600
  1080pHighlocked1200last800:prepass avg3.3215/3.3921,p953.7327/3.9704;
  baseline recheck avg3.4781/3.5133,p953.8361/3.8116. Earlier baseline
  3.5290/3.5063. Modest average gain,not p95/tail improvement guarantee.
  Draws206to312,triangles260766to503150:extra depth geometry saves fragment
  work here. Captured before/after native image pixel-identical (mean0,
  fractionanyRGBdelta>16=0). Kept onlyfairground arena1,not assumed forarena2.
- Native/cart builds pass. RomdevWebGPU bout1,600frames,state1errors0/missing0,
  inspected derby-prepass-wgpu.png:cars,fence,smoke and lighting render.
  Logs out/derby-prepass-{after,before-recheck},baseline saved separately.
  Worst after spike23.86ms remains;no attribution to outside load.

### 2026-10-21 — Derby dusk shadow readability
- Previous turn established fairground prepass win (progress). Current dusk
  capture hid car damage/crowd in dark areas. Arena1 existing hemisphere
  energy.42to.70,vignette.38to.26;no newlights orpasses. Warm floodlight
  colors,exposure and duskgrade retained.
- Matched native1200frame screenshot inspected:shadowed panels/ground lifted
  with dusk retained. AverageRGB33.67to37.66,centralregion47.65to51.02
  (supporting image measurement,not perceptual quality score).
- GuardedRX7600High1080p two runsavg3.3483/3.3796,p953.9541/4.0183,
  max8.0577/9.9772.312draws/503150triangles unchanged. No strict4ms pass
  claim;similar averages to priorprepass3.32–3.39. Raw logs/images at
  out/derby-dusk-readable. Native/cart buildspass. RomdevWebGPUbout1
  600frames,state1errors0/missing0,derby-dusk-readable-wgpu.png inspected.

### 2026-10-21 — Derby initialized skid draw range
- Previous turn improved dusk readability (progress). Found all2600skid
  quads submitted even while unused positions collapsed below floor. Use
  three.c geometry draw_count:0atcreation,skid_used*6after insertion,
  capped by existing2600 count. Once wrapped all slots remain drawable.
  Added world.skid_quads diagnostic. No reduction of persistent mark capacity.
- Native/cartbuildspass. Matched300frame nativeCPUfixture before/after:
  112usedquads,triangles833250to828274 (4976unused triangles skipped),
  same351draws,images exactly pixel-identical. No frame-time win claimed.
  Logs/images out/derby-skid-range,beforeexe+pack out/derby-skids-before.
- RomdevWebGPUbout1,2400frames:882marks,state1errors0/missing0;inspected
  wgpu-grown.png. Both empty/prefix growth paths use valid engine draw ranges;
  fullwrap not reached in this runtime sample,capacity behavior code-reviewed.

### 2026-10-21 — latest bubble splatter request verification
- Confirmed swept bubble impacts emit spray and persistent scene decals; rider
  and bicycle contacts use the shared skinned mesh decal receiver. Existing
  implementation already covers this request; no duplicate effect added.
- Fresh romdev WebGPU seed1 track15, ride river1 and brake360:30 bubbles,
  25 bubble impacts including1 rider/bike contact,45 total body contacts,
  294 persistent deposits.11 mesh decals/26 pieces generated. Inspected
  out/chocolate-bubble-latest.png: airborne bubbles and accumulated splats
  visible. Dark clothing stains remain less conspicuous at gameplay distance.

### 2026-10-21 — Spout rain/silk attribute updates
- Removed per-frame geometry version increments for rain and silk in
  games/spout/world.c. Position attribute versions still advance. Current
  three.c use_plain_geometry deletes/rebuilds VAOs on geometry version
  changes; these effects change positions only, not vertex layout or indices.
- Native/cart builds pass. Romdev GL rain820frames and active silk zip8frames
  inspected (spout-layout-rain.png,spout-layout-silk.png), errors0/missing0.
- Matched seed1 native830frame before/after runs with rain and active silk:
  screenshots pixel-identical;game.state2,errors0/missing0. Logs and images
  out/spout-layout-compare; saved original exe/pack out/spout-layout-before.
  This removes unnecessary VAO churn; no measured frame-time gain claimed.

### 2026-10-21 — Chupacabra painted wall sign distinction
- Checked remaining example geometry updates: no additional whole-layout
  version churn found. Reopened original ChupacabraTaproom.png for fidelity.
  Rectangular wall board in photo is rust/cream painted signage, while the
  round animal-head blade carries pink neon. Removed accidental HDR pink
  from rectangular text and changed its backing to existing brown material;
  circular logo/ring glow retained. No added geometry, lights, or textures.
- Native/cart builds pass. Romdev WebGPU seed1 track3,1180 pedalframes:
  inspected out/chupa-painted-wall.png; painted fascia readable and round
  pink neon prominent. Renderer errors0/missing0 checked during approach.
  Architecture still stylized; this is not full photo-fidelity signoff.

### 2026-10-21 — Chupacabra patio shade sails
- Replaced the two generic parasols with overlapping ochre/cream sagging
  triangular shade sails and four corner posts, following the supplied
  ChupacabraTaproom.png patio reference. Retained tables/railings. Sails
  remain below fascia and circular neon. Reused existing mesa_sail helper
  (128 triangles per two-sided sail) and existing static materials/batching;
  no cloth simulation, added lights, or new shader variant.
- Native/cart builds pass. Romdev WebGPU seed1 track3,1180 pedalframes:
  errors0/missing0. Inspected out/chupa-sails.png: curved sail edges and
  supports visible, wall lettering and circular logo unobstructed by fabric.
  No new performance measurement or full venue-fidelity claim.

### 2026-10-21 — Spout layout timing evidence
- Previous turn changed Chupacabra sails (progress). Measured saved pre-fix
  Spout and current executable sequentially on guarded RX7600/X11,High1080p,
  seed1,1200frames,last800; climbing rain fixture. All gameplay/error gates
  pass;103 draws/124158triangles unchanged.
- Initial before avg2.2447/2.2421ms,p952.7864/2.7790;after2.2836/2.2423,
  p953.7691/3.4041. Load rose2.25to5.27 during after samples. Rechecked
  both:before2.2170/2.2213,p953.0442/2.9984;after2.2160/2.2132,
  p953.0221/2.9809,max3.4805/4.0997. No meaningful measured speedup
  or sustained regression established. Keep correct attribute-only updates
  for avoiding known redundant VAO work, not as a claimed budget gain.
- Matched recheck screenshots pixel-identical. Raw data/screenshots in
  out/spout-layout-timing-{before,after,before-recheck,after-recheck}.
  Current samples are under4ms at p95 but not every frame. No further
  repetitions justified for this small change; focus future work elsewhere.

### 2026-10-21 — Moon wreck thermal blanket detail
- Gameplay audit atx256 confirms Apollo wreck and astronaut read clearly
  around62%course. Gold descent stage was a smooth octagonal drum. Added
  384 static faceted blanket triangles using existing gold material and
  merge_still. Irregular vertex spacing and shallow outward creases avoid
  the initial overly regular diamond pattern. No new lights/textures or
  animation; blanket sits outside original closed hull.
- Native/cart builds pass. Fresh romdev WebGPU seed1 track11,3450frames:
  errors0/missing0. Inspected out/moon-foil-final.png against
  moon-wreck-audit.png: irregular light-catching gold facets visible.
  Still a stylized Apollo-inspired wreck, not a historical replica. No
  frame-time gain claimed; geometry detail adds384triangles before passes.

### 2026-10-21 — ball-pit unchanged matrix uploads
- Publish pit instance batches only when rendered ball translations change;
  compare interpolated poses so final settling frames are retained. Physics,
  capacity432balls and multiplayer remain unchanged. Added diagnostic
  toys.pit_matrix_updates. Romdev WebGPU1200 idleframes:0writes; four-rider
  impact2080:432writes,errors0/missing0;toypit-upload-four.png inspected.
- Matched native2080frame before/after screenshots pixel-identical; identical
  state.hash2955366692,finished1,errors0/missing0. out/toypit-upload-compare.
  No timing benefit claimed. Native/cart builds pass.

### 2026-10-21 — user corrections: brick, streetlights, tent grounding
- Restored Pizza Mart warm red brick with pale mortar from gray-green masonry.
  Same texture dimensions, geometry and materials count.
- Replaced18m streetlight spacing with28 poles positioned in gaps derived
  from sorted venue centers. No lamp directly over Oro's sign anymore;
  removed glare at its fascia in inspected gameplay. Future layout edits
  feed lamp locations too. Fewer poles/lights, no brighter replacement lights.
- All8 bigtops now sample terrain around64-sector footprints. Wall skirts
  overlap local lowest edge height by.3m; eaves rise where needed to maintain
  1.5m clearance over high terrain. Center/edge/canopy poles and guy-rope
  pegs seat in terrain. Canopy heights follow raised eaves. Mesh count and
  roof subdivisions unchanged; no per-frame terrain work.
- Native/cart builds pass. Romdev WebGPU Neon4150frames inspected red brick
  and lamp in Pizza/Guadalupe gap (pizza-brick-lamps.png);Neon1000frames
  inspected clear Oro sign (oro-lamp-clear.png). Circus1450 and1810frames
  inspected main red tent skirt and blue tent at loop (circus-grounded.png,
  circus-grounded-blue.png). Renderer errors0/missing0 at checked scenes.

### 2026-10-21 — distant split-screen city views
- Added bench-neon.py --spread-players (requires multiplayer):onlyP1 pedals,
  other pads remain still. End diagnostics include race.x for everyMP run.
  This exercises separated streaming sources instead of overlapping views.
- Romdev WebGPU two players,Neon3000frames:P1x563.14/P2x2.00,errors0/missing0.
  Inspected neon-divergent-views.png and neon-divergent-distant.png:both
  starting storefronts and distant city remain rendered with players561m apart.
- Guarded RX7600 native1080pHigh,3000frames,last1000,two runs:
  avg3.1261/2.4276ms,p954.8300/3.7156,max14.7060/16.6624;
  renderCPU2.5316/2.0135ms,336draws/727964triangles. Both end at identical
  race.x[562.18866,2.00234],correctplayercount,errors0/missing0. Sampledload
  5.57–5.80; variability not attributed to any particular background process.
  Results out/neon-spread-rx7600. Not a strict4ms pass; distant-view renderCPU
  and tails remain candidates for investigation. No additional runs needed
  merely to obtain a favorable sample.

### 2026-10-21 — distant-view render profiling / prepass experiment
- Previous turn added separated-view workload (progress). Native CPU-only
  profiler trace2800–2999 (GPUtimersoff):depth_prepass1.6008ms exclusive,
  scene.9502,froxel.bin.3293,lighting.pass.2120,visibility.cull.1738.
  Profile wall-zone durations include waits and instrumentation; not a
  replacement for uninstrumented frame-time measurement. Trace/log saved
  out/neon-spread-profile;correct separated race.x and errors0/missing0.
- Tested disabling Neon prepass:draws336to174,triangles727964to407089.
  GuardedRX7600High1080p two-player spread3000/last1000:offavg3.4600/3.3632,
  p956.7428/4.7046;onrecheck3.7152/3.8399,p955.3262/5.8496.
  Unlike earlier lower-load onruns2.43–3.13, sampledload rose10.2–17.7
  duringoff and13.4–14.8 duringon. Results inconclusive under changingload;
  no claim that all variability comes from external activity. Existing
  depth_prepass=true restored;native artifact rebuilt. Cart remainedon.
- Data out/neon-spread-no-prepass and neon-spread-prepass-recheck; saved
  baselineexe+pack out/neon-prepass-on. Avoid further noisy repetitions.
  Next investigation can isolate pass submission work or revisit controlled
  comparisons when load stabilizes; no validated optimization from this test.

### 2026-10-21 — shared froxel empty-copy guard
- Inspected clustered binning after prior profiling. compact_slices called
  variable-size memcpy for every cell, including zero-light cells. Guard
  copies with cnt>0; records and compaction offsets remain identical. Also
  avoids constructing an items pointer for an empty grid with no allocation.
  Applied rootengine and actual pinned buildengine src/render/froxel.c.
- Native unit_froxel:48984checks pass,0fails,checksum2951283967 at default,
  CW_WORKERS0 and3. Existing suite covers coverage,cones,overflow and empty
  lists. Native/cart all6primary examples rebuilt successfully.
- Romdev WebGPU Neon2Pspread3000frames:race.x562.90/2.00,errors0/missing0;
  inspected out/neon-empty-copies.png. No light counts or geometry reduced.
  No measured frame-time speedup claim; defer timing under unstable load.

### 2026-10-21 — fresh/cleared empty froxel regression
- Added test_empty to rootengine and actual pinned tests/unit/test_froxel.c.
  Exercises NULL input/count0 on a fresh grid with no item allocation, then
  after populating light lists. Requires nonzero cell count,zero item count,
  every packed record zero,zero references/overflow/drop statistics.
- Native unit_froxel passes48997checks,0fails atCW_WORKERS0,3,23;existing
  checksum2951283967 preserved. Logs /tmp/froxel-empty-regression-{0,3,23}.log.
- Attempted cart target in examplesbuild:unit_froxel is not configured there
  (unknown target), so no cross-host claim for newtest. Existing game cart
  runtime already checked previous turn. No game/artifact behavior changed.

### 2026-10-21 — smooth and seated translucent gumdrops
- Gumdrop mesh reused flat circus-quad normals, causing visibly faceted
  highlights. Replaced normals with normalized profile derivatives and a
  vertical crown normal. Same32x6quads/384triangles; existing transmission,
  roughness,absorption and four materials retained. No new shader/texture.
- Seat each of12gumdrops at the lowest of16footprint terrain samples minus
  .08m, preventing wide bases from hanging above the slope. Sugar crystals
  retain relative placement. All placement calculations happen at build time.
- Native/cart builds pass. Romdev WebGPU Candyseed1,1620frames,errors0/missing0;
  inspected out/gumdrops-smooth.png: green gumdrop beside large gummy bear
  has continuous highlights instead of polygon bands; base enters the bank.
  Existing gummy shape/material retained. No performance improvement claim.

### 2026-10-21 — discovery scene gameplay timing guard
- Previous response rechecked existing correction screenshots; it added no new
  implementation. Resumed confirmed shell session13635 and collected its
  completed benchmark results instead of restarting it.
- Added --require-gameplay to bench-neon.py: requires game.state3/finished0.
  Optional so deliberate Toytown finish/ballpit measurements remain possible.
  Exercised actual guard against four saved gameplay rows (accepted) and four
  older finished rows (rejected). End screenshots now saved with each run.
- RX7600/X11 High1080p,1800frames,last1200,two runs each:
  Circus avg1.4884/1.5340ms,p951.7676/2.0172,max6.2941/6.5340;
  Powder avg1.5648/1.5790,p951.9771/2.0924,max8.7549/7.7640.
  All game.state3/finished0/errors0/missing0. Inspected both first-run PNGs;
  circus loop/tent and snowy track rendered. Load12.25–12.77; no attribution
  of outliers to particular apps. Evidence out/discovery-budget-live-track02
  and track14. Earlier2400frame runs included finish sequence and are not
  pure gameplay comparisons.
- Other completed2400frame/last1700 windows stayed on course:
  Temple avg1.5019/1.5163,p951.6344/1.7476,max6.5327/4.9274;
  Mars avg1.6005/2.0553,p951.9096/3.8267,max13.1921/13.1876.
  Evidence out/discovery-budget-track04/13; these are partial-course solo
  windows, not whole-course/multiplayer or strict4ms passes. High systemload
  and run variation prevent claiming an optimization from these baselines.

### 2026-10-21 — Level One window surrounds / facade reference check
- Previous turn progressed benchmark validation. This turn reopened original
  LevelOne.png and PhantomFox.png, then captured current WebGPU Neon at1630
  and3000frames. Phantom retains widened frontage, one wordmark, Goat&Ram
  patio sign and shade sails; no sign scaling changed. Capture is from an
  elevated riding position, so it does not prove every ground-level detail.
- Level One silhouette/windows match the tall reference but lacked pale
  window surrounds and lintels plus circular pipe caps. Added four static
  boxes and two existing-cylinder instances using existing materials. These
  inherit the existing height/width transforms and city static batching.
  No new textures, lights or update logic.
- Native/cart builds pass. Repeated romdev WebGPU seed1/track3/3000frames,
  errors0/missing0. Viewed out/levelone-trim.png versus
  levelone-fidelity-audit.png: pale surrounds/lintels and left pipe cap read.
  Upper sign partly overlaps HUD at this particular camera position; full
  downtown fidelity remains incomplete. Phantom evidence:
  out/phantom-fidelity-audit.png. No frame-time win claimed for art detail.

### 2026-10-21 — examples release preparation
- User requested a new games release and noted GitHub is far behind. The
  examples main/tag remain v0.9.0; current game work is on local branch
  release-prep-v0.10.0 pending cartwheel-engine v0.14.0 SDK.
- Current examples scripts/deps.lock pins engine SDK v0.13.0. An actual
  RELEASE=1 TARGETS=cart-wgpu six-game build failed at BikeMesa main.c:
  cartwheel/quality_menu.h absent from the old SDK. This dependency is real;
  do not tag examples against v0.13.0.
- Separate agent cliemu-c0 is preparing three.c v0.186.18 and engine v0.14.0
  from clean dependencies. Do not edit those main checkouts; await engine tag
  and SDK asset SHA-256 values. Then update examples scripts/deps.lock,
  fetch/verify new SDKs, build all six release carts and platform artifacts,
  run release checks, and publish examples tag after green CI.
- Local build/whimsy-native and build/whimsy-cart rebuild six games; romdev
  WebGPU Neon400frames after reverting incomplete skyline experiment reports
  errors0/missing0. All six asset roots have zero missing files. Save
  migration versions3–8 passed. Staged source diff has no whitespace errors.
- Reference research archive HTML/WRL and generated pycache excluded from
  release prep commit; keep them local. Original/reconstructed runtime art,
  needed rover models, source credits, and detailed WORKLIST are staged.
