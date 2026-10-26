// Regenerates racer's announcer: every line spoken by an open text-to-speech model run locally,
// then given its character here (a PA horn's band, a squeeze, a little grit, a stadium's slapback
// and tail), written as Ogg into games/racer/assets/audio/voice/.
//
//   npm install --prefix games/racer/tools/voice        (once: kokoro-js, the model runner)
//   node games/racer/tools/make-voice.mjs [--asr] [--only id,id]
//
// The model: Kokoro-82M v1.0 (hexgrad, Apache-2.0) in its ONNX build (onnx-community/Kokoro-82M-v1.0-ONNX,
// Apache-2.0), run by kokoro-js 1.2.1 (Apache-2.0) on the CPU; the voice is am_fenrir from the same
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
const VOICE = 'am_fenrir';
const SPEED = 1.12; /* a touch quicker than the model's own pace: punchier */
const SR = 24000;   /* the model's rate; kept (speech has nothing above 12 kHz worth the bytes) */
L.useSR(SR);

/* ── the lines ──────────────────────────────────────────────────────────
 * id: the enum (VL_<ID>) and the file stem; say: the variants (the game rotates them, never the same twice
 * running); speed: the pace when a line wants its own. Wasteland league, comic, no real names or brands. */
const LINES = [
  /* the countdown: said on the beat, so short and even */
  { id: 'three', say: ['Three!'], speed: 1.0 },
  { id: 'two', say: ['Two!'], speed: 1.0 },
  { id: 'one', say: ['One!'], speed: 1.0 },
  { id: 'go', say: ['Go!', 'Go, go, go!'] },
  /* the start */
  { id: 'rocket_start', say: ['Rocket start!', 'What a launch!'] },
  { id: 'flooded', say: ['Flooded it!', 'Too early on the gas!'] },
  /* laps */
  { id: 'lap2', say: ['Lap two!'] },
  { id: 'lap3', say: ['Lap three!'] },
  { id: 'lap4', say: ['Lap four!'] },
  { id: 'final_lap', say: ['Final lap!', 'Last lap! Give it everything!', 'Final lap! Bring it home!'] },
  { id: 'wrong_way', say: ['Wrong way!', 'Other way, champ!', 'Turn it around!'] },
  /* the player's place */
  { id: 'lead', say: ['Into the lead!', 'Out in front!', 'Taking the lead!'] },
  { id: 'second', say: ['Up to second!', 'Second place!'] },
  { id: 'third', say: ['Up to third!', 'Third place!'] },
  { id: 'moving_up', say: ['Moving up!', 'Coming through!', 'Another one passed!'] },
  { id: 'lost_lead', say: ['Lost the lead!', 'They got by! Chase them down!'] },
  { id: 'dropping', say: ['Dropping back!', 'Don\'t let them get away!'] },
  /* items */
  { id: 'shock_warn', say: ['Shockwave incoming! Jump!', 'Shockwave! Jump it!'] },
  { id: 'shock_dodged', say: ['Dodged the shockwave!', 'Right over it!'] },
  { id: 'direct_hit', say: ['Direct hit!', 'Boom! Got them!', 'Bullseye!'] },
  { id: 'got_hit', say: ['Ouch!', 'That will leave a dent!'] },
  { id: 'shield_up', say: ['Shield up!', 'Shields up! Bring it on!'] },
  { id: 'shield_saved', say: ['The shield holds!'] },
  /* big moments */
  { id: 'huge_air', say: ['Big, big air!', 'Look at that hang time!'] },
  { id: 'perfect_landing', say: ['Perfect landing!', 'Stuck the landing!'] },
  { id: 'drift_max', say: ['Maximum drift boost!', 'Purple sparks! Full send!'] },
  { id: 'slipstream', say: ['What a slingshot!'] },
  { id: 'fall', say: ['Overboard!', 'And down they go!'] },
  /* the finish */
  { id: 'winner', say: ['Winner!', 'Winner winner, wasteland dinner!'] },
  { id: 'what_a_race', say: ['What a race!'] },
  { id: 'so_close', say: ['So close!', 'Oh, so close!'] },
  { id: 'podium', say: ['On the podium!'] },
  { id: 'finished', say: ['Across the line!', 'Better luck next time!'] },
  /* time trial */
  { id: 'new_record', say: ['New record!', 'That\'s a new record!'] },
  { id: 'medal_bronze', say: ['Bronze medal!'] },
  { id: 'medal_silver', say: ['Silver medal!'] },
  { id: 'medal_gold', say: ['Gold medal!'] },
  { id: 'medal_author', say: ['Author time! Unreal!'] },
  /* the cup */
  { id: 'cup_winner', say: ['Cup winner!', 'Champion of the cup!'] },
  { id: 'cup_trophy', say: ['A trophy! Well driven!'] },
  { id: 'cup_done', say: ['That\'s the cup! Come back stronger!'] },
  /* the welcome, one map at a time (MAPS order) */
  { id: 'map_salt', say: ['Welcome to the Salt Flats Run! Keep it shiny side up!', 'Salt Flats Run, folks! Flat out, all day!'] },
  { id: 'map_drowned', say: ['Welcome to the Drowned Downtown! Hope you brought a snorkel!', 'Drowned Downtown! Mind the puddles, they\'re deep!'] },
  { id: 'map_fair', say: ['Welcome to the Rustwheel Fairgrounds! Hang on to your lunch!', 'The Fairgrounds are open! Step right up!'] },
  { id: 'map_refinery', say: ['Welcome to the Cinder Refinery! It\'s a scorcher today!', 'Cinder Refinery! Things are heating up!'] },
  { id: 'map_dam', say: ['Welcome to the Spillway Dam! Mind the drop!', 'The Spillway Dam! Don\'t look down!'] },
  { id: 'map_frost', say: ['Welcome to Frostbite Pass! Bundle up, racers!', 'Frostbite Pass! Cold tyres, hot tempers!'] },
  { id: 'map_temple', say: ['Welcome to the Overgrowth Temple! Watch for the vines!', 'Overgrowth Temple! Ancient stones, brand new scrapes!'] },
  { id: 'map_orbit', say: ['Welcome to the Wreck Orbit! No air, no brakes, no problem!', 'Wreck Orbit! One small lap for a racer!'] },
  /* colour, when the race has been quiet a while */
  { id: 'chatter', say: ['Scrap and glory, folks!', 'Nobody brakes in this league!', 'The crowd is going wild!', 'Somebody\'s gonna need a new bumper!', 'Pedal down, folks!', 'What a day for racing!'] },
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
      out.set(fileOf(line, v), Float32Array.from(a.audio));
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
/* a delayed, darker copy (the far stand's slap) */
function slap(a, sec, gain, tone) {
  const d = Math.round(sec * SR), o = new Float32Array(a.length);
  for (let i = d; i < a.length; i++) o[i] = a[i - d] * gain;
  return lp(o, tone, 0.7);
}

const TAIL = 0.55; /* the stadium's ring after the last word, seconds */
function character(raw) {
  const [s0, s1] = speechSpan(raw);
  const pre = Math.round(SR * 0.012), post = Math.round(SR * 0.04);
  const n = (s1 + post) - Math.max(0, s0 - pre), tail = Math.round(SR * TAIL);
  const a = new Float32Array(n + tail);
  a.set(raw.subarray(Math.max(0, s0 - pre), Math.min(raw.length, s1 + post)), 0);
  removeDC(a);
  const g0 = 0.5 / (peakOf(a) || 1);
  for (let i = 0; i < a.length; i++) a[i] *= g0;
  /* the horn: a band from 200 Hz to 6.2 kHz with a lift where the words cut through a crowd (2.4 kHz) */
  hp(a, 200, 0.7); hp(a, 200, 0.7);
  lp(a, 6200, 0.7); lp(a, 6200, 0.7);
  const pres = bp(Float32Array.from(a), 2400, 1.1);
  for (let i = 0; i < a.length; i++) a[i] += pres[i] * 0.9;
  /* the squeeze and a little grit */
  compress(a);
  const g1 = 0.7 / (peakOf(a) || 1);
  for (let i = 0; i < a.length; i++) a[i] *= g1;
  softclip(a, 1.5);
  /* the stadium: two slaps off the far stands and a short wide tail */
  const s1a = slap(a, 0.115, 0.2, 3200), s2a = slap(a, 0.235, 0.09, 2400);
  const [rl, rr] = reverb(a, { room: 0.78, damp: 0.5, predelay: 0.02 });
  const out = new Float32Array(a.length);
  for (let i = 0; i < a.length; i++) out[i] = a[i] + s1a[i] + s2a[i] + 0.14 * (rl[i] + rr[i]) * 0.5;
  hp(out, 120, 0.7);
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
h += `  int ms[VL_MAX_VARIANTS];          /* how long the words last; the file rings on ${Math.round(TAIL * 1000)} ms past that (the stadium) */\n} voice_line;\n\n`;
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
