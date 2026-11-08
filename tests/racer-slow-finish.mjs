// racer on the cart through romdev, GL and WebGPU: a newcomer who is slow still finishes the first race. From the title
// by the screen's own prompts (PRESS (A), RACE, READY, START RACE), then the first race driven the way a first-timer
// drives it: two and a half minutes at the lights before finding the gas, then the gas held, no steering of their own (the steering
// assist keeps the kart on the road). The computer field is home long before, more than 40 s ahead of the player; the
// race must still wait for the player and end with the player over the line. Time is the race's own clock (race.ticks):
// a cart's simulation follows the host's clock, so frames stepped are not race time. Control: sw dnf_any 1 (the race ends 40 s after the first kart
// home, a computer one too, as the game once did) must end the race with the player short of the line.
//   node tests/racer-slow-finish.mjs [--gl] [--webgpu] [--legs slow,control] [--cart <path>]
import { mkdirSync } from 'node:fs';
import { resolve, dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';

const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '..'), SESSION = process.env.RACER_SESSION || 'racer-slow-finish', OUT = join(ROOT, 'out/racer-slow-finish');
const arg = (n, d) => { const i = process.argv.indexOf(n); return i > 0 ? process.argv[i + 1] : d; };
const CART = resolve(arg('--cart', join(ROOT, 'build/cart-wgpu/carts/racer.wasc')));
const BACKENDS = process.argv.includes('--gl') ? ['gl'] : process.argv.includes('--webgpu') ? ['webgpu'] : ['gl', 'webgpu'];
const LEGS = arg('--legs', 'slow,control').split(',');
mkdirSync(OUT, { recursive: true });
// every exit tears this session's romdev host down, a failed run too: a host left loaded holds the server's memory
const bye = async (code) => {
  try { await fetch('http://127.0.0.1:7331/tool/host', { method: 'POST', headers: { 'Content-Type': 'application/json', 'x-romdev-session': SESSION }, body: '{"op":"shutdown"}', signal: AbortSignal.timeout(30000) }); } catch {}
  process.exit(code);
};
const tool = async (name, args) => {
  let r;
  try { r = await fetch(`http://127.0.0.1:7331/tool/${name}`, { method: 'POST', headers: { 'Content-Type': 'application/json', 'x-romdev-session': SESSION }, body: JSON.stringify(args) }); }
  catch (e) { console.error(`romdev ${name}: ${e.cause?.message || e.message} (the server is down)`); await bye(3); }
  const t = await r.text();
  let j; try { j = JSON.parse(t); } catch { j = t; }
  if (!r.ok || j?.error) { console.error(`romdev ${name}: ${t.slice(0, 400)}`); await bye(3); }
  return j;
};
const rd = async (n) => { const v = await tool('wasm', { op: 'read', name: n }); return v.values ?? v.value; };
const step = (frames) => tool('frame', { op: 'step', frames });
const pad = (p) => tool('input', { op: 'set', ports: [p, {}] });
const press = async (b) => { await pad({ [b]: true }); await step(4); await pad({}); await step(26); };
const M = { MAP: 3, RACE: 6 }, PH_RACE = 2, PH_DONE = 3, TICK_HZ = 60;
let fail = 0;
const check = (name, ok, detail) => { console.log(`${ok ? 'ok  ' : 'FAIL'} ${name}: ${detail}`); if (!ok) fail++; };

async function run(backend, tag, pre) {
  const l = await tool('loadMedia', { platform: 'wasmcart', path: CART, gpuPower: 'low-power', ...(backend === 'gl' ? { wgpu: false } : {}) });
  if (!l.loaded) { console.error(JSON.stringify(l).slice(0, 300)); await bye(3); }
  await step(180);
  if (pre) await tool('wasm', { op: 'command', text: pre });
  await press('south');                                        /* "PRESS (A)" */
  await press('south');                                        /* RACE */
  await press('south');                                        /* "(A) READY" */
  await step(30);
  /* the first map, as on a fresh save (romdev keeps the cart's save, which remembers the last map raced) */
  for (let i = 0; i < 12 && +(await rd('ui.mode')) === M.MAP && +(await rd('ui.map')) !== 0; i++) await press('left');
  if (+(await rd('ui.mode')) === M.MAP) await press('south');  /* "START RACE" */
  let mode = 0, phase = 0;
  for (let f = 0; f < 1500 && !(mode === M.RACE && phase === PH_RACE); f += 30) { await step(30); mode = +(await rd('ui.mode')); phase = +(await rd('race.phase')); }
  if (mode !== M.RACE || phase !== PH_RACE) { console.error(`no race started (mode ${mode}, phase ${phase})`); await bye(3); }
  /* two and a half minutes at the lights, then the gas held until the race is over (12 minutes of race at most) */
  let t = +(await rd('race.ticks'));
  while (t < 150 * TICK_HZ) { await step(60); t = +(await rd('race.ticks')); }
  await pad({ south: true });
  while (t < 12 * 60 * TICK_HZ && phase !== PH_DONE) { await step(120); phase = +(await rd('race.phase')); t = +(await rd('race.ticks')); }
  await pad({});
  const fin = await rd('karts.finished'), ft = await rd('karts.finish_tick'), lap = await rd('karts.lap');
  const cpu = ft.slice(1).filter((v, i) => fin[i + 1]);
  await tool('frame', { op: 'stepAndShot', frames: 1, path: join(OUT, `${backend}-${tag}.png`) });
  return { phase, done: !!fin[0], lap: lap[0], mine: ft[0], firstCpu: cpu.length ? Math.min(...cpu) : 0, secs: t / TICK_HZ };
}

for (const backend of BACKENDS) {
  console.log(`--- ${backend}`);
  if (LEGS.includes('slow')) {
    const r = await run(backend, 'slow');
    check('a slow newcomer finishes the first race', r.phase === PH_DONE && r.done, `race phase ${r.phase} after ${r.secs.toFixed(0)} s, player finished ${r.done} on lap ${r.lap}`);
    check('the computer field was home more than 40 s before the player', r.done && r.firstCpu > 0 && r.mine - r.firstCpu > 40 * TICK_HZ,
      `first computer kart home at ${(r.firstCpu / TICK_HZ).toFixed(1)} s, the player at ${(r.mine / TICK_HZ).toFixed(1)} s`);
  }
  if (LEGS.includes('control')) {
    const c = await run(backend, 'control', 'sw dnf_any 1');
    check('control: sw dnf_any 1, the race ends with the player short of the line', c.phase === PH_DONE && !c.done,
      `race phase ${c.phase} after ${c.secs.toFixed(0)} s, player finished ${c.done} on lap ${c.lap}, first computer kart home at ${(c.firstCpu / TICK_HZ).toFixed(1)} s`);
  }
}
console.log(fail ? `${fail} FAILED` : 'all ok');
await bye(fail ? 1 : 0);
