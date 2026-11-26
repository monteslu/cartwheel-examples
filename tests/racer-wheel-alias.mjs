// racer: the karts' wheels never read as rolling backwards (the wagon-wheel effect). A rim with n spokes looks the same
// every 2pi/n of turn, so the eye takes the turn between two drawn frames as the shortest way to the next look: past half
// a spoke gap a frame it reads backwards. The tread blocks do the same against their own spacing, and the one painted
// spoke against a whole turn. models.c rig_wheel_roll draws each rim crisp at a crawl, then smeared with its drawn turn
// capped, then blurred to a disc, and each tyre crisp then as a smooth ring (WHEEL_* in models.h).
//
// The verdict for one drawn frame of one part (rim or tyre): blurred, or the drawn turn a frame, taken modulo its gap to
// the nearest look (-1/2..1/2 of a gap), turns the same way as the wheel and by at most MAX_SHARE of the gap (past that it
// is a coin toss for the eye); a rim's painted spoke the same against a whole turn. A crisp part must also turn with the
// wheel itself (drawn = true turn), and under the smear threshold every part must be crisp.
//
// Legs:
//   model    the drawing rule (WHEEL_* read from models.h, the vehicles from sim.c and models_veh.c) run over every speed
//            from a standstill to 100 m/s and back, every vehicle, at 24 to 240 frames a second, with uneven frame
//            times too. The old rule (every frame crisp at the true turn) must fail it: the control.
//   cart     the built cart through romdev: a race in each vehicle (race ... <ride>), the gas held from the grid, then put
//            on the road at 90 m/s and held down to its top speed; the cart records the player's rear wheel as drawn,
//            frame by frame (wheels.*). Every frame must pass, the model run over the same frame steps must draw the same
//            looks and turns (the model is the cart's rule), and the run must cover a crawl to past 60 m/s.
//   control  the same cart with sw wheel_alias 1 (the old way): its frames must fail.
//   shots    consecutive frames of the chase view at a crawl, mid speed, top speed and a boost's speed, the old way and
//            the new, kept in out/racer-wheel-alias to look at (a still cannot show the motion: look at each run of frames)
//   node tests/racer-wheel-alias.mjs [--legs model,cart,control,shots] [--gl] [--webgpu] [--rides 0,1,...] [--cart <path>]
import { ROMDEV_URL } from './lib/romdev-url.mjs';
import { mkdirSync, readFileSync } from 'node:fs';
import { resolve, dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';
import { request } from 'node:http';

const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '..'), GAME = join(ROOT, 'games/racer');
const SESSION = process.env.RACER_SESSION || 'racer-wheel-alias', AGENT = process.env.RACER_AGENT || 'racer-wheel-alias', OUT = join(ROOT, 'out/racer-wheel-alias');
const arg = (n, d) => { const i = process.argv.indexOf(n); return i > 0 ? process.argv[i + 1] : d; };
const CART = resolve(arg('--cart', join(ROOT, 'build/cart-wgpu/carts/racer.wasc')));
const BACKENDS = process.argv.includes('--gl') ? ['gl'] : process.argv.includes('--webgpu') ? ['webgpu'] : ['gl', 'webgpu'];
const LEGS = arg('--legs', 'model,cart,control,shots').split(',');
const MAX_SHARE = 0.45, TAU = 2 * Math.PI;
let fail = 0;
const check = (name, ok, detail) => { console.log(`${ok ? 'ok  ' : 'FAIL'} ${name}: ${detail}`); if (!ok) fail++; };

// ── the game's numbers, read from its source ──
const src = (f) => readFileSync(join(GAME, f), 'utf8');
const num = (s, name) => { const m = s.match(new RegExp(`#define ${name} ([0-9.]+)f?`)); if (!m) throw new Error(`models.h: no ${name}`); return parseFloat(m[1]); };
const H = src('models.h');
const RULE = { smear: num(H, 'WHEEL_SMEAR_AT'), blur: num(H, 'WHEEL_BLUR_AT'), cap: num(H, 'WHEEL_STEP_CAP'), hyst: num(H, 'WHEEL_HYST') };
const VEH = [...src('sim.c').matchAll(/^\s*\{ "(\w+)", "[^"]*", ([0-9.]+)f, (?:[^,]+, ){8}([0-9.]+)f \},/gm)].map((m) => ({ name: m[1], top: +m[2], r: +m[3] }));
const WL = [...src('models_veh.c').matchAll(/vg->wl = \(wheel_look\)\{ [0-9.]+f, (\d+), [0-9.]+f, (\d+),/g)].map((m) => ({ lugs: +m[1], spokes: +m[2] }));
if (VEH.length !== 6 || WL.length !== 6) { console.error(`read ${VEH.length} vehicles and ${WL.length} wheel looks from the source, not 6`); process.exit(3); }
VEH.forEach((v, i) => Object.assign(v, WL[i]));
const CRISP = 0, SMEAR = 1, BLUR = 2;

// ── models.c rig_wheel_roll, line for line (the cart leg proves it draws what the cart draws) ──
function roller(old) {
  const look = [CRISP, CRISP];
  return (step, gaps) => gaps.map((gap, p) => {
    const f = Math.abs(step) / gap;
    if (look[p] === CRISP && f > RULE.smear) look[p] = p ? BLUR : SMEAR;
    if (look[p] === SMEAR && f > RULE.blur) look[p] = BLUR;
    if (look[p] === BLUR && f < (p ? RULE.smear : RULE.blur) - RULE.hyst) look[p] = p ? CRISP : SMEAR;
    if (look[p] === SMEAR && f < RULE.smear - RULE.hyst) look[p] = CRISP;
    if (old) look[p] = CRISP;
    const cap = RULE.cap * gap, d = look[p] === CRISP ? step : Math.max(Math.min(step, cap), -cap);
    return { look: look[p], drawn: d };
  });
}

// ── the verdict on one drawn frame of one part ──
const near = (x, period) => { let f = (((x / period) % 1) + 1) % 1; if (f > 0.5) f -= 1; return f; }; /* -1/2..1/2 */
/** why this frame reads wrong, or '' */
function wrong(step, look, drawn, gap, rim) {
  if (Math.abs(step) < 1e-6 || look === BLUR) return '';
  const f = near(drawn, gap), sg = Math.sign(step);
  if (Math.sign(f) !== sg || Math.abs(f) > MAX_SHARE) return `reads ${Math.sign(f) === sg ? 'either way' : 'backwards'} (${f.toFixed(2)} of a gap)`;
  if (rim) { const m = near(drawn, TAU); if (Math.sign(m) !== sg || Math.abs(m) > MAX_SHARE) return `the painted spoke reads backwards (${m.toFixed(2)} of a turn)`; }
  if (look === CRISP && Math.abs(drawn - step) > 1e-4) return `crisp but drawn ${drawn.toFixed(3)} for a turn of ${step.toFixed(3)}`;
  if (Math.abs(step) / gap < RULE.smear - RULE.hyst && look !== CRISP) return `not crisp at ${(Math.abs(step) / gap).toFixed(2)} of a gap a frame`;
  return '';
}

/** the frames of one run: {speed, step, parts: [{look, drawn}, {look, drawn}]}; returns per part the bad count and the first few */
function judge(frames, v) {
  const gaps = [TAU / v.spokes, TAU / v.lugs], out = [];
  for (let p = 0; p < 2; p++) {
    let bad = 0, crisp = 0, blur = 0, crispTop = 0;
    const ex = [];
    for (const fr of frames) {
      const w = wrong(fr.step, fr.parts[p].look, fr.parts[p].drawn, gaps[p], p === 0);
      if (fr.parts[p].look === CRISP && Math.abs(fr.step) > 1e-6) crisp++, crispTop = Math.max(crispTop, Math.abs(fr.speed));
      if (fr.parts[p].look === BLUR) blur++;
      if (w) { bad++; if (ex.length < 3) ex.push(`${fr.speed.toFixed(1)} m/s: ${w}`); }
    }
    out.push({ bad, crisp, blur, crispTop, ex, n: frames.length });
  }
  return out;
}
const PART = ['rim', 'tyre'];

// ── leg: the model over every speed and frame rate ──
if (LEGS.includes('model')) {
  console.log('--- model: every vehicle, 0 to 100 m/s and back, 24 to 240 frames a second');
  const FPS = [24, 30, 50, 60, 72, 90, 120, 144, 165, 240];
  for (const old of [false, true]) {
    let worst = 0;
    for (const v of VEH) {
      const gaps = [TAU / v.spokes, TAU / v.lugs];
      for (const fps of FPS)
        for (const uneven of [false, true]) {
          const roll = roller(old), frames = [];
          let seed = 7;
          const rnd = () => ((seed = (seed * 1103515245 + 12345) >>> 0) / 4294967296);
          /* up at 11 m/s^2 to 100 m/s, hold, back down at 11 m/s^2, with a few reverses at a crawl */
          let t = 0, speed = 0, phase = 0;
          while (phase < 4) {
            const dt = (1 / fps) * (uneven ? 0.75 + 0.5 * rnd() : 1);
            if (phase === 0) { speed += 11 * dt; if (speed >= 100) phase = 1, t = 0; }
            else if (phase === 1) { t += dt; if (t > 0.5) phase = 2; }
            else if (phase === 2) { speed -= 11 * dt; if (speed <= -6) phase = 3; }
            else { speed += 11 * dt; if (speed >= 0) phase = 4; }
            const step = (speed / v.r) * dt;
            frames.push({ speed, step, parts: roll(step, gaps) });
          }
          const j = judge(frames, v);
          for (let p = 0; p < 2; p++) {
            worst = Math.max(worst, j[p].bad / j[p].n);
            if (!old && j[p].bad) check(`model: ${v.name} ${PART[p]} at ${fps} fps${uneven ? ' (uneven frames)' : ''}`, false, `${j[p].bad} of ${j[p].n} frames read wrong: ${j[p].ex.join('; ')}`);
          }
          if (!old && fps === 60 && !uneven)
            console.log(`     ${v.name.padEnd(10)} 60 fps: rim crisp to ${j[0].crispTop.toFixed(1)} m/s, then smeared and capped, blurred from ${((RULE.blur * gaps[0] * 60) * v.r).toFixed(1)} m/s; tyre crisp to ${j[1].crispTop.toFixed(1)} m/s`);
        }
    }
    if (!old) check('model: every vehicle, part, frame rate and speed reads forward or blurred', fail === 0, `worst share of wrong frames ${(worst * 100).toFixed(1)}%`);
    else check('model control: the old rule (crisp at the true turn) reads wrong', worst > 0.2, `worst share of wrong frames ${(worst * 100).toFixed(1)}% (must be over 20%)`);
  }
  /* the old rule at 60 fps, the share of speeds that read backwards, for the record */
  for (const v of VEH) {
    const sh = [];
    for (const [gap, label] of [[TAU / v.spokes, 'spokes'], [TAU / v.lugs, 'tread'], [TAU, 'painted spoke']]) {
      let back = 0, n = 0;
      for (let s = 1; s <= 90; s += 0.25) { n++; if (near((s / v.r) / 60, gap) < 0) back++; }
      sh.push(`${label} ${((back / n) * 100).toFixed(0)}%`);
    }
    console.log(`     old way, ${v.name.padEnd(10)} 1-90 m/s at 60 fps, the share of speeds read backwards: ${sh.join(', ')}`);
  }
}

// ── romdev ──
const bye = async (code) => {
  if (LEGS.some((l) => l !== 'model')) { try { await post('host', '{"op":"shutdown"}'); } catch {} }
  console.log(fail ? `${fail} FAILED` : 'all ok');
  process.exit(code);
};
const post = (name, body) => new Promise((ok, no) => {
  const q = request(`${ROMDEV_URL}/tool/${name}`, { method: 'POST', headers: { 'Content-Type': 'application/json', 'x-romdev-session': SESSION, 'x-romdev-agent': AGENT } }, (res) => {
    let t = '';
    res.setEncoding('utf8');
    res.on('data', (c) => (t += c));
    res.on('end', () => ok({ status: res.statusCode, text: t }));
  });
  q.on('error', no);
  q.end(body);
});
const tool = async (name, args) => {
  let r;
  try { r = await post(name, JSON.stringify(args)); }
  catch (e) { console.error(`romdev ${name}: ${e.message} (the server is down)`); await bye(3); }
  let j; try { j = JSON.parse(r.text); } catch { j = r.text; }
  if (r.status !== 200 || j?.error) { console.error(`romdev ${name}: ${r.text.slice(0, 400)}`); await bye(3); }
  return j;
};
const rd = async (n) => { const v = await tool('wasm', { op: 'read', name: n }); return v.values ?? v.value; };
const state = async () => Object.fromEntries((await tool('wasm', { op: 'debugState' })).fields.map((f) => [f.name, f.values ?? f.value]));
const cmd = (text) => tool('wasm', { op: 'command', text });
/* ranges, not loops: the pad held for a stretch of frames and those frames stepped, in one request */
const hold = (frames, p0) => tool('input', { op: 'sequence', steps: [{ input: { ports: [p0, {}] }, frames }] });

async function race(ride, alias) {
  await hold(1, {});
  await cmd(`sw wheel_alias ${alias ? 1 : 0}`);
  await cmd(`race 0 1 1 2 1 ${ride}`); /* one human, one lap, the fastest class, the steering assist on, this ride */
  let phase = 0;
  for (let f = 0; f < 1500 && phase !== 2; f += 30) { await hold(30, {}); phase = +(await rd('race.phase')); }
  if (phase !== 2) { console.error(`ride ${ride}: no race started (phase ${phase})`); await bye(3); }
}
/** the record since `wheels reset` */
function recorded(s) {
  const n = +s['wheels.n'], fr = [];
  for (let i = 0; i < n; i++)
    fr.push({ speed: s['wheels.speed'][i], step: s['wheels.step'][i * 2], parts: [0, 1].map((p) => ({ look: s['wheels.look'][i * 2 + p], drawn: s['wheels.drawn'][i * 2 + p] })) });
  return { veh: +s['wheels.veh'], gaps: s['wheels.gap'], fr };
}
/** a race in this ride: from the grid with the gas held, then put at 90 m/s and held down to its top speed */
async function drive(ride, alias) {
  await race(ride, alias);
  await cmd('wheels reset 0 2');
  await hold(420, { south: true });
  await cmd('put 120 90');
  await hold(360, { south: true });
  await hold(1, {});
  return recorded(await state());
}

const RIDES = arg('--rides', '0,1,2,3,4,5').split(',').map(Number);
if (LEGS.some((l) => l !== 'model')) {
  mkdirSync(OUT, { recursive: true });
  for (const backend of BACKENDS) {
    console.log(`--- ${backend}`);
    const l = await tool('loadMedia', { platform: 'wasmcart', path: CART, gpuPower: 'low-power', ...(backend === 'gl' ? { wgpu: false } : {}) });
    if (!l.loaded) { console.error(JSON.stringify(l).slice(0, 300)); await bye(3); }
    await hold(120, {});
    if (LEGS.includes('cart'))
      for (const ride of RIDES) {
        const v = VEH[ride], r = await drive(ride, false);
        if (r.veh !== ride) { check(`cart ride ${ride}`, false, `the record is of vehicle ${r.veh}`); continue; }
        const lo = Math.min(...r.fr.map((f) => Math.abs(f.speed))), hi = Math.max(...r.fr.map((f) => Math.abs(f.speed)));
        check(`cart ${v.name}: the run covers a crawl to past 60 m/s`, r.fr.length > 600 && lo < 2 && hi > 60, `${r.fr.length} frames, ${lo.toFixed(1)} to ${hi.toFixed(1)} m/s`);
        /* the model over the cart's own frame steps draws what the cart drew */
        const roll = roller(false);
        let differ = 0, first = '';
        for (const [i, f] of r.fr.entries()) {
          const m = roll(f.step, [TAU / v.spokes, TAU / v.lugs]);
          for (let p = 0; p < 2; p++)
            if (m[p].look !== f.parts[p].look || Math.abs(m[p].drawn - f.parts[p].drawn) > 1e-4) { differ++; if (!first) first = `frame ${i} ${PART[p]}: model ${m[p].look}/${m[p].drawn.toFixed(4)}, cart ${f.parts[p].look}/${f.parts[p].drawn.toFixed(4)}`; }
        }
        check(`cart ${v.name}: the model draws what the cart drew`, differ === 0, differ ? `${differ} differ, first ${first}` : `${r.fr.length} frames, rim and tyre alike`);
        const j = judge(r.fr, v);
        for (let p = 0; p < 2; p++)
          check(`cart ${v.name} ${backend}: the ${PART[p]} reads forward or blurred every frame`, j[p].bad === 0,
            `${j[p].n} frames: ${j[p].crisp} crisp (to ${j[p].crispTop.toFixed(1)} m/s), ${j[p].blur} blurred, ${j[p].bad} wrong${j[p].ex.length ? ': ' + j[p].ex.join('; ') : ''}`);
      }
    if (LEGS.includes('control')) {
      const v = VEH[RIDES[0]], r = await drive(RIDES[0], true);
      const j = judge(r.fr, v);
      check(`control: sw wheel_alias 1, ${v.name}: the old way reads backwards`, j[0].bad > r.fr.length * 0.2 && j[1].bad > r.fr.length * 0.2,
        `rim ${j[0].bad} and tyre ${j[1].bad} of ${r.fr.length} frames wrong (must be over 20%): ${j[0].ex.join('; ')}`);
      await cmd('sw wheel_alias 0');
    }
    if (LEGS.includes('shots'))
      for (const alias of [true, false])
        for (const ride of [RIDES[0]]) {
          await race(ride, alias);
          for (const [name, speed] of [['crawl', 3], ['mid', 15], ['top', 38], ['boost', 70]]) {
            await cmd(`put 120 ${speed}`);
            await hold(20, speed > 30 ? { south: true } : {});
            const dir = join(OUT, `${backend}-${VEH[ride].name}-${alias ? 'old' : 'new'}-${name}`);
            await tool('recordSession', { frames: 4, sampleEvery: 1, includeScreenshots: true, outputDir: dir, ...(speed > 30 ? { holdInputs: [{ b: true }, {}] } : {}) });
            /* from the side: a camera held 3.5 m to the kart's left, where it will be 12 frames on, and the frames as it passes */
            await cmd('wheels reset 0 2');
            await hold(2, {});
            const s = await state(), p = s['wheels.pos'], f = s['wheels.fwd'];
            const ahead = Math.max(speed, 1) * 12 / 60, q = [0, 1, 2].map((i) => p[i] + f[i] * ahead);
            const left = [f[2], 0, -f[0]], n = Math.hypot(left[0], left[2]) || 1; /* up x forward: the kart's left (+x) */
            const eye = [q[0] + (left[0] / n) * 3.5, q[1] + 0.4, q[2] + (left[2] / n) * 3.5];
            await cmd(`eye ${eye.map((x) => x.toFixed(2)).join(' ')} ${q[0].toFixed(2)} ${(q[1] - 0.2).toFixed(2)} ${q[2].toFixed(2)}`);
            await hold(9, {});
            await tool('recordSession', { frames: 6, sampleEvery: 1, includeScreenshots: true, outputDir: `${dir}-side` });
            await cmd('eye');
          }
        }
    await tool('host', { op: 'shutdown' });
  }
}
await bye(fail ? 1 : 0);
