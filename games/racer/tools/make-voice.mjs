// Regenerates racer's announcer: every line spoken by an open text-to-speech model run locally,
// then given its character here: a wacky 90s radio DJ (the most sing-song voice, pitched up, through a nasal AM-radio
// band: no lows, a honk at 1.2-2.6 kHz, a hard squeeze, grit), written as Ogg into games/racer/assets/audio/voice/.
//
//   npm install --prefix games/racer/tools/voice        (once: kokoro-js, the model runner)
//   node games/racer/tools/make-voice.mjs [--asr] [--only id,id]
//
// The model: Kokoro-82M v1.0 (hexgrad, Apache-2.0) in its ONNX build (onnx-community/Kokoro-82M-v1.0-ONNX,
// Apache-2.0), run by kokoro-js 1.2.1 (Apache-2.0) on the CPU; the voice is am_liam from the same
// release (Apache-2.0). The weights are fetched from Hugging Face on the first run and cached under
// tools/voice/node_modules. The phonemes come from the phonemizer package (espeak-ng compiled to wasm,
// GPL-3.0; a tool, its output carries no licence of its own). See CREDITS.
//
// It also writes:
//   voice_lines.h     the line table the game reads (audio.c): ids, files, the words (for the dev log)
//   assets/roots.txt  the voice files between the "# voice" markers, so cwpack packs them
// and prints for every file its size, length, peak, loudness, silences and clipping. --asr transcribes
// each file back with Whisper base.en (MIT; a check only, nothing of it ships) and prints the words heard.
import { mkdirSync, readFileSync, writeFileSync, readdirSync, rmSync, statSync, existsSync } from 'node:fs';
import { dirname, join } from 'node:path';
import { fileURLToPath, pathToFileURL } from 'node:url';
import * as L from './audiolib.mjs';
import { hp, lp, bp, softclip, removeDC, peakOf, rmsOf, db, fromdb, reverb, centroid } from './audiolib.mjs';

const HERE = dirname(fileURLToPath(import.meta.url));
const GAME = join(HERE, '..');
const OUT = join(GAME, 'assets', 'audio', 'voice');
const TMP = join(GAME, '..', '..', 'out', 'racer-voice');
const MODS = join(HERE, 'voice', 'node_modules');
const VOICE = 'am_liam';
const SPEED = 1.12; /* then sped up again by PITCH: quick and wound up */
const HYPE = 1.7;   /* the pitch swing stretched this much (hype) */
const PITCH = 1.09; /* a semitone and a half up, faster with it: over-excited. am_liam swings 11-12 semitones across a hype
                     * line (am_michael, the last pick, 7: flat and boring; the measure is the 10-90% spread of its pitch) */
const SR = 24000;   /* the model's rate; kept (speech has nothing above 12 kHz worth the bytes) */
L.useSR(SR);

/* ── the lines ──────────────────────────────────────────────────────────
 * id: the enum (VL_<ID>) and the file stem; say: the variants (the game rotates them, never the same twice
 * running); speed: the pace when a line wants its own. Wasteland league, comic, no real names or brands. */
const LINES = [
  /* the countdown: said on the beat, so short and even */
  { id: 'three', say: ['Three!'], speed: 1.05 },
  { id: 'two', say: ['Two!'], speed: 1.05 },
  { id: 'one', say: ['One!'], speed: 1.05 },
  { id: 'go', say: ['Go go go!', 'Here we gooo!'] },
  /* the start */
  { id: 'rocket_start', say: ['Whoa! Rocket start, baby!', 'Ohhh, what a launch!'] },
  { id: 'flooded', say: ['Whoopsie! Flooded it!', 'Easy on the gas, hotshot!'] },
  /* laps */
  { id: 'lap2', say: ['Lap two! Keep it rollin!'] },
  { id: 'lap3', say: ['Lap three! Turn it up!'] },
  { id: 'lap4', say: ['Lap four! Still goin!'] },
  { id: 'final_lap', say: ['Final lap, baby!', 'Last lap! Floor it!', 'One more lap! Bring it home!'] },
  { id: 'wrong_way', say: ['Hey hey! Wrong way!', 'Other way, buddy!', 'Turn it around, turn it around!'] },
  /* the player's place */
  { id: 'lead', say: ['Whoa-ho! Into the lead!', 'Number one, baby!', 'Out in front! Nobody can touch ya!'] },
  { id: 'second', say: ['Up to second! Keep pushin!', 'Second place! Go get em!'] },
  { id: 'third', say: ['Third place! Movin on up!', 'Up to third, alright!'] },
  { id: 'moving_up', say: ['Coming through!', 'See ya, sucker!', 'Another one bites the dust!'] },
  { id: 'lost_lead', say: ['Ohh no! Lost the lead!', 'They got by ya! Chase em down!'] },
  { id: 'dropping', say: ['Droppin back! Step on it!', 'Hey! Don\'t let em get away!'] },
  /* items */
  { id: 'shock_warn', say: ['Shockwave comin! Tap drift to hop it!', 'Shockwave! Hop it with drift!'] },
  { id: 'shock_dodged', say: ['Woo! Right over it!', 'Ha! Dodged it, baby!'] },
  { id: 'direct_hit', say: ['Kaboom! Direct hit!', 'Bullseye, baby!', 'Ooh, got them good!'] },
  { id: 'got_hit', say: ['Ouch! That smarts!', 'Yikes! That\'s gonna leave a mark!'] },
  { id: 'shield_up', say: ['Shields up!', 'Shields up! Bring it on!'] },
  { id: 'shield_saved', say: ['Blocked it! Ha!', 'Nice try, pal!'] }, /* a shield, or an item held out back stopping a missile */
  /* big moments */
  { id: 'huge_air', say: ['Big air! Woo-hoo!', 'Look at that hang time!'] },
  { id: 'perfect_landing', say: ['Stuck the landing!', 'Smooth as butter!'] },
  { id: 'drift_max', say: ['Purple sparks! Full send!', 'Maximum drift, baby!'] },
  { id: 'slipstream', say: ['Slingshot! Yeah!'] },
  { id: 'fall', say: ['Whoa! Overboard!', 'Aaand down they go!'] },
  /* the finish */
  { id: 'winner', say: ['Winner! Oh yeah!', 'Winner! Take a bow, champ!'] },
  { id: 'what_a_race', say: ['What a race! Wow!'] },
  { id: 'so_close', say: ['Ohh, so close!', 'Argh! Inches!'] },
  { id: 'podium', say: ['On the podium, baby!'] },
  { id: 'finished', say: ['Across the line!', 'Better luck next time, champ!'] },
  /* time trial */
  { id: 'new_record', say: ['New record! Oh yeah!', 'That\'s a brand new record!'] },
  { id: 'medal_bronze', say: ['Bronze medal! Alright!'] },
  { id: 'medal_silver', say: ['Silver medal! Sweet!'] },
  { id: 'medal_gold', say: ['Gold medal! Way to go!'] },
  { id: 'medal_author', say: ['Author time! Unbelievable!'] },
  /* the cup */
  { id: 'cup_winner', say: ['Cup champion! Yeah!', 'Champion of the cup, baby!'] },
  { id: 'cup_trophy', say: ['A trophy! Nice drivin!'] },
  { id: 'cup_done', say: ['That\'s the cup! Come back swingin!'] },
  /* the welcome, one map at a time (MAPS order) */
  { id: 'map_salt', say: ['Welcome to the Salt Flats Run! Keep it shiny side up!', 'Salt Flats Run, baby! Flat out, all day!'] },
  { id: 'map_drowned', say: ['Welcome to Drowned Downtown! Hope ya brought a snorkel!', 'Drowned Downtown! Them puddles are deep!'] },
  { id: 'map_fair', say: ['Welcome to the Rustwheel Fairgrounds! Hang on to your lunch!', 'The Fairgrounds are open! Step right up!'] },
  { id: 'map_refinery', say: ['Welcome to the Cinder Refinery! It\'s a scorcher!', 'Cinder Refinery! Things are heatin up!'] },
  { id: 'map_dam', say: ['Welcome to the Spillway Dam! Mind the drop!', 'The Spillway Dam! Don\'t look down!'] },
  { id: 'map_frost', say: ['Welcome to Frostbite Pass! Bundle up!', 'Frostbite Pass! Cold tires, hot tempers!'] },
  { id: 'map_temple', say: ['Welcome to the Overgrowth Temple! Watch them vines!', 'Overgrowth Temple! Ancient stones, brand new scrapes!'] },
  { id: 'map_orbit', say: ['Welcome to Wreck Orbit! No air, no brakes, no problem!', 'Wreck Orbit! One small lap for racing!'] },
  /* colour, when the race has been quiet a while */
  { id: 'chatter', say: ['Scrap and glory, baby!', 'Nobody brakes in this league!', 'The crowd is goin wild!', 'Somebody\'s gonna need a new bumper!', 'Step on it, folks!', 'Whoo! What a day for racing!'] },
  /* a close pass (EV_NEARMISS) */
  { id: 'close_call', say: ['Whoa! Close call!', 'Inches to spare!', 'Squeezed right through!'] },
  /* battle */
  { id: 'pop', say: ['Pop! Got one!', 'There goes a balloon!', 'Pop goes the balloon!'] },
  { id: 'last_balloon', say: ['Last balloon! Careful!', 'One balloon left! Don\'t blow it!'] },
  { id: 'knocked_out', say: ['Knocked out! Ouch!', 'You\'re out of here!'] },
  { id: 'rival_out', say: ['Another one down!', 'Buh-bye!'] },
  { id: 'battle_win', say: ['Last kart standing, baby!', 'Battle champion!'] },
  { id: 'battle_over', say: ['That\'s the battle!', 'Battle over! Woo!'] },
  /* capture the flag */
  { id: 'flag_got', say: ['Flag taken! Bring it home, baby!', 'Got the flag! Now hit the gas!'] },
  { id: 'flag_stolen', say: ['They have your flag!', 'Hey! They swiped your flag!'] },
  { id: 'flag_captured', say: ['Captured! Score!', 'Captured! Oh yeah!'] },
  { id: 'flag_lost', say: ['Ouch! They scored!', 'They got one! Get it back!'] },
  { id: 'flag_saved', say: ['Flag\'s back home!', 'Nice save! Flag\'s home!'] },
  { id: 'team_win', say: ['Your team wins! Woo!', 'Team victory, baby!'] },
  /* king of the mountain */
  { id: 'king', say: ['King of the mountain!', 'Top of the world, baby!'] },
  { id: 'hill_moving', say: ['The hill is moving!', 'Heads up! The hill\'s on the move!'] },
  { id: 'contested', say: ['Push em off the hill!', 'Crowded up there!'] },
  /* the new weapons */
  { id: 'bomb_hit', say: ['Ka-blammo!', 'Bombs away, baby!'] },
  { id: 'boomerang_hit', say: ['Boing! Boomerang, baby!', 'Whack! Didn\'t see that comin back!'] },
];
const MAX_VARIANTS = 6;

/* ── the voice ──────────────────────────────────────────────────────── */
function fileOf(line, v) { return line.say.length > 1 ? `${line.id}_${v + 1}` : line.id; }

async function speakAll(lines) {
  if (!existsSync(join(MODS, 'kokoro-js'))) throw new Error('kokoro-js is missing: npm install --prefix games/racer/tools/voice');
  const K = await import(pathToFileURL(join(MODS, 'kokoro-js', 'dist', 'kokoro.js')).href);
  const tts = await K.KokoroTTS.from_pretrained('onnx-community/Kokoro-82M-v1.0-ONNX', { dtype: 'fp32', device: 'cpu' });
  const out = new Map();
  for (const line of lines)
    for (let v = 0; v < line.say.length; v++) {
      const a = await tts.generate(line.say[v], { voice: VOICE, speed: line.speed ?? SPEED });
      if (a.sampling_rate !== SR) throw new Error(`the model spoke at ${a.sampling_rate} Hz, expected ${SR}`);
      out.set(fileOf(line, v), retape(hype(Float32Array.from(a.audio), HYPE), PITCH));
    }
  return out;
}

/* played at k times speed, as a tape is: pitch and pace move together (linear interpolation) */
function retape(a, k) {
  const n = Math.floor(a.length / k), o = new Float32Array(n);
  for (let i = 0; i < n; i++) {
    const x = i * k, j = Math.floor(x), f = x - j;
    o[i] = (a[j] ?? 0) * (1 - f) + (a[j + 1] ?? 0) * f;
  }
  return o;
}

/* the hype: the pitch's swings stretched about its median (TD-PSOLA: grains two periods long, laid back down at the new
 * pitch's spacing, so the timing is kept). k = 1.7 makes a line's 12-semitone swing about 20: the sing-song of a DJ
 * shouting over the race instead of a newsreader's 7. The model's own delivery is too even for "wacky" */
function hype(a, k) {
  const hop = Math.round(SR * 0.005), win = Math.round(SR * 0.03), minL = Math.round(SR / 400), maxL = Math.round(SR / 70);
  const nf = Math.floor(a.length / hop), per = new Float32Array(nf); /* the period at each 5 ms, 0 unvoiced */
  for (let f = 0; f < nf; f++) {
    const s0 = f * hop - (win >> 1);
    if (s0 < 0 || s0 + win + maxL >= a.length) continue;
    let e = 0; for (let i = 0; i < win; i++) e += a[s0 + i] * a[s0 + i];
    if (e / win < 1e-4) continue;
    let best = 0, bl = 0;
    for (let l = minL; l <= maxL; l++) { let c = 0; for (let i = 0; i < win; i++) c += a[s0 + i] * a[s0 + i + l]; if (c > best) best = c, bl = l; }
    if (best / e > 0.5) per[f] = bl;
  }
  const voiced = Array.from(per).filter(x => x > 0).sort((x, y) => x - y);
  if (voiced.length < 10) return a;
  const med = voiced[voiced.length >> 1];
  /* analysis marks: one per period through voiced stretches, every 5 ms elsewhere */
  const marks = [];
  for (let t = 0; t < a.length;) {
    const p = per[Math.min(nf - 1, Math.floor(t / hop))];
    marks.push([t, p]);
    t += p > 0 ? p : hop;
  }
  const out = new Float32Array(a.length);
  let mi = 0;
  for (let t = 0; t < a.length;) {
    while (mi + 1 < marks.length && marks[mi + 1][0] <= t) mi++;
    const [mt, p] = marks[mi];
    /* the new period: the old one's distance from the median stretched k times (in log, so in semitones) */
    const np = p > 0 ? Math.max(minL, Math.min(maxL, Math.round(med * Math.pow(p / med, k)))) : hop;
    const L = p > 0 ? p : hop;
    for (let i = -L; i < L; i++) {
      const src = mt + i, dst = t + i;
      if (src < 0 || src >= a.length || dst < 0 || dst >= a.length) continue;
      out[dst] += a[src] * (0.5 + 0.5 * Math.cos(Math.PI * i / L));
    }
    t += np;
  }
  return out;
}

/* ── the character ──────────────────────────────────────────────────── */
/* where the speech is: a 10 ms envelope against a floor 45 dB under the peak */
function speechSpan(a) {
  const w = Math.round(SR * 0.01), pk = peakOf(a), floor = pk * fromdb(-45);
  let first = -1, last = -1;
  for (let s = 0; s < a.length; s += w) {
    let m = 0;
    for (let i = s; i < Math.min(a.length, s + w); i++) m = Math.max(m, Math.abs(a[i]));
    if (m > floor) { if (first < 0) first = s; last = Math.min(a.length, s + w); }
  }
  return [Math.max(0, first), last < 0 ? a.length : last];
}
/* a feed-forward compressor on a smoothed level: fast attack, slower release */
function compress(a, { thresh = -22, ratio = 4, attack = 0.004, release = 0.09 } = {}) {
  const ka = 1 - Math.exp(-1 / (attack * SR)), kr = 1 - Math.exp(-1 / (release * SR)), t = fromdb(thresh);
  let e = 0;
  for (let i = 0; i < a.length; i++) {
    const x = Math.abs(a[i]);
    e += (x > e ? ka : kr) * (x - e);
    const g = e > t ? Math.pow(e / t, 1 / ratio - 1) : 1;
    a[i] *= g;
  }
  return a;
}

const TAIL = 0.3; /* the room's ring after the last word, seconds */
function character(raw) {
  const [s0, s1] = speechSpan(raw);
  const pre = Math.round(SR * 0.012), post = Math.round(SR * 0.04);
  const n = (s1 + post) - Math.max(0, s0 - pre), tail = Math.round(SR * TAIL);
  const a = new Float32Array(n + tail);
  a.set(raw.subarray(Math.max(0, s0 - pre), Math.min(raw.length, s1 + post)), 0);
  removeDC(a);
  const g0 = 0.5 / (peakOf(a) || 1);
  for (let i = 0; i < a.length; i++) a[i] *= g0;
  /* the nasal radio: no chest (high-pass 300 Hz), no air (low-pass 5 kHz), a honk where the nose rings (1.2 kHz) and a
   * bite above it (2.6 kHz), then the station's hard squeeze and grit */
  hp(a, 300, 0.7); hp(a, 300, 0.7);
  lp(a, 5000, 0.7); lp(a, 5000, 0.7);
  const honk = bp(Float32Array.from(a), 1250, 2.2), bite = bp(Float32Array.from(a), 2600, 1.6);
  for (let i = 0; i < a.length; i++) a[i] += honk[i] * 1.2 + bite[i] * 0.8;
  compress(a, { thresh: -30, ratio: 8, attack: 0.002, release: 0.05 });
  const g1 = 0.8 / (peakOf(a) || 1);
  for (let i = 0; i < a.length; i++) a[i] *= g1;
  softclip(a, 2.6);
  /* a small bright room, just enough to sit it in the race */
  const [rl, rr] = reverb(a, { room: 0.32, damp: 0.35, predelay: 0.008 });
  const out = new Float32Array(a.length);
  for (let i = 0; i < a.length; i++) out[i] = a[i] + 0.07 * (rl[i] + rr[i]) * 0.5;
  hp(out, 200, 0.7);
  /* the tail fades to nothing; the start rises from nothing (no clicks) */
  const fo = Math.round(SR * 0.25), fi = Math.round(SR * 0.004);
  for (let i = 0; i < fi; i++) out[i] *= i / fi;
  for (let i = 0; i < fo; i++) out[out.length - 1 - i] *= i / fo;
  removeDC(out);
  /* level: the speech's loudness to -16 dBFS RMS (at most 6 dB past where its peak would sit at the ceiling), then a
   * look-ahead limiter holds the peaks at -2 dBFS (the encoder adds a little): every line comes out as loud */
  const sp = out.subarray(0, n), r = rmsOf(sp), pk = peakOf(out), ceil = fromdb(-2);
  const g = Math.min(fromdb(-16) / (r || 1), fromdb(6) * ceil / (pk || 1));
  for (let i = 0; i < out.length; i++) out[i] *= g;
  limit(out, ceil);
  return { a: out, speech: n };
}
/* offline peak limiter: the gain each sample needs, its minimum over a 2 ms look-ahead, eased back up over 60 ms */
function limit(a, ceil) {
  const n = a.length, look = Math.round(SR * 0.002), need = new Float32Array(n);
  const ka = 1 - Math.exp(-1 / (0.0006 * SR)), kr = 1 - Math.exp(-1 / (0.06 * SR));
  for (let i = 0; i < n; i++) need[i] = Math.min(1, ceil / (Math.abs(a[i]) || 1e-9));
  let g = 1, over = 0;
  for (let i = 0; i < n; i++) {
    let m = 1;
    for (let j = i; j < Math.min(n, i + look); j++) m = Math.min(m, need[j]);
    g += (m < g ? ka : kr) * (m - g); /* down within the look-ahead (three time constants), back up slowly */
    let y = a[i] * g;
    if (Math.abs(y) > ceil) y = Math.sign(y) * ceil, over++; /* what the attack did not quite catch */
    a[i] = y;
  }
  return over;
}

/* ── the checks ─────────────────────────────────────────────────────── */
/* the longest run with nothing in it, inside the speech (a word lost, a stall in the model) */
function longestGap(a, n) {
  const w = Math.round(SR * 0.01), floor = peakOf(a) * fromdb(-40);
  let run = 0, best = 0;
  for (let s = 0; s + w <= n; s += w) {
    let m = 0;
    for (let i = s; i < s + w; i++) m = Math.max(m, Math.abs(a[i]));
    run = m < floor ? run + w : 0;
    best = Math.max(best, run);
  }
  return best / SR;
}

/* ── main ───────────────────────────────────────────────────────────── */
const argv = process.argv.slice(2);
const only = argv.includes('--only') ? new Set(argv[argv.indexOf('--only') + 1].split(',')) : null;
for (const l of LINES) if (l.say.length > MAX_VARIANTS) throw new Error(l.id + ': too many variants');
mkdirSync(OUT, { recursive: true });
mkdirSync(TMP, { recursive: true });
const todo = only ? LINES.filter((l) => only.has(l.id)) : LINES;
if (!only) for (const f of readdirSync(OUT)) if (f.endsWith('.ogg')) rmSync(join(OUT, f));
/* a partial run clears the old takes of the lines it remakes (a line that went from one take to two renames its file) */
else for (const f of readdirSync(OUT)) if (todo.some((l) => new RegExp(`^${l.id}(_\\d+)?\\.ogg$`).test(f))) rmSync(join(OUT, f));
const spoken = await speakAll(todo);
const report = [];
for (const line of todo)
  for (let v = 0; v < line.say.length; v++) {
    const stem = fileOf(line, v);
    const { a, speech } = character(spoken.get(stem));
    const path = join(OUT, stem + '.ogg');
    L.writeOgg(path, a, TMP, 3, SR);
    report.push({ stem, text: line.say[v], path, speech });
  }

/* the table the game reads */
const enumName = (id) => 'VL_' + id.toUpperCase();
const cstr = (s) => JSON.stringify(s);
let h = '/* racer\'s announcer lines: GENERATED by tools/make-voice.mjs (edit the LINES there, then run it). */\n';
h += '#ifndef RACER_VOICE_LINES_H\n#define RACER_VOICE_LINES_H\n\n';
h += `#define VL_MAX_VARIANTS ${MAX_VARIANTS}\n`;
h += 'enum {\n' + LINES.map((l) => `  ${enumName(l.id)},`).join('\n') + '\n  VL_N\n};\n\n';
h += 'typedef struct voice_line {\n  const char *id;\n  int n;                           /* variants */\n  const char *file[VL_MAX_VARIANTS];\n  const char *text[VL_MAX_VARIANTS];\n';
h += `  int ms[VL_MAX_VARIANTS];          /* how long the words last; the file rings on ${Math.round(TAIL * 1000)} ms past that (the room) */\n} voice_line;\n\n`;
h += 'static const voice_line VOICE_LINES[VL_N] = {\n';
for (const l of LINES) {
  const files = l.say.map((_, v) => cstr(`audio/voice/${fileOf(l, v)}.ogg`)).join(', ');
  const texts = l.say.map(cstr).join(', ');
  /* from the files on disk, so a partial run (--only) still writes the whole table */
  const ms = l.say.map((_, v) => Math.max(0, Math.round((L.decode(join(OUT, fileOf(l, v) + '.ogg'), TMP, SR, 1)[0].length / SR - TAIL) * 1000))).join(', ');
  h += `  [${enumName(l.id)}] = { ${cstr(l.id)}, ${l.say.length}, { ${files} }, { ${texts} }, { ${ms} } },\n`;
}
h += '};\n\n#endif\n';
writeFileSync(join(GAME, 'voice_lines.h'), h);

/* the files cwpack packs */
const rootsPath = join(GAME, 'assets', 'roots.txt');
let roots = readFileSync(rootsPath, 'utf8');
const all = LINES.flatMap((l) => l.say.map((_, v) => `audio/voice/${fileOf(l, v)}.ogg`));
const block = '# voice (tools/make-voice.mjs)\n' + all.join('\n') + '\n# end voice\n';
if (roots.includes('# voice (tools/make-voice.mjs)')) roots = roots.replace(/# voice \(tools\/make-voice\.mjs\)\n[\s\S]*?# end voice\n/, block);
else roots = roots.replace(/\s*$/, '\n') + block;
writeFileSync(rootsPath, roots);

/* the report, from the files as written (decoded back) */
let asr = null;
if (argv.includes('--asr')) {
  const T = await import(pathToFileURL(join(MODS, '@huggingface', 'transformers', 'dist', 'transformers.node.mjs')).href);
  asr = await T.pipeline('automatic-speech-recognition', 'onnx-community/whisper-base.en', { dtype: 'fp32', device: 'cpu' });
}
const issues = [];
let total = 0;
console.log('file                     bytes   sec  speech  peak dB  rms dB  centroid  gap s  clip' + (asr ? '  heard' : ''));
for (const f of report) {
  const [d] = L.decode(f.path, TMP, SR, 1);
  const bytes = statSync(f.path).size;
  total += bytes;
  const pk = peakOf(d), r = rmsOf(d.subarray(0, f.speech)), gap = longestGap(d, Math.min(f.speech, d.length));
  let clip = 0;
  for (const x of d) if (Math.abs(x) >= 0.999) clip++;
  let heard = '';
  if (asr) {
    const d16 = L.decode(f.path, TMP, 16000, 1)[0];
    heard = (await asr(d16)).text.trim();
  }
  const row = `${(f.stem + '.ogg').padEnd(22)} ${String(bytes).padStart(7)} ${(d.length / SR).toFixed(2).padStart(5)} ${(f.speech / SR).toFixed(2).padStart(6)} ${db(pk).toFixed(1).padStart(8)} ${db(r).toFixed(1).padStart(7)} ${centroid(d, SR).toFixed(0).padStart(9)} ${gap.toFixed(2).padStart(6)} ${String(clip).padStart(5)}`;
  console.log(row + (asr ? '  ' + heard : ''));
  if (clip > 0) issues.push(`${f.stem}: ${clip} clipped samples`);
  if (db(pk) > -0.5) issues.push(`${f.stem}: peak ${db(pk).toFixed(1)} dBFS`);
  if (db(r) < -22 || db(r) > -12) issues.push(`${f.stem}: loudness ${db(r).toFixed(1)} dBFS`);
  if (gap > 0.45) issues.push(`${f.stem}: a silence of ${gap.toFixed(2)} s inside the speech`);
  if (f.speech / SR > 4.5) issues.push(`${f.stem}: ${(f.speech / SR).toFixed(1)} s is long for a call`);
  if (asr) {
    const norm = (s) => s.toLowerCase().replace(/[^a-z0-9 ]/g, ' ').replace(/\b(three|two|one|four)\b/g, (w) => ({ three: '3', two: '2', one: '1', four: '4' })[w]).split(/\s+/).filter(Boolean);
    const want = norm(f.text), got = new Set(norm(heard));
    const miss = want.filter((w) => !got.has(w));
    if (miss.length > Math.max(1, want.length * 0.34)) issues.push(`${f.stem}: heard "${heard}" for "${f.text}"`);
  }
}
console.log(`voice total ${(total / 1024).toFixed(0)} KB, ${report.length} files, ${LINES.length} lines`);
if (issues.length) console.log('\nISSUES:\n  ' + issues.join('\n  '));
else console.log('\nno clipping, loudness, silence' + (asr ? ' or transcription' : '') + ' problems in the voice');
