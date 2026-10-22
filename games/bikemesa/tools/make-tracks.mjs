// Generates rider's tracks: the ground lines the bike rides (games/bikemesa/tracks.inc,
// sampled every 0.25 m) and one scene per track holding its sky, sun, fog and
// post settings (assets/scenes/trackNN.cwscene). Tracks are written as a list
// of shaped pieces below; edit them here and rerun:
//
//   node games/bikemesa/tools/make-tracks.mjs
//
// Pieces (x grows to the right, y up, metres):
//   ['flat', len]                 level ground
//   ['slope', len, dy]            a smooth rise or fall (cosine)
//   ['kicker', len, h]            a ramp that curls up to a lip h higher (a jump's take-off)
//   ['gap', len, dy]              a jump's tabletop: a plank deck from the lip that falls dy over len
//                                 (a short jump lands on it, a full one clears it to the landing)
//   ['pit', len, dy]              open air: the ground resumes len further on, dy lower or higher
//   ['landing', len, drop]        a downslope that catches a jump, drop lower at its foot
//   ['rollers', n, amp, wl]       n smooth bumps, amp high, wl long
//   ['bump', len, h]              one smooth hump
//   ['dip', len, h]               one smooth hollow
//   ['quarter', len, h]           a ramp that curls up toward vertical (launch straight up)
//   ['bridge', len, dy]           open air spanned by a plank bridge that collapses behind the rider
//   ['ice', len]                  an icy patch (less grip) over the next len metres
//   ['crater', len, depth]        a bowl and its rim: a natural kicker
//   ['drawbridge', len]           a hinged deck over a pit that rises and falls (a ramp when up)
//   ['seesaw', len]               a see-saw plank on a pivot over flat ground
//   ['wreck', len]                flat ground under a swinging wrecking ball (it only reaches the air)
//   ['logs', len, dy]             a downhill slope that logs roll down
//   ['train', len, dy]            a jump's tabletop with a train crossing under it
//   ['split', len, h, dy, at, end] two routes after a kicker: a high plank deck from h above the lip at at m on, sloping
//                                 down to end above it (a hopped jump//                                 reaches it; faster, a bonus) over a low line falling dy; they rejoin
//   ['loop', r, powered=false]    a vertical loop of radius r; powered adds a pedal-driven boost strip
//   ['corkscrew', len, radius, direction] a full roll around the track's forward axis; direction is +1 or -1
//   ['check']                     a checkpoint here
//   ['boost', len]                a boost strip along the next len metres
//   ['pinch', len, lo, hi]        only lanes lo..hi open along the next len metres (a barrier closes the others)
// a track's ramps: [x0, up, h, top, down, lo, hi] each, a ramp on lanes lo..hi only: up over
//   `up` m to h above the track, flat for `top`, down over `down`; the other lanes ride past on the track
//   ['finish']                    the finish line here
import { writeFileSync, mkdirSync } from 'node:fs';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';

const HERE = dirname(fileURLToPath(import.meta.url));
const GAME = join(HERE, '..');
const STEP = 0.25;

// ── themes: the look of each track ──────────────────────────────────────
// sky: time of day (hours), latitude, sun energy, sky energy, mie haze; fog
// density; ground colour under the horizon; tone map and exposure; the grade
// (assets/grades/<name>.cube, tools/make-grade.mjs)
export const THEMES = {
  canyon: { tod: 16.9, lat: 34, north: 75, sun: 6.2, sky: 0.95, mie: 1.1, fog: 0.0035, fogh: 0.04, ground: [0.32, 0.17, 0.08],
    amb: [1, 0.8, 0.62], hemi: 0.3, tonemap: 'AGX', exposure: 1.0, grade: 'canyon' },
  alpine: { tod: 7.4, lat: 46, north: 100, sun: 5.5, sky: 1.0, mie: 0.8, fog: 0.004, fogh: 0.03, ground: [0.6, 0.65, 0.72],
    amb: [0.75, 0.82, 1], hemi: 0.35, tonemap: 'AGX', exposure: 1.0, grade: 'alpine' },
  neon: { tod: 19.9, lat: 35, north: 60, sun: 1.2, sky: 0.8, mie: 0.6, fog: 0.004, fogh: 0.06, ground: [0.08, 0.06, 0.12],
    amb: [0.58, 0.7, 1], hemi: 0.55, tonemap: 'AGX', exposure: 1.65, grade: 'neon' },
  jungle: { tod: 10.5, lat: 8, north: 40, sun: 5.8, sky: 1.0, mie: 1.4, fog: 0.006, fogh: 0.08, ground: [0.12, 0.2, 0.06],
    amb: [0.85, 1, 0.75], hemi: 0.35, tonemap: 'AGX', exposure: 0.95, grade: 'jungle' },
  desert: { tod: 13.2, lat: 26, north: 90, sun: 7.0, sky: 1.0, mie: 1.0, fog: 0.0025, fogh: 0.06, ground: [0.62, 0.45, 0.25],
    amb: [1, 0.9, 0.75], hemi: 0.3, tonemap: 'AGX', exposure: 0.9, grade: 'desert' },
  autumn: { tod: 15.8, lat: 48, north: 70, sun: 5.6, sky: 0.9, mie: 1.2, fog: 0.005, fogh: 0.08, ground: [0.35, 0.2, 0.08],
    amb: [1, 0.82, 0.6], hemi: 0.32, tonemap: 'AGX', exposure: 1.0, grade: 'autumn' },
  coast: { tod: 12.0, lat: 52, north: 80, sun: 2.4, sky: 0.7, mie: 2.6, fog: 0.009, fogh: 0.04, ground: [0.2, 0.24, 0.26],
    amb: [0.72, 0.8, 0.9], hemi: 0.45, tonemap: 'AGX', exposure: 1.1, grade: 'coast' },
  cosmic: { tod: 19.2, lat: 20, north: 30, sun: 2.6, sky: 0.9, mie: 0.5, fog: 0.002, fogh: 0.02, ground: [0.1, 0.06, 0.16],
    amb: [0.7, 0.55, 1], hemi: 0.9, tonemap: 'AGX', exposure: 1.5, grade: 'cosmic' },
  scary: { tod: 20.4, lat: 50, north: 60, sun: 2.1, sky: 0.6, mie: 1.2, fog: 0.01, fogh: 0.1, ground: [0.1, 0.09, 0.11],
    amb: [0.78, 0.68, 0.82], hemi: 1.65, tonemap: 'AGX', exposure: 2.4, grade: 'scary' },
  xmas: { tod: 17.2, lat: 55, north: 80, sun: 3.0, sky: 0.85, mie: 1.0, fog: 0.005, fogh: 0.06, ground: [0.7, 0.72, 0.8],
    amb: [0.8, 0.85, 1], hemi: 0.5, tonemap: 'AGX', exposure: 1.2, grade: 'xmas' },
  moon: { tod: 12.5, lat: 30, north: 70, sun: 7.5, sky: 0.02, mie: 0.05, fog: 0.0, fogh: 0.02, ground: [0.02, 0.02, 0.03],
    amb: [0.6, 0.62, 0.7], hemi: 0.12, tonemap: 'AGX', exposure: 1.0, grade: 'moon' },
  beach: { tod: 11.5, lat: 25, north: 85, sun: 7.0, sky: 1.0, mie: 0.8, fog: 0.002, fogh: 0.06, ground: [0.6, 0.55, 0.4],
    amb: [1, 0.95, 0.85], hemi: 0.35, tonemap: 'AGX', exposure: 0.95, grade: 'beach' },
  mars: { tod: 14.5, lat: 20, north: 75, sun: 5.0, sky: 1.1, mie: 3.8, fog: 0.007, fogh: 0.035, ground: [0.55, 0.3, 0.16],
    amb: [1, 0.75, 0.55], hemi: 0.4, tonemap: 'AGX', exposure: 1.0, grade: 'mars' },
  circus: { tod: 16.2, lat: 30, north: 75, sun: 5.0, sky: 1.0, mie: 0.8, fog: 0.0025, fogh: 0.06, ground: [0.35, 0.16, 0.25],
    amb: [1, 0.85, 0.75], hemi: 0.45, tonemap: 'AGX', exposure: 1.05, grade: 'beach' },
  stuntshow: { tod: 14.5, lat: 35, north: 70, sun: 6.4, sky: 1.0, mie: 0.65, fog: 0.002, fogh: 0.04, ground: [0.3, 0.32, 0.38],
    amb: [1, 0.92, 0.82], hemi: 0.38, tonemap: 'AGX', exposure: 1.0, grade: 'beach' },
  candy: { tod: 10.8, lat: 25, north: 85, sun: 5.2, sky: 1.0, mie: 0.7, fog: 0.003, fogh: 0.05, ground: [0.6, 0.35, 0.5],
    amb: [1, 0.85, 0.95], hemi: 0.5, tonemap: 'AGX', exposure: 1.0, grade: 'beach' },
  toybox: { tod: 15.5, lat: 35, north: 70, sun: 5.4, sky: 0.95, mie: 0.8, fog: 0.003, fogh: 0.06, ground: [0.3, 0.35, 0.5],
    amb: [0.85, 0.9, 1], hemi: 0.45, tonemap: 'AGX', exposure: 1.0, grade: 'canyon' },

};

// ── the tracks ──────────────────────────────────────────────────────────
export const TRACKS = [
  { name: 'Red Rock Run', theme: 'canyon', par: 30, ready: true, ramps: [[32.3, 2.4, 0.6, 1.2, 2.2, 2, 3], [83.5, 2, 0.5, 1.5, 2, 0, 0], [132, 3, 0.9, 0, 3, 2, 3], [265.5, 2.5, 0.8, 1, 2.5, 3, 3], [266, 2.5, 0.7, 1, 2.5, 0, 0]], pieces: [
    ['flat', 14], ['slope', 18, -4], ['flat', 6], ['rollers', 3, 0.7, 6], ['flat', 4],
    ['kicker', 6, 1.4], ['gap', 7, -1.2], ['landing', 10, 2.2], ['flat', 8], ['check'],
    ['flat', 8], ['boost', 8], ['flat', 12], ['loop', 3.5], ['flat', 8],
    ['slope', 12, 3.5], ['flat', 4], ['bump', 8, 1.2], ['slope', 16, -5], ['boost', 10], ['flat', 12],
    ['kicker', 7, 1.8], ['gap', 9, -2.5], ['landing', 14, 3.5], ['flat', 6], ['check'],
    ['rollers', 4, 0.9, 7], ['pinch', 40, 1, 3], ['dip', 14, 2.0], ['flat', 6], ['kicker', 5, 1.2], ['gap', 5, -0.5], ['landing', 8, 1.5],
    ['slope', 20, -6], ['flat', 10], ['kicker', 7, 2.2], ['gap', 6, -1.0], ['landing', 10, 3], ['flat', 8], ['check'],
    ['slope', 14, -4], ['boost', 12], ['flat', 14], ['kicker', 9, 2.6], ['gap', 18, -3.5], ['landing', 16, 4],
    ['flat', 8], ['finish'], ['flat', 60],
  ] },
  { name: 'Powder Line', theme: 'alpine', par: 30, ready: true, ramps: [[90.5, 2, 0.6, 1, 2, 2, 3], [210.3, 2, 0.6, 1, 2, 0, 0]], pieces: [
    ['flat', 10], ['slope', 24, -7], ['rollers', 4, 0.8, 6], ['kicker', 6, 1.5], ['gap', 8, -2], ['landing', 12, 3],
    ['flat', 6], ['check'], ['slope', 10, 2.5], ['bump', 10, 1.4], ['slope', 22, -8], ['boost', 10], ['flat', 10],
    ['kicker', 8, 2.2], ['split', 40, 6.2, -4, 18], ['landing', 14, 3], ['flat', 6], ['check'],
    ['dip', 16, 2.5], ['rollers', 5, 0.7, 5], ['slope', 18, -6], ['kicker', 6, 1.6], ['gap', 6, -2.5], ['landing', 12, 3],
    // The late ski jump clears an actual missing section of track. The long
    // boosted approach makes it possible without a precisely timed hop.
    ['flat', 6], ['check'], ['slope', 26, -9], ['boost', 14], ['flat', 10], ['kicker', 12, 4.2], ['pit', 24, -7],
    ['landing', 20, 5], ['flat', 8], ['finish'], ['flat', 60],
  ] },
  { name: 'Downtown', theme: 'neon', par: 70, ready: true, pieces: [
    ['flat', 16], ['kicker', 5, 1.0], ['gap', 5, -0.8], ['flat', 4], ['wreck', 12], ['flat', 4], ['kicker', 5, 1.2], ['gap', 6, -1.5], ['flat', 12],
    ['check'], ['slope', 10, -3], ['boost', 10], ['flat', 10], ['kicker', 7, 2.0], ['gap', 12, -3], ['flat', 10],
    ['bump', 6, 0.8], ['flat', 6], ['kicker', 4, 0.9], ['gap', 4, 0.6], ['flat', 10], ['check'],
    ['slope', 8, 2.5], ['flat', 6], ['kicker', 6, 1.6], ['gap', 9, -4], ['landing', 12, 2.5], ['flat', 8],
    ['rollers', 3, 0.6, 5], ['check'], ['slope', 16, -5], ['boost', 12], ['flat', 10], ['kicker', 9, 2.8], ['gap', 18, -4.5],
    ['landing', 14, 3], ['flat', 10], ['check'],
    ['flat', 18], ['bump', 12, 0.8], ['flat', 12], ['kicker', 7, 1.5],
    ['gap', 10, -1.5], ['landing', 12, 2], ['rollers', 2, 0.55, 6], ['flat', 13],
    // East Valley Paranormal and Against the Grain are neighboring venues
    // before the Nile. The extra block keeps their frontages full size.
    ['flat', 16], ['bump', 10, .8], ['flat', 19], ['check'], ['flat', 15],
    // Four 86 m blocks leave room for full-scale landmarks and riding gaps.
    ['check'], ['flat', 18], ['bump', 12, 0.8], ['kicker', 7, 1.5],
    ['gap', 10, -1.5], ['landing', 12, 2], ['rollers', 2, 0.55, 6], ['flat', 9],
    ['check'], ['flat', 18], ['bump', 12, 0.8], ['kicker', 7, 1.5],
    ['gap', 10, -1.5], ['landing', 12, 2], ['rollers', 2, 0.55, 6], ['flat', 9],
    ['check'], ['flat', 18], ['bump', 12, 0.8], ['kicker', 7, 1.5],
    ['gap', 10, -1.5], ['landing', 12, 2], ['rollers', 2, 0.55, 6], ['flat', 9],
    ['check'], ['flat', 18], ['bump', 12, 0.8], ['kicker', 7, 1.5],
    ['gap', 10, -1.5], ['landing', 12, 2], ['rollers', 2, 0.55, 6], ['flat', 9],
    // Two shorter riding blocks accommodate the final district.
    ['check'], ['flat', 14], ['bump', 10, .7], ['kicker', 6, 1.3],
    ['gap', 8, -1.3], ['landing', 10, 1.8], ['rollers', 2, .45, 5], ['flat', 8],
    ['check'], ['flat', 14], ['bump', 10, .7], ['kicker', 6, 1.3],
    ['gap', 8, -1.3], ['landing', 10, 1.8], ['rollers', 2, .45, 5], ['flat', 8],
    ['finish'], ['flat', 60],
  ] },
  { name: 'Enchanted Forest', theme: 'jungle', par: 38, ready: true, pieces: [
    ['flat', 12], ['rollers', 4, 0.9, 6], ['slope', 14, -4], ['kicker', 6, 1.6], ['gap', 8, -1.5], ['landing', 12, 3],
    ['check'], ['slope', 14, 4], ['flat', 4], ['dip', 18, 3.5], ['slope', 10, -2], ['kicker', 7, 2.2], ['gap', 10, -2],
    ['landing', 12, 3.5], ['flat', 6], ['check'], ['rollers', 6, 1.0, 6.5], ['slope', 24, -8],
    ['flat', 8], ['kicker', 8, 2.2], ['gap', 15, -3.5], ['landing', 22, 4.5], ['flat', 8], ['check'],
    ['flat', 6], ['boost', 8], ['flat', 26], ['flat', 13], ['flat', 10], ['bump', 10, 2.0],
    ['slope', 12, -4], ['kicker', 6, 1.4], ['gap', 6, 0.5], ['flat', 6], ['slope', 18, -6], ['kicker', 9, 2.8],
    ['gap', 17, -4], ['landing', 16, 4.5], ['flat', 8], ['finish'], ['flat', 60],
  ] },
  { name: 'Dune Sea', theme: 'desert', par: 30, ready: true, pieces: [
    ['flat', 10], ['rollers', 3, 1.4, 12], ['slope', 16, -4], ['kicker', 7, 1.8], ['gap', 10, -2], ['landing', 14, 3.5],
    ['check'], ['rollers', 4, 1.6, 11], ['slope', 20, -5], ['boost', 12], ['kicker', 9, 2.6], ['split', 44, 7.2, -4.5, 20, 2.5],
    ['landing', 16, 3.5], ['check'], ['dip', 20, 4], ['rollers', 3, 1.2, 10], ['kicker', 7, 2.0], ['gap', 11, -2.5],
    ['landing', 14, 3.5], ['check'], ['slope', 28, -8], ['boost', 14], ['flat', 8], ['kicker', 11, 3.4], ['gap', 24, -6],
    ['landing', 20, 6], ['flat', 10], ['finish'], ['flat', 60],
  ] },
  { name: 'Leaf Fall', theme: 'autumn', par: 34, ready: true, pieces: [
    ['flat', 12], ['slope', 16, -5], ['rollers', 5, 0.8, 5.5], ['kicker', 6, 1.6], ['gap', 8, -1.8], ['landing', 12, 3],
    ['check'], ['slope', 12, 3.5], ['bump', 8, 1.5], ['kicker', 5, 1.2], ['gap', 6, 0.4], ['flat', 6], ['slope', 20, -7],
    ['kicker', 8, 2.4], ['gap', 14, -3.5], ['landing', 16, 4.5], ['check'], ['dip', 14, 3], ['rollers', 4, 1.0, 6],
    ['slope', 10, -3], ['kicker', 6, 1.8], ['gap', 9, -3], ['landing', 12, 3], ['flat', 6], ['check'],
    ['logs', 22, -8], ['boost', 12], ['flat', 8], ['kicker', 10, 3.0], ['gap', 20, -5], ['landing', 18, 5], ['flat', 6],
    ['kicker', 6, 1.6], ['gap', 8, -2], ['landing', 12, 3], ['flat', 8], ['finish'], ['flat', 60],
  ] },
  { name: 'Storm Cliffs', theme: 'coast', par: 32, ready: true, ramps: [[60.5, 2, 0.6, 1, 2, 1, 2], [204.2, 2, 0.7, 0.5, 2.5, 0, 1]], pieces: [
    ['flat', 12], ['slope', 14, -4], ['kicker', 6, 1.8], ['train', 10, -3], ['landing', 12, 3], ['flat', 6], ['check'],
    ['rollers', 5, 1.1, 6], ['slope', 16, 4], ['flat', 4], ['kicker', 5, 1.4], ['gap', 8, -6], ['landing', 14, 3.5],
    ['check'], ['slope', 18, -6], ['boost', 10], ['kicker', 9, 2.8], ['gap', 18, -4], ['landing', 16, 4.5], ['flat', 6],
    ['check'], ['dip', 16, 3.5], ['bump', 10, 2.2], ['slope', 14, -5], ['kicker', 7, 2.2], ['gap', 12, -4],
    ['landing', 14, 3.5], ['check'], ['slope', 24, -9], ['boost', 14], ['flat', 8], ['kicker', 12, 3.6], ['gap', 26, -7],
    ['landing', 20, 6], ['flat', 10], ['finish'], ['flat', 60],
  ] },
  { name: 'Star Road', theme: 'cosmic', par: 30, ready: true, ramps: [[28.5, 2.5, 0.9, 0, 3, 2, 3], [161.5, 2, 0.6, 1.5, 2, 0, 3]], pieces: [
    ['flat', 12], ['kicker', 6, 1.6], ['gap', 10, -1], ['flat', 8], ['rollers', 4, 1.2, 7], ['check'],
    ['slope', 20, -6], ['boost', 12], ['kicker', 9, 3.0], ['gap', 22, -3], ['landing', 16, 4], ['check'],
    ['bump', 12, 1.4], ['kicker', 6, 1.6], ['gap', 6, -0.5], ['flat', 6], ['slope', 16, -5],
    ['kicker', 8, 2.6], ['gap', 13, -4], ['landing', 16, 4.5], ['check'], ['dip', 18, 4], ['rollers', 3, 1.4, 8],
    ['slope', 26, -9], ['boost', 16], ['flat', 8], ['kicker', 12, 4.0], ['gap', 32, -8], ['landing', 22, 7],
    ['flat', 10], ['finish'], ['flat', 60],
  ] },
  { name: 'Scary Town', theme: 'scary', par: 36, ready: true, music: 'music_scary', pieces: [
    ['flat', 12], ['slope', 14, -4], ['rollers', 3, 0.6, 7], ['kicker', 6, 1.4], ['gap', 8, -1.5], ['landing', 12, 2.5],
    ['flat', 8], ['bridge', 12, -0.6], ['flat', 8], ['check'], ['slope', 16, -5], ['kicker', 7, 1.8], ['gap', 10, -2],
    ['landing', 14, 3], ['flat', 6], ['bump', 10, 1.4], ['flat', 6], ['pinch', 18, 1, 2], ['bridge', 16, -1.2], ['flat', 8], ['check'],
    ['dip', 14, 2.5], ['slope', 18, -6], ['boost', 10], ['flat', 10], ['kicker', 8, 2.2], ['gap', 14, -3], ['landing', 18, 4],
    ['flat', 8], ['check'], ['flat', 6], ['boost', 12], ['flat', 30], ['loop', 5.6], ['flat', 10],
    ['bridge', 14, -0.8], ['flat', 6], ['slope', 16, -5], ['kicker', 9, 2.4], ['gap', 16, -3.5],
    ['landing', 18, 4.5], ['flat', 8], ['finish'], ['flat', 60],
  ] },
  { name: 'Christmas Town', theme: 'xmas', par: 25, ready: true, music: 'music_xmas', grip: 1, pieces: [
    ['flat', 12], ['slope', 16, -5], ['ice', 10], ['flat', 10], ['kicker', 6, 1.5], ['gap', 8, -1.5], ['landing', 14, 3],
    ['flat', 6], ['check'], ['rollers', 3, 0.6, 7], ['slope', 14, -4], ['ice', 12], ['flat', 12], ['kicker', 7, 2.0],
    ['gap', 11, -2.5], ['landing', 16, 3.5], ['flat', 6], ['check'], ['dip', 14, 2.5], ['slope', 18, -6], ['boost', 10],
    ['flat', 10], ['kicker', 8, 2.3], ['gap', 14, -3], ['landing', 18, 4], ['ice', 10], ['flat', 12], ['check'],
    ['slope', 16, -5], ['kicker', 9, 2.4], ['gap', 16, -3.5], ['landing', 18, 4.5], ['flat', 8], ['finish'], ['flat', 60],
  ] },
  { name: 'Moon Base', theme: 'moon', par: 72, ready: true, music: 'music_moon', gravity: 1.62, drag: 0.15, pieces: [
    ['flat', 16], ['seesaw', 10], ['flat', 8], ['crater', 14, 1.6], ['flat', 10], ['crater', 18, 2.2], ['flat', 12], ['check'], ['slope', 18, -4],
    ['kicker', 7, 1.6], ['gap', 18, -2], ['landing', 20, 3], ['flat', 10], ['crater', 16, 1.8], ['flat', 10], ['check'],
    ['rollers', 3, 1.2, 12], ['crater', 20, 2.6], ['flat', 12], ['kicker', 8, 2.0], ['gap', 24, -3], ['landing', 24, 4],
    ['flat', 12], ['check'], ['crater', 16, 2.0], ['flat', 10], ['kicker', 9, 2.4], ['gap', 26, -4], ['landing', 26, 5],
    ['flat', 10], ['finish'], ['flat', 60],
  ] },
  { name: 'Pier Pressure', theme: 'beach', par: 31, ready: true, music: 'music_beach', drag: 1.5, pieces: [
    ['flat', 12], ['rollers', 3, 1.0, 10], ['slope', 14, -4], ['kicker', 6, 1.5], ['gap', 9, -1.5], ['landing', 14, 3],
    ['flat', 6], ['check'], ['rollers', 4, 1.1, 10], ['slope', 16, -5], ['boost', 10], ['flat', 10], ['kicker', 8, 2.2],
    ['gap', 16, -2.5], ['landing', 16, 3.5], ['flat', 6], ['check'], ['dip', 16, 2.5], ['slope', 18, -6], ['flat', 10],
    ['drawbridge', 10], ['flat', 10], ['check'], ['slope', 20, -6], ['boost', 12],
    ['flat', 8], ['kicker', 10, 2.6], ['gap', 20, -4], ['landing', 20, 5], ['flat', 8], ['finish'], ['flat', 60],
  ] },
  { name: 'Red Planet', theme: 'mars', par: 51, ready: true, music: 'music_mars', gravity: 3.7, drag: 0.5, pieces: [
    ['flat', 14], ['slope', 16, -4], ['rollers', 3, 0.9, 9], ['kicker', 7, 1.8], ['gap', 14, -2], ['landing', 16, 3],
    ['flat', 8], ['check'], ['crater', 16, 2.0], ['flat', 10], ['slope', 18, -5], ['kicker', 8, 2.2], ['gap', 18, -3],
    ['landing', 20, 4], ['flat', 8], ['check'], ['rollers', 4, 1.0, 10], ['dip', 16, 3], ['slope', 18, -6], ['boost', 10],
    ['flat', 10], ['kicker', 9, 2.6], ['slope', 48, -4.5], ['landing', 22, 4], ['flat', 10], ['check'], ['crater', 18, 2.4],
    ['flat', 10], ['kicker', 9, 2.4], ['gap', 20, -4], ['landing', 20, 5], ['flat', 8], ['finish'], ['flat', 60],
  ] },
  // A three-act circus: trampoline rollers, juggling bumps, then a boosted finale.
  { name: 'Big Top Tumble', theme: 'circus', par: 40, ready: true, music: 'music_circus', pieces: [
    ['flat', 14], ['slope', 18, -4], ['rollers', 5, 0.65, 7], ['flat', 6], ['check'],
    ['kicker', 7, 1.6], ['gap', 10, -2], ['landing', 14, 3], ['flat', 10], ['rollers', 2, 0.4, 6], ['flat', 6], ['check'],
    ['slope', 22, -5], ['rollers', 3, 0.8, 9], ['flat', 8], ['kicker', 8, 2.1], ['gap', 14, -2.5], ['landing', 18, 4],
    ['flat', 10], ['check'], ['slope', 18, -5], ['boost', 24], ['flat', 12], ['loop', 12, true], ['flat', 14], ['check'],
    ['boost', 12], ['flat', 16], ['kicker', 9, 2.6], ['gap', 20, -4],
    ['landing', 22, 5], ['flat', 12], ['finish'], ['flat', 60],
  ] },
  // Gentle low gravity makes the candy hills a playground for long, floaty tricks.
  { name: 'Candy Comet', theme: 'candy', par: 40, ready: true, gravity: 6.5, drag: 0.8, pieces: [
    ['flat', 14], ['slope', 18, -4], ['rollers', 4, 0.8, 10], ['flat', 8], ['check'],
    ['kicker', 8, 1.8], ['gap', 18, -2.5], ['landing', 22, 4], ['flat', 10], ['check'],
    ['dip', 20, 2], ['rollers', 4, 1, 12], ['slope', 20, -5], ['flat', 10], ['kicker', 9, 2.2],
    ['gap', 22, -3], ['landing', 24, 4], ['flat', 10], ['check'], ['slope', 20, -5], ['boost', 10],
    ['flat', 12], ['kicker', 10, 2.5], ['gap', 26, -4], ['landing', 26, 5], ['flat', 12], ['finish'], ['flat', 60],
  ] },
  // A toy railway, rollers and staggered lane ramps in a giant construction set.
  { name: 'Clockwork Toybox', theme: 'toybox', par: 34, ready: true, ramps: [[36, 3, 0.6, 2, 3, 2, 3]], pieces: [
    ['flat', 14], ['slope', 18, -4], ['flat', 14], ['rollers', 3, 0.6, 7], ['flat', 8], ['check'],
    ['kicker', 7, 1.6], ['train', 12, -2], ['landing', 16, 3], ['flat', 10], ['rollers', 2, 0.4, 6], ['flat', 6], ['check'],
    ['slope', 20, -5], ['rollers', 4, 0.7, 8], ['flat', 8], ['kicker', 8, 2], ['gap', 14, -3], ['landing', 18, 4],
    ['flat', 10], ['check'], ['dip', 18, 2], ['slope', 18, -5], ['boost', 12], ['flat', 14], ['kicker', 9, 2.4],
    ['gap', 18, -4], ['landing', 22, 5], ['flat', 12], ['finish'], ['flat', 60],
  ] },
  // Two full axial rolls, in opposite directions, with a broad run-in and
  // recovery sections. The boost lanes keep the inverted section rideable.
  { name: 'Helix Harbor', theme: 'coast', sky: 'beach', par: 43, ready: true, pieces: [
    ['flat', 16], ['slope', 18, -4], ['rollers', 3, 0.65, 8], ['flat', 8], ['check'],
    ['slope', 14, -3], ['boost', 16], ['flat', 10], ['corkscrew', 58, 3.2, 1], ['flat', 12], ['check'],
    ['bump', 12, 1.0], ['flat', 8], ['kicker', 7, 1.6], ['gap', 9, -1.5], ['landing', 14, 3], ['flat', 8], ['check'],
    ['slope', 18, -5], ['boost', 14], ['flat', 10], ['corkscrew', 48, 2.7, -1], ['flat', 12], ['check'],
    ['rollers', 3, 0.7, 8], ['slope', 16, -4], ['kicker', 8, 2.1], ['gap', 13, -3], ['landing', 18, 4],
    ['flat', 12], ['finish'], ['flat', 60],
  ] },
  // A shameless arena stunt show: two bus leaps and a fiery hoop above the
  // second gap. Boosted run-ups let the bike clear the parked buses.
  { name: 'Liberty Leap', theme: 'stuntshow', par: 38, ready: true, music: 'music_stunt', pieces: [
    ['flat', 18], ['slope', 18, -3], ['flat', 12], ['check'],
    ['boost', 18], ['flat', 10], ['kicker', 10, 3.2], ['pit', 20, -4], ['landing', 22, 4], ['flat', 12], ['check'],
    ['rollers', 3, 0.55, 8], ['slope', 14, -3], ['boost', 18], ['flat', 10],
    ['kicker', 11, 3.6], ['pit', 24, -4], ['landing', 24, 4], ['flat', 12], ['check'],
    ['slope', 18, -4], ['boost', 12], ['flat', 10], ['kicker', 8, 1.6], ['gap', 10, -2], ['landing', 16, 3],
    ['flat', 14], ['finish'], ['flat', 60],
  ] },

];

// ── sampling ────────────────────────────────────────────────────────────
function build(track) {
  const pieces = [[]];
  const checks = [], boosts = [], kickers = [], bridges = [], ice = [], movers = [], highs = [], loops = [], pinches = [], corkscrews = [];
  const ramps = track.ramps || [];
  let x = -24, y = 0, finish = null; // a flat run-up behind the start line (x = 0)
  const cur = () => pieces[pieces.length - 1];
  const put = (px, py) => { const c = cur(); const last = c[c.length - 1]; if (!last || px - last[0] > 1e-4) c.push([+px.toFixed(4), +py.toFixed(4)]); };
  put(x, y);
  const curve = (len, f) => { const n = Math.max(1, Math.round(len / STEP)); for (let i = 1; i <= n; i++) { const t = i / n; put(x + len * t, y + f(t)); } x += len; y += f(1); };
  curve(24, () => 0);
  const log = [];
  let prev = '';
  for (const p of track.pieces) {
    const [k, a, b, c] = p;
    log.push(`${x.toFixed(0)}:${p.join(' ')}`);
    const last = prev;
    prev = k;
    if (k === 'flat') curve(a, () => 0);
    else if (k === 'slope') curve(a, (t) => b * (1 - Math.cos(Math.PI * t)) / 2);
    else if (k === 'kicker') {
      // every take-off gets a flat run-up: a rider arrives level and at speed
      if (last !== 'flat' && last !== 'boost') curve(6, () => 0);
      const x0 = x; curve(a, (t) => b * t * t); kickers.push([x0, x, b]);
    }
    else if (k === 'quarter') { const x0 = x; curve(a, (t) => b * (1 - Math.sqrt(Math.max(0, 1 - t * t * 0.995)))); kickers.push([x0, x, b]); }
    else if (k === 'gap') { const x0 = x; curve(a, (t) => b * t * t * (3 - 2 * t)); kickers.push([x0, x, 0]); }
    else if (k === 'pit') { x += a; y += b; pieces.push([]); put(x, y); }
    else if (k === 'bridge') { bridges.push([x, y, x + a, y + b]); x += a; y += b; pieces.push([]); put(x, y); }
    else if (k === 'drawbridge') { movers.push([1, x, y, a]); x += a; pieces.push([]); put(x, y); }
    else if (k === 'seesaw') { movers.push([2, x + a / 2, y, a]); curve(a, () => 0); }
    else if (k === 'wreck') { movers.push([3, x + a / 2, y, a]); curve(a, () => 0); }
    else if (k === 'logs') { movers.push([4, x, y, a]); curve(a, (t) => b * (1 - Math.cos(Math.PI * t)) / 2); }
    else if (k === 'train') { movers.push([5, x + a / 2, y + b / 2, a]); const x0 = x; curve(a, (t) => b * t * t * (3 - 2 * t)); kickers.push([x0, x, 0]); }
    else if (k === 'loop') { if(b) boosts.push([x, x + 2*a + 6]); loops.push([x + 3 + a, y, a]); curve(2 * a + 6, () => 0); }
    else if (k === 'corkscrew') { corkscrews.push([x, x + a, y, b, c]); boosts.push([x, x + a]); curve(a, () => 0); }
    else if (k === 'split') { highs.push([x + (p[4] || 18), x + a - 3, y + b, y + (p[5] ?? 1.5)]); curve(a, (t) => c * t * t * (3 - 2 * t)); }
    else if (k === 'ice') ice.push([+x.toFixed(3), +(x + a).toFixed(3)]);
    else if (k === 'crater') { curve(a * 0.6, (t) => -b * Math.sin(Math.PI * t)); const x0 = x; curve(a * 0.4, (t) => b * 0.55 * t * t); kickers.push([x0, x, -b]); }
    else if (k === 'landing') curve(a, (t) => -b * (1 - Math.cos(Math.PI * t)) / 2);
    else if (k === 'rollers') { const amp = Math.min(b, c * 0.11); curve(a * c, (t) => amp * (1 - Math.cos(2 * Math.PI * a * t)) / 2); }
    else if (k === 'bump') curve(a, (t) => b * (1 - Math.cos(2 * Math.PI * t)) / 2);
    else if (k === 'dip') curve(a, (t) => -b * (1 - Math.cos(2 * Math.PI * t)) / 2);
    else if (k === 'check') checks.push([+x.toFixed(3), +y.toFixed(3)]);
    else if (k === 'boost') boosts.push([+x.toFixed(3), +(x + a).toFixed(3)]);
    else if (k === 'pinch') pinches.push([+x.toFixed(3), +(x + a).toFixed(3), b, c]);
    else if (k === 'finish') finish = [+x.toFixed(3), +y.toFixed(3)];
    else throw new Error('unknown piece ' + k);
  }
  if (!finish) throw new Error(track.name + ': no finish');
  let minY = Infinity, maxY = -Infinity;
  for (const pc of pieces) for (const [, py] of pc) { minY = Math.min(minY, py); maxY = Math.max(maxY, py); }
  if (process.env.DUMP) console.log(track.name + '\n  ' + log.join('\n  '));
  return { pieces, checks, boosts, kickers, bridges, ice, movers, highs, loops, pinches, corkscrews, ramps, finish, minY, maxY, length: x };
}

const f = (v) => { const s = String(v); return (/[.e]/.test(s) ? s : s + '.0') + 'f'; };
const cid = (s) => s.replace(/[^a-z0-9]/gi, '_').toLowerCase();
let inc = '/* generated by games/bikemesa/tools/make-tracks.mjs: do not edit */\n';
const descs = [];
TRACKS.forEach((t, ti) => {
  const b = build(t);
  const id = `t${ti + 1}`;
  b.pieces.forEach((pc, pi) => {
    inc += `static const float ${id}_p${pi}[] = {\n`;
    for (let i = 0; i < pc.length; i += 6) inc += '  ' + pc.slice(i, i + 6).map(([px, py]) => `${f(px)}, ${f(py)}`).join(', ') + ',\n';
    inc += '};\n';
  });
  inc += `static const track_piece ${id}_pieces[] = { ${b.pieces.map((pc, pi) => `{ ${id}_p${pi}, ${pc.length} }`).join(', ')} };\n`;
  inc += `static const float ${id}_checks[] = { ${b.checks.flat().map(f).join(', ') || '0'} };\n`;
  inc += `static const float ${id}_boosts[] = { ${b.boosts.flat().map(f).join(', ') || '0'} };\n`;
  inc += `static const float ${id}_kickers[] = { ${b.kickers.flat().map((v) => f(+v.toFixed(3))).join(', ') || '0'} };\n`;
  inc += `static const float ${id}_bridges[] = { ${b.bridges.flat().map((v) => f(+v.toFixed(3))).join(', ') || '0'} };\n`;
  inc += `static const float ${id}_ice[] = { ${b.ice.flat().map(f).join(', ') || '0'} };\n`;
  inc += `static const float ${id}_highs[] = { ${b.highs.flat().map((v) => f(+(+v).toFixed(3))).join(', ') || '0'} };\n`;
  inc += `static const float ${id}_loops[] = { ${b.loops.flat().map((v) => f(+(+v).toFixed(3))).join(', ') || '0'} };\n`;
  inc += `static const float ${id}_corkscrews[] = { ${b.corkscrews.flat().map((v) => f(+(+v).toFixed(3))).join(', ') || '0'} };\n`;
  inc += `static const float ${id}_pinches[] = { ${b.pinches.flat().map((v) => f(+(+v).toFixed(3))).join(', ') || '0'} };\n`;
  inc += `static const float ${id}_ramps[] = { ${b.ramps.flat().map((v) => f(+(+v).toFixed(3))).join(', ') || '0'} };\n`;
  inc += `static const float ${id}_movers[] = { ${b.movers.flat().map((v) => f(+(+v).toFixed(3))).join(', ') || '0'} };\n`;
  descs.push(`  { "${t.name}", "${t.theme}", ${t.ready ? 'true' : 'false'}, "scenes/track${String(ti + 1).padStart(2, '0')}.cwscene", ${t.par}, ${id}_pieces, ${b.pieces.length}, ` +
    `${id}_checks, ${b.checks.length}, ${id}_boosts, ${b.boosts.length}, ${id}_kickers, ${b.kickers.length}, ` +
    `${f(b.finish[0])}, ${f(b.finish[1])}, ${f(+b.minY.toFixed(3))}, ${f(+b.maxY.toFixed(3))}, ${f(+b.length.toFixed(3))}, ` +
    `${id}_bridges, ${b.bridges.length}, ${id}_ice, ${b.ice.length}, ${f(t.gravity ?? 10)}, ${f(t.grip ?? 1)}, ${f(t.drag ?? 1)}, "${t.music || 'music'}", ` +
    `${id}_movers, ${b.movers.length}, ${id}_highs, ${b.highs.length}, ${id}_loops, ${b.loops.length}, ${id}_pinches, ${b.pinches.length}, ${id}_ramps, ${b.ramps.length}, ${id}_corkscrews, ${b.corkscrews.length} },`);
  console.log(`${t.name}: ${b.length.toFixed(0)} m, ${b.pieces.length} pieces, ${b.checks.length} checkpoints, y ${b.minY.toFixed(1)}..${b.maxY.toFixed(1)}`);
  writeScene(ti + 1, t, b);
});
inc += `static const track_desc TRACKS[] = {\n${descs.join('\n')}\n};\n`;
writeFileSync(join(GAME, 'tracks.inc'), inc);

function writeScene(n, t, b) {
  const th = THEMES[t.sky || t.theme];
  const col = (c) => `color(${c.map((v) => +v.toFixed(4)).join(', ')}, 1)`;
  const s = `[scene format=1]

[node name="Track" type="Node3D"]

[node name="Sky" parent="." type="ProceduralSky"]
${t.theme === 'neon' ? 'sun = ""' : 'sun = "../Sun"'}
time_of_day = ${th.tod}
latitude = ${th.lat}
declination = 12
north = ${th.north}
sun_energy = ${th.sun}
sky_energy = ${th.sky}
mie = ${th.mie}
sun_disk_intensity = 1.2
ground_color = ${col(th.ground)}
env_update_angle = 2

[node name="Sun" parent="." type="DirectionalLight3D"]
position = vec3(0, 0, 0)
${t.theme === 'neon' ? 'rotation = vec3(-0.65, -0.4, 0)\ncolor = color(0.55, 0.7, 1, 1)\nenergy = 0.7\n' : ''}shadow = true
shadow_size = 2048
shadow_extent = 16
shadow_far = 120
shadow_bias = -0.0002
shadow_normal_bias = 0.02

[node name="Env" parent="." type="WorldEnvironment"]
ambient_color = ${col(th.amb)}
ambient_energy = 0.05
hemi_energy = ${th.hemi}

[node name="Fog" parent="." type="HeightFog"]
density = ${th.fog}
height_falloff = ${th.fogh}
base_height = ${(b.minY - 10).toFixed(1)}
use_sky_color = true
${t.theme === 'scary' ? 'volumetric = true\nvolumetric_distance = 48\nvolumetric_ambient = 1.3\nvolumetric_albedo = color(0.8, 0.85, 1, 1)\n' : ''}sun_scatter = 0.35
sun_g = 0.8
max_opacity = 0.7
start_distance = 30
sky_distance = 500

[node name="Post" parent="." type="PostProcess"]
${t.theme === 'neon' ? 'depth_prepass = true\n' : ''}tonemap = ${th.tonemap}
exposure = ${th.exposure}
bloom = true
bloom_intensity = 0.06
bloom_threshold = 1
bloom_knee = 0.6
ssao = true
ssao_radius = 0.8
ssao_intensity = 1
aa = SMAA
vignette = true
vignette_intensity = 0.25
vignette_smoothness = 0.6
grade = true
lut = "grades/${th.grade}.cube"
grade_intensity = ${t.theme === 'neon' ? 0.3 : 0.75}

[node name="Camera" parent="." type="Camera3D"]
position = vec3(0, 3, 14)
fov = 46
near = 0.3
far = 900
`;
  mkdirSync(join(GAME, 'assets', 'scenes'), { recursive: true });
  writeFileSync(join(GAME, 'assets', 'scenes', `track${String(n).padStart(2, '0')}.cwscene`), s);
}
