// bikemesa's ride gates on the cart through romdev, GL and WebGPU, timed from the game's own state instead of
// absolute frame numbers. Each ride in tests/bikemesa-rides.json gets to the start line by the game's state (the
// menus pressed through as the screen comes up, or a start request such as "track 2"), then waits on the debug
// field ride.start (the tick the countdown's GO lands on, known as soon as the countdown begins) and plays its
// input from there: an entry's "at" is in ticks from GO, so the ride's presses land on the same stretch of track
// however long a host spent getting there. The run ends `ticks` after GO and its named state must equal `expect`.
// Every ride's control (its "control": another input, other start requests, or none) runs the same way and must
// miss `expect`, or the gate could not tell a different ride apart. The WebGPU leg must match the GL leg's named
// state exactly (state.hash, bike.pos and every expected field).
//
// Ranges, never a loop over frames: a stretch of held input is one request (an input sequence); the waits look at
// the state every 10 frames. The request count is printed for each ride.
//
//   node tests/bikemesa-rides.mjs [ride ...] [--gl | --webgpu]
import { ROMDEV_HOST, ROMDEV_PORT } from './lib/romdev-url.mjs';
import { mkdirSync, readFileSync } from 'node:fs';
import { request } from 'node:http';
import { join, resolve, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';

const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '..');
const OUT = join(ROOT, 'out', 'bikemesa-rides');
mkdirSync(OUT, { recursive: true });
const argv = process.argv.slice(2);
const only = argv.filter((a) => !a.startsWith('--'));
// --trace N: also print the ride every N ticks (diagnosis: where the bike is when)
// --no-control: the rides alone (tuning a ride's timing; never a gate run)
const NO_CONTROL = argv.includes('--no-control');
const TRACE = argv.includes("--trace") ? +argv[argv.indexOf("--trace") + 1] : 0;
const only2 = only.filter((a) => !/^\d+$/.test(a));
const BACKENDS = argv.includes('--gl') ? ['gl'] : argv.includes('--webgpu') ? ['webgpu'] : ['gl', 'webgpu'];
const RIDES = JSON.parse(readFileSync(process.env.BIKEMESA_RIDES || join(ROOT, 'tests', 'bikemesa-rides.json'), 'utf8')).filter((r) => !only2.length || only2.includes(r.name));
if (only2.length && RIDES.length !== only2.length) { console.log(`FAIL no ride named ${only2.filter((n) => !RIDES.some((r) => r.name === n)).join(', ')}`); process.exit(1); }
const GS = { TITLE: 0, SELECT: 1, COUNT: 2, RIDE: 3, FINISH: 5, CHARACTER: 7 };
const AGENT = process.env.CW_ROMDEV_AGENT || 'bikemesa-rides';

let calls = 0, session = '';
function post(tool, a) {
  calls++;
  const body = JSON.stringify(a);
  return new Promise((ok, fail) => {
    const req = request({ host: ROMDEV_HOST, port: ROMDEV_PORT, path: `/tool/${tool}`, method: 'POST', agent: false,
      headers: { 'Content-Type': 'application/json', 'Content-Length': Buffer.byteLength(body), 'x-romdev-session': session, 'x-romdev-agent': AGENT } }, (res) => {
      let t = '';
      res.setEncoding('utf8');
      res.on('data', (c) => (t += c));
      res.on('end', () => {
        let b;
        try { b = JSON.parse(t); } catch { b = t; }
        if (res.statusCode >= 300 || (b && (b.isError || b.error))) return fail(new Error(`romdev ${tool}: ${t.slice(0, 600)}`));
        if (b && Array.isArray(b.content)) {
          const x = b.content.find((c) => c.type === 'text');
          try { return ok(JSON.parse(x.text)); } catch { return ok(x ? x.text : b); }
        }
        ok(b);
      });
    });
    req.setTimeout(900000, () => req.destroy(new Error(`romdev ${tool}: no reply in 900 s`)));
    req.on('error', fail);
    req.end(body);
  });
}
const shutdown = () => post('host', { op: 'shutdown' }).catch(() => {});
async function state() {
  const st = await post('wasm', { op: 'debugState' });
  const o = {};
  for (const f of st.fields || []) o[f.name] = f.values ?? f.value;
  return o;
}
/** a pad entry ({buttons, axes, pad2..pad4}) as romdev's ports */
function ports(e, n) {
  const one = (s) => {
    const p = {};
    for (const b of (s && s.buttons) || []) p[b === 'back' ? 'select' : b] = true;
    if (s && s.axes) p.axes = s.axes;
    return p;
  };
  const out = [one(e)];
  for (const k of ['pad2', 'pad3', 'pad4'].slice(0, n - 1)) out.push(one(e && e[k]));
  return out;
}
const hold = (steps) => post('input', { op: 'sequence', steps: steps.filter((s) => s.frames > 0) });
const press = (p, frames = 3, after = 14) => hold([{ input: { ports: p }, frames }, { input: { ports: p.map(() => ({})) }, frames: after }]);

/** step (with nothing held) until pred(state) holds, looking every `every` frames; the last state read */
async function waitFor(pred, what, cap, n, every = 10) {
  for (let f = 0; ; f += every) {
    const s = await state();
    if (pred(s)) return s;
    if (f >= cap) throw new Error(`gave up waiting for ${what} after ${cap} frames (game.state ${s['game.state']})`);
    await hold([{ input: { ports: ports(null, n) }, frames: every }]);
  }
}

let view = null; // cw.view: [logical w, h, picture w, h, x, y]: touch positions are the game's logical pixels
const fb = (t) => ({ x: Math.round((view ? view[4] : 0) + t.x * (view ? view[2] / view[0] : 1)), y: Math.round((view ? view[5] : 0) + t.y * (view ? view[3] / view[1] : 1)) });
async function tap(t) {
  const p = fb(t);
  await post('input', { op: 'pointer', id: 1, ...p, left: true, active: true });
  await hold([{ input: { ports: ports(null, 1) }, frames: 3 }]);
  await post('input', { op: 'pointer', id: 1, ...p, left: false, active: false });
  await hold([{ input: { ports: ports(null, 1) }, frames: 14 }]);
}

/** the menus as the screen shows them: the title, each seat's rider, bike, paint and READY, then track 1 */
async function menus(m) {
  const n = m.players;
  const all = (on) => Array.from({ length: n }, (_, i) => (on(i) ? { south: true } : {}));
  await hold([{ input: { ports: ports(null, n) }, frames: 24 }]); // the title takes a press after 20 ticks
  if (m.touch) await tap({ x: 480, y: 300 });
  else await press(all((i) => i === 0));
  await waitFor((s) => s['game.state'] === GS.CHARACTER, 'the rider pick', 300, n);
  if (n > 1) await press(all((i) => i > 0)); // pads 2.. join
  for (let k = 0; k < 3; k++) await press(all(() => true)); // rider, bike, paint: READY
  await waitFor((s) => s['game.state'] === GS.SELECT, 'the track select', 300, n);
  await hold([{ input: { ports: ports(null, n) }, frames: 12 }]); // the select takes a press after 10 ticks
  if (m.touch) await tap({ x: 162, y: 156 }); // track 1's tile
  else await press(all((i) => i === 0));
}

/**
 * one run: to the start line, then the input from GO for `ticks` ticks; the state at the end. A run with no menus
 * and no start requests (a control that never starts a ride) stays where it is for as long.
 */
async function ride(app, gpu, tag, r) {
  session = `bikemesa-rides-${process.pid}`;
  const n = r.menus ? r.menus.players : (r.startCmds || []).some((c) => /^players 2/.test(c)) ? 2 : Math.max(1, ...(r.input || []).map((e) => (e.pad4 ? 4 : e.pad3 ? 3 : e.pad2 ? 2 : 1)));
  const cart = join(ROOT, 'build', 'cart-wgpu', 'carts', `${app}.wasc`);
  const loaded = await post('loadMedia', { platform: 'wasmcart', path: cart, deterministicSeed: 1, deterministicStepMs: 1000 / 60, gpuPower: 'low-power', wgpu: gpu === 'webgpu' });
  if (loaded.gpu !== gpu) throw new Error(`the cart runs on ${loaded.gpu}, not ${gpu}`);
  try {
    await post('wasm', { op: 'command', text: 'test_run' });
    for (const c of r.startCmds || []) await post('wasm', { op: 'command', text: c });
    const v = (await state())['cw.view'];
    view = Array.isArray(v) && v[0] > 0 ? v : null;
    if (r.menus) await menus(r.menus);
    const starts = !!r.menus || (r.startCmds || []).some((c) => /^track /.test(c));
    let s;
    if (!starts) {
      await hold([{ input: { ports: ports(null, n) }, frames: r.idleFrames }]);
      s = await state();
    } else {
      s = await waitFor((x) => x['ride.start'] > 0, 'the countdown (ride.start)', 1200, n);
      const go = s['ride.start'], t0 = s.ticks;
      // the input from GO on: entry i held from tick go + at until the next entry (the last to the end)
      const input = (r.input || []).map((e) => ({ ...e, tick: go + e.at }));
      const late = input.filter((e, i) => e.tick < t0 && !(input[i + 1] && input[i + 1].tick <= t0));
      if (late.length && late[0].tick < t0 - 1) throw new Error(`reached the countdown at tick ${t0}, after the input's first entry (tick ${late[0].tick})`);
      const end = go + r.ticks;
      let at = t0, held = null, touches = new Map();
      const segs = []; // [{entry, from, to}]
      for (let i = 0; i < input.length; i++) {
        const from = Math.max(input[i].tick, t0), to = i + 1 < input.length ? Math.max(input[i + 1].tick, t0) : end;
        if (to > from) segs.push({ e: input[i], from, to });
      }
      if (!segs.length || segs[0].from > at) segs.unshift({ e: null, from: at, to: segs.length ? segs[0].from : end });
      // pad-only stretches go as one sequence; a touch change sends its pointers first (they reset the held pad)
      let batch = [];
      const flush = async () => { if (batch.length) await hold(batch); batch = []; };
      for (const g of segs) {
        const now = new Map(((g.e && g.e.touches) || []).map((t) => [t.id, t]));
        const changed = [...new Set([...touches.keys(), ...now.keys()])].filter((id) => JSON.stringify(touches.get(id)) !== JSON.stringify(now.get(id)));
        if (changed.length) {
          await flush();
          for (const id of changed) {
            const t = now.get(id) || touches.get(id);
            await post('input', { op: 'pointer', id, ...fb(t), left: now.has(id), active: now.has(id) });
          }
        }
        touches = now;
        held = g.e;
        if (!TRACE) batch.push({ input: { ports: ports(held, n) }, frames: g.to - g.from });
        else for (let f = g.from; f < g.to;) {
          const k = Math.min(g.to, (Math.floor((f - go) / TRACE) + 1) * TRACE + go) - f;
          batch.push({ input: { ports: ports(held, n) }, frames: k });
          f += k;
          if ((f - go) % TRACE === 0) { await flush(); const x = await state(); console.log(`  ${tag} +${f - go}: state ${x["game.state"]} pos ${(x["bike.pos"] || []).map((v) => v.toFixed(1))} vel ${(x["bike.velocity"] || []).map((v) => v.toFixed(1))} ang ${(+x["bike.angle"]).toFixed(2)} air ${x["air.max"]} land ${x.landings} crash ${x.crashes} flips ${x.flips} lanes ${x["race.lanes"]} rx ${(x["race.x"] || []).map((v) => v.toFixed(0))} rc ${x["race.crashes"]} hops ${x.hops} cp ${x.checkpoint}`); }
        }
        at = g.to;
      }
      await flush();
      for (const id of touches.keys()) await post('input', { op: 'pointer', id, ...fb(touches.get(id)), left: false, active: false });
      s = await state();
      if (s.ticks !== end) throw new Error(`ended on tick ${s.ticks}, not GO + ${r.ticks} = ${end}: a frame ran other than one tick`);
      // "until": the ride goes on (the last entry held) until the named state holds, looking every 60 frames, at most "cap"
      // frames more: an end that is a game state (the results) rather than a tick
      if (r.until) {
        const met = (x) => Object.entries(r.until).every(([k, v]) => same(x[k], v));
        for (let f = 0; !met(s); f += 60) {
          if (f >= (r.cap || 1200)) throw new Error(`gave up waiting for ${JSON.stringify(r.until)} ${r.cap || 1200} frames after GO + ${r.ticks}`);
          await hold([{ input: { ports: ports(held, n) }, frames: 60 }]);
          s = await state();
        }
      }
    }
    await post('frame', { op: 'screenshot', path: join(OUT, `${tag}.${gpu}.png`) });
    return s;
  } finally { await shutdown(); }
}

const same = (a, b) => JSON.stringify(a) === JSON.stringify(b);
const misses = (s, expect) => Object.entries(expect).filter(([k, v]) => !same(s[k], v)).map(([k, v]) => `${k} = ${JSON.stringify(s[k])}, expected ${JSON.stringify(v)}`);
let failed = 0, total = 0;
for (const r of RIDES) {
  const problems = [], notes = [];
  const before = calls;
  const legs = {};
  for (const gpu of BACKENDS) {
    try {
      const s = await ride(r.app, gpu, r.name, r);
      legs[gpu] = s;
      for (const m of misses(s, r.expect)) problems.push(`${gpu}: ${m}`);
      if (NO_CONTROL) { notes.push(`${gpu}: ${JSON.stringify(Object.fromEntries(Object.keys(r.expect).map((k) => [k, s[k]])))}`); continue; }
      const c = r.control;
      const cs = await ride(c.app || r.app, gpu, `${r.name}.control`, { ...r, ...c, menus: 'menus' in c ? c.menus : r.menus, startCmds: c.startCmds ?? r.startCmds, input: c.input ?? r.input });
      const cm = misses(cs, r.expect);
      if (!cm.length) problems.push(`CONTROL PASSED (${gpu}): the control meets every expected value`);
      else notes.push(`${gpu} control misses: ${cm.slice(0, 2).join('; ')}`);
    } catch (e) { problems.push(`${gpu}: ${e.message}`); }
  }
  if (legs.gl && legs.webgpu) {
    const keys = ['state.hash', 'bike.pos', 'ride.ticks', ...Object.keys(r.expect)];
    const d = keys.filter((k) => !same(legs.gl[k], legs.webgpu[k]));
    if (d.length) problems.push(`WebGPU vs GL: ${d.map((k) => `${k} ${JSON.stringify(legs.webgpu[k])} vs ${JSON.stringify(legs.gl[k])}`).join('; ')}`);
  }
  total++;
  const n = calls - before;
  if (NO_CONTROL && !problems.length) problems.push('--no-control: no control ran, not a gate result');
  if (problems.length) { failed++; console.log(`FAIL ${r.name} [romdev ${BACKENDS.join('+')}, ${n} requests]\n  ${problems.join('\n  ')}`); }
  else console.log(`ok   ${r.name} [romdev ${BACKENDS.join('+')}, ${n} requests]: ${notes.join('; ')}`);
}
console.log(`${total - failed} of ${total} rides ok, ${calls} romdev requests`);
process.exit(failed ? 1 : 0);
