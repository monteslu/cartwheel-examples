// chromite on the cart through romdev, GL and WebGPU, by the pad: a newcomer powers on, reads the title's prompt
// ({fire} : launch), launches, and plays level 1 by what the screen draws (the craft and the enemy rounds, read from
// the view.enemies and view.rounds fields the world draws them from, and its own ship): it holds fire, slides under
// the lowest craft and sidesteps rounds coming down its column, looking every 10 frames as a first-timer reacts.
// When its last ship goes it takes the results card's "{fire} : continue from this level". It must reach level 2
// within six minutes of play.
// The second leg is the idle seat: chromite is one player; a second pad that presses Start on the title and then
// nothing must not hold anything up (the game launches and the first pad plays it through).
// Control: sw rules 1 (classic, the original's rule: every enemy that gets past costs a ship) leaves the same
// newcomer short of level 2.
//   node tests/chromite-newcomer.mjs [--gl] [--webgpu] [--cart <path>] [--no-control] [--leg newcomer|control]
import { mkdirSync } from 'node:fs';
import { resolve, dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';

const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '..'), SESSION = process.env.CHROMITE_SESSION || 'chromite-newcomer';
const OUT = join(ROOT, 'out/chromite-newcomer');
const arg = (n, d) => { const i = process.argv.indexOf(n); return i > 0 ? process.argv[i + 1] : d; };
const CART = resolve(arg('--cart', join(ROOT, 'build/cart-wgpu/carts/chromite.wasc')));
const BACKENDS = process.argv.includes('--gl') ? ['gl'] : process.argv.includes('--webgpu') ? ['webgpu'] : ['gl', 'webgpu'];
const LIMIT = +arg("--frames", 21600); /* six minutes */
const LOOK = 10; /* frames between looks: a first-timer reacts in a sixth of a second */
mkdirSync(OUT, { recursive: true });
const bye = async (code) => {
  try { await fetch('http://127.0.0.1:7331/tool/host', { method: 'POST', headers: { 'Content-Type': 'application/json', 'x-romdev-session': SESSION }, body: '{"op":"shutdown"}', signal: AbortSignal.timeout(30000) }); } catch {}
  process.exit(code);
};
const tool = async (name, args) => {
  const r = await fetch(`http://127.0.0.1:7331/tool/${name}`, { method: 'POST', headers: { 'Content-Type': 'application/json', 'x-romdev-session': SESSION }, body: JSON.stringify(args) });
  const t = await r.text();
  let j; try { j = JSON.parse(t); } catch { j = t; }
  if (!r.ok || j?.error) { console.error(`romdev ${name}: ${t.slice(0, 400)}`); await bye(3); }
  return j;
};
const rd = async (n) => { const v = await tool('wasm', { op: 'read', name: n }); return v.values ?? v.value; };
/* several debug fields in one request (the built-in fields command) */
let fieldsOk = true;
const fields = async (...names) => {
  if (fieldsOk) {
    const r = await tool('wasm', { op: 'command', text: 'fields ' + names.join(' ') });
    let j = r.reply ?? r.result ?? r;
    if (typeof j === 'string') try { j = JSON.parse(j); } catch { j = {}; }
    j = j.fields ?? j;
    if (names.every((n) => n in j)) return j;
    fieldsOk = false; /* a host that answers otherwise: one read a field */
    console.log('  (fields command answered', JSON.stringify(r).slice(0, 200), ': reading one at a time)');
  }
  const out = {};
  for (const n of names) out[n] = await rd(n);
  return out;
};
const step = (frames) => tool('frame', { op: 'step', frames });
const pads = (p0, p1 = {}) => tool('input', { op: 'set', ports: [p0, p1] });
let fail = 0;
const check = (name, ok, detail) => { console.log(`${ok ? 'ok  ' : 'FAIL'} ${name}: ${detail}`); if (!ok) fail++; };



async function play(backend, tag, { pre, idle } = {}) {
  const l = await tool('loadMedia', { platform: 'wasmcart', path: CART, gpuPower: 'low-power', ...(backend === 'gl' ? { wgpu: false } : {}) });
  if (!l.loaded) { console.error(JSON.stringify(l).slice(0, 300)); await bye(3); }
  await step(150);
  if (pre) await tool('wasm', { op: 'command', text: pre });
  if (idle) { await pads({}, { start: true }); await step(4); await pads({}, {}); await step(30); }
  const titleState = await rd('game.state');
  await tool('frame', { op: 'stepAndShot', frames: 1, path: join(OUT, `${backend}-${tag}-title.png`) });
  await pads({ south: true }); await step(4); await pads({}); await step(20); /* the prompt: {fire} : launch */
  const started = await rd('game.state');
  let continues = 0, frames = 0, tx = 0, reached = false, over = false, shots = 0, lastLog = 0;
  while (frames < LIMIT) {
    const f = await fields('game.state', 'level', 'hero.pos', 'view.enemies', 'view.rounds');
    const st = +[].concat(f['game.state'])[0], level = +[].concat(f.level)[0];
    if (level >= 2) { reached = true; break; }
    if (st === 4) {
      /* the results card: under the modern rules it reads "{fire} : continue from this level"; the newcomer takes it */
      if (+(await rd('rules')) === 0 && continues < 9) { await step(90); await pads({ south: true }); await step(4); await pads({}); await step(20); continues++; continue; }
      over = true; break;
    }
    if (st === 2) { await pads({ start: true }); await step(4); await pads({}); await step(4); continue; } /* never pauses on purpose */
    if (st !== 1) { await step(LOOK); frames += LOOK; continue; }
    const hp = f['hero.pos'], en = f['view.enemies'], rn = f['view.rounds'];
    /* the target: the lowest craft on the screen */
    let best = null;
    for (let i = 0; i < en.length; i += 2) if (en[i] < 90 && en[i + 1] > hp[1] + 1 && (!best || en[i + 1] < best[1])) best = [en[i], en[i + 1]];
    if (best) tx = best[0];
    /* danger: rounds in the column above the ship, weighted by nearness; sidestep away from them */
    let push = 0;
    for (let i = 0; i < rn.length; i += 2) {
      if (rn[i] > 90) break;
      const dx = rn[i] - hp[0], dy = rn[i + 1] - hp[1];
      if (Math.abs(dx) > 1.8 || dy < -0.5 || dy > 5) continue;
      push += (dx > 0 ? -1 : 1) * (1 - Math.abs(dx) / 1.8) * (1 - dy / 5.5);
    }
    const steer = Math.abs(push) > 0.3 ? Math.sign(push) : Math.max(-1, Math.min(1, (tx - hp[0]) * 0.8));
    await pads({ south: true, axes: { lx: steer, ly: Math.max(-1, Math.min(1, (hp[1] - (-4.5)) * 0.5)) } });
    await step(LOOK);
    frames += LOOK;
    if (frames - lastLog >= 1800) { lastLog = frames; console.log(`  ${backend} ${tag} f ${frames} lives ${await rd('lives')} kills ${await rd('kills')} escapes ${await rd('escapes')} losses ${await rd('losses')} score ${await rd('score')}`); }
    if (++shots % 300 === 0) await tool('frame', { op: 'stepAndShot', frames: 1, path: join(OUT, `${backend}-${tag}-${frames}.png`) });
  }
  await pads({});
  await tool('frame', { op: 'stepAndShot', frames: 1, path: join(OUT, `${backend}-${tag}-end.png`) });
  return { titleState, started, reached, over, frames, continues, lives: await rd('lives'), kills: await rd('kills'), losses: await rd('losses'), escapes: await rd('escapes') };
}

/* a GPU suite holds the lock 15 minutes: --leg newcomer or --leg control runs one leg (default both) */
const LEG = arg('--leg', 'both');
for (const backend of BACKENDS) {
  console.log(`--- ${backend}`);
  if (LEG !== 'control') {
  const n = await play(backend, 'newcomer', { idle: true });
  check('newcomer (a second pad pressed Start on the title, then idled): launches from the prompt and reaches level 2', n.started === 1 && n.reached,
    `title ${n.titleState}, launched ${n.started}, level 2 ${n.reached} at ${n.frames} frames, continues ${n.continues}, game over ${n.over}, lives ${n.lives}, kills ${n.kills}, escapes ${n.escapes}, losses ${n.losses}`);
  }
  if (process.argv.includes('--no-control') || LEG === 'newcomer') continue;
  const c = await play(backend, 'control', { pre: 'sw rules 1' });
  check('control (sw rules 1, classic): the same newcomer falls short of level 2', !c.reached,
    `level 2 ${c.reached} at ${c.frames} frames, game over ${c.over}, lives ${c.lives}, losses ${c.losses}`);
}
console.log(fail ? `${fail} FAILED` : 'all ok');
await bye(fail ? 1 : 0);
