// chromite's newcomer gate on the cart through romdev, GL and WebGPU: a newcomer who makes a person's mistakes beats
// level 1, its command craft included, on the default skill and rules.
//
// The newcomer plays by what the screen draws (the craft and the enemy rounds, read from the view.enemies and
// view.rounds fields the world draws them from, and its own ship) and errs as a person does (games/chromite/study.c
// has the same bot in C, with the same numbers):
//   it looks every 6 frames, seeing the rounds twice a frame apart (how they move), and acts on what it saw two
//   looks before: a reaction of 12 to 18 frames, a fifth to a third of a second, guessing most of the way where a
//   round has gone since; it aims at the lowest craft with an error of about 0.7 field units (a craft is 1.4 to 7
//   wide) that it corrects only now and then, a little at a time; it misses one round in ten it could have seen;
//   it holds fire, never self-destructs and never goes for a power-up. It weighs fifteen stick moves over the next
//   36 frames against the rounds it saw, a ram, the aim, and flying low.
// It beats level 1 when it clears it taking at most one continue (the results card's "{fire} : continue"); a run
// that needs a second continue has hit a wall and stops there.
//
// Legs:
//   study    `study newcomer 20` in the cart: twenty games of level 1 without drawing, one seed each for the
//            newcomer's mistakes. At least 60% must beat it. Control: sw old_boss 1 (the original's first command
//            craft, whose beam fires every frame the ship is under it): at most 10% beat it.
//   pad      the same newcomer at the pad from power-on: it reads the title's prompt ({fire} : launch), launches and
//            plays, on --seeds seeds (default 3; the first with a second pad that pressed Start on the title and then
//            idled: chromite is one player and an idle seat holds nothing up). At least half must beat level 1.
//   release  the pad leg on --control-cart (the release before this change, v0.20.1): fewer than half beat it.
//   node tests/chromite-newcomer.mjs [--gl] [--webgpu] [--cart <path>] [--control-cart <path>] [--seeds n]
//                                    [--first-seed n] [--leg study|pad|release|all]
// (a run at the pad takes about as long as the play it makes on a slow host: --seeds and --first-seed split the legs)
import { mkdirSync } from 'node:fs';
import { resolve, dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';

const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '..'), SESSION = process.env.CHROMITE_SESSION || 'chromite-newcomer';
const OUT = join(ROOT, 'out/chromite-newcomer');
const arg = (n, d) => { const i = process.argv.indexOf(n); return i > 0 ? process.argv[i + 1] : d; };
const CART = resolve(arg('--cart', join(ROOT, 'build/cart-wgpu/carts/chromite.wasc')));
const CONTROL_CART = arg('--control-cart', '') && resolve(arg('--control-cart'));
const BACKENDS = process.argv.includes('--gl') ? ['gl'] : process.argv.includes('--webgpu') ? ['webgpu'] : ['gl', 'webgpu'];
const SEEDS = +arg('--seeds', 3), FIRST = +arg('--first-seed', 1), LEG = arg('--leg', 'all');
const LIMIT = 60 * 60 * 9; /* frames of play a run may take */
mkdirSync(OUT, { recursive: true });
let requests = 0; /* romdev requests, printed at the end */
const HEADERS = { 'Content-Type': 'application/json', 'x-romdev-session': SESSION, 'x-romdev-agent': process.env.ROMDEV_AGENT || 'chromite-newcomer' };
const bye = async (code) => {
  try { await fetch('http://127.0.0.1:7331/tool/host', { method: 'POST', headers: HEADERS, body: '{"op":"shutdown"}', signal: AbortSignal.timeout(30000) }); } catch {}
  process.exit(code);
};
const tool = async (name, args) => {
  requests++;
  const r = await fetch(`http://127.0.0.1:7331/tool/${name}`, { method: 'POST', headers: HEADERS, body: JSON.stringify(args), signal: AbortSignal.timeout(300000) });
  const t = await r.text();
  let j; try { j = JSON.parse(t); } catch { j = t; }
  if (!r.ok || j?.error) { console.error(`romdev ${name}: ${t.slice(0, 400)}`); await bye(3); }
  return j;
};
const rd = async (n) => { const v = await tool('wasm', { op: 'read', name: n }); return v.values ?? v.value; };
const cmd = async (text) => {
  const j = await tool('wasm', { op: 'command', text });
  const v = j.reply ?? j.result ?? j.value ?? j;
  return typeof v === 'string' ? JSON.parse(v) : v;
};
/* several debug fields in one request (the built-in fields command) */
let fieldsOk = true;
const fields = async (...names) => {
  if (fieldsOk) {
    let j = await cmd('fields ' + names.join(' '));
    j = j.fields ?? j;
    if (names.every((n) => n in j)) return j;
    fieldsOk = false; /* a host that answers otherwise: one read a field */
    console.log('  (fields command answered', JSON.stringify(j).slice(0, 200), ': reading one at a time)');
  }
  const out = {};
  for (const n of names) out[n] = await rd(n);
  return out;
};
const step = (frames) => tool('frame', { op: 'step', frames });
const pads = (p0, p1 = {}) => tool('input', { op: 'set', ports: [p0, p1] });
/* ranges, not loops: a decision is one read (the fields command: the rounds now and a tick before, view.rounds_prev) and
   one request that holds the stick for the look's frames and steps them; a press is one request too */
const seq = (...steps) => tool('input', { op: 'sequence', steps: steps.map(([p0, frames, p1 = {}]) => ({ input: { ports: [p0, p1] }, frames })) });
let fail = 0;
const check = (name, ok, detail) => { console.log(`${ok ? 'ok  ' : 'FAIL'} ${name}: ${detail}`); if (!ok) fail++; };

// ── the newcomer: games/chromite/study.c's newcomer_* step for step ──
const VIEW_N = 24, LOOK = 6, DELAY_LOOKS = 2, HIST = 4, LEAD = 0.7, AIM_SIGMA = 0.7, AIM_HOLD = 40, MISS = 0.1, HOME_Y = -4.5;
const K = 50 / 60, SPEED = 8 * 0.03 * K, HERO_X = 10, HERO_Y = 7.5;
const clamp = (v, a, b) => Math.min(b, Math.max(a, v));
class Newcomer {
  constructor(seed) {
    this.rng = (Math.imul(seed, 2654435761) + 0x9e3779b9) >>> 0 || 1;
    for (let i = 0; i < 4; i++) this.next();
    this.looks = 0, this.tx = 0, this.hx = 0, this.hy = 0;
    this.en = [], this.rn = [];
    this.aimErr = this.gauss() * AIM_SIGMA;
  }
  next() { let x = this.rng; x ^= x << 13; x >>>= 0; x ^= x >>> 17; x ^= x << 5; return (this.rng = x >>> 0); }
  unit() { return (this.next() >>> 8) / 16777216; }
  gauss() { return (this.unit() + this.unit() + this.unit() - 1.5) * 2; }
  /* gap: ticks between the two views (1 in the study; a host may run more than one tick a frame) */
  see(en, ra, rb, hx, hy, gap = 1) {
    const k = this.looks % HIST, r = [];
    for (let i = 0; i < VIEW_N; i++) {
      const o = [rb[i * 2], rb[i * 2 + 1], 0, -0.2 * K];
      if (o[0] <= 90) {
        let best = 1e9;
        for (let j = 0; j < VIEW_N; j++) {
          if (ra[j * 2] > 90) break;
          const dx = o[0] - ra[j * 2], dy = o[1] - ra[j * 2 + 1], d2 = dx * dx + dy * dy;
          if (d2 < 0.12 * 0.12 * gap * gap || d2 > 0.65 * 0.65 * gap * gap || d2 >= best) continue;
          best = d2, o[2] = dx / gap, o[3] = dy / gap;
        }
      }
      r.push(o);
    }
    this.en[k] = en.slice(), this.rn[k] = r, this.hx = hx, this.hy = hy;
    this.looks++;
  }
  decide() {
    if (this.looks <= DELAY_LOOKS) return [0, 0];
    const k = (this.looks - 1 - DELAY_LOOKS) % HIST, en = this.en[k], rn = this.rn[k], hx = this.hx, hy = this.hy;
    if (this.unit() < 1 / AIM_HOLD) this.aimErr = this.aimErr * 0.4 + this.gauss() * AIM_SIGMA;
    let bx = 99, by = 99;
    for (let i = 0; i < VIEW_N; i++) if (en[i * 2] < 90 && en[i * 2 + 1] > hy + 1 && en[i * 2 + 1] < by) bx = en[i * 2], by = en[i * 2 + 1];
    if (bx < 90) this.tx = bx + this.aimErr;
    const ahead = DELAY_LOOKS * LOOK * LEAD, R = [];
    for (const r of rn) {
      if (r[0] > 90) break;
      if (this.unit() < MISS) continue;
      R.push([r[0] + r[2] * ahead, r[1] + r[3] * ahead, r[2], r[3]]);
    }
    let best = 1e30, sx = 0, sy = 0;
    for (let cx = -2; cx <= 2; cx++)
      for (let cy = -1; cy <= 1; cy++) {
        let mx = cx * 0.5, my = cy;
        const l = Math.hypot(mx, my);
        if (l > 1) mx /= l, my /= l;
        let danger = 0;
        for (let t = 0; t <= 36; t += 3) {
          const x = clamp(hx + mx * SPEED * t, -HERO_X, HERO_X), y = clamp(hy + my * SPEED * t, -HERO_Y, HERO_Y);
          for (const [px, py, vx, vy] of R) {
            const d = Math.abs(px + vx * t - x) + Math.abs(py + vy * t - y);
            if (d < 1.3) danger += (1.3 - d) * (1 - t / 45);
          }
          for (let i = 0; i < VIEW_N; i++) {
            if (en[i * 2] > 90) continue;
            const d = Math.abs(en[i * 2] - x) + Math.abs(en[i * 2 + 1] - y);
            if (d < 2) danger += (2 - d) * 0.5;
          }
        }
        const fx = hx + mx * SPEED * 12, fy = hy + my * SPEED * 12;
        const cost = danger * 10 + Math.abs(fx - this.tx) * 0.3 + Math.abs(fy - HOME_Y) * 0.15;
        if (cost < best) best = cost, sx = mx, sy = my;
      }
    return [sx, sy];
  }
}

async function load(cart, backend) {
  const l = await tool('loadMedia', { platform: 'wasmcart', path: cart, gpuPower: 'low-power', ...(backend === 'gl' ? { wgpu: false } : {}) });
  if (!l.loaded) { console.error(JSON.stringify(l).slice(0, 300)); await bye(3); }
  await step(150);
}

/* one newcomer from power-on to level 2, a second continue or the clock */
async function play(cart, backend, tag, seed, idle) {
  await load(cart, backend);
  await cmd('sw rules 0'); /* the default rules, whatever an earlier run saved */
  if (idle) await seq([{}, 4, { start: true }], [{}, 30]);
  const titleState = await rd('game.state');
  await tool('frame', { op: 'stepAndShot', frames: 1, path: join(OUT, `${backend}-${tag}-title.png`) });
  await seq([{ south: true }, 4], [{}, 20]); /* the prompt: {fire} : launch */
  const started = await rd('game.state');
  const bot = new Newcomer(seed);
  let tpf = 1, lastTick = -1, lastStep = 0; /* ticks a frame, measured: a host can run more than one tick a frame, so a look spans LOOK ticks */
  let continues = 0, frames = 0, reached = false, wall = false, lastLog = 0, sx = 0, sy = 0, shots = 0, bossAt = -1;
  /* the stick as a thumb works it: the game's movement actions start past half a tilt (input.cfg's default dead zone, 0.5,
     rescaled to 0..1 above it), so a move of strength s is a tilt of 0.5 + 0.5 s; the study (study.c) hands s to the sim */
  const tilt = (s) => (s === 0 ? 0 : Math.sign(s) * (0.5 + 0.5 * Math.abs(s)));
  const stick = () => ({ south: true, axes: { lx: tilt(sx), ly: tilt(-sy) } });
  while (frames < LIMIT) {
    const b = await fields('game.state', 'level', 'hero.pos', 'view.enemies', 'view.rounds_prev', 'view.rounds', 'boss.plates', 'ticks');
    const tick = +[].concat(b.ticks)[0];
    if (lastTick >= 0 && lastStep > 0 && tick > lastTick) tpf = tpf * 0.8 + ((tick - lastTick) / lastStep) * 0.2;
    lastTick = tick, lastStep = 0;
    const st = +[].concat(b['game.state'])[0];
    if (st === 4) {
      /* the results card: under the modern rules it reads "{fire} : continue"; a second one is the wall */
      if (continues >= 1) { wall = true; break; }
      await seq([{}, 90], [{ south: true }, 4], [{}, 20]); continues++;
      continue;
    }
    if (st === 2) { await seq([{ start: true }, 4], [{}, 4]); continue; } /* never pauses on purpose */
    if (st !== 1) { await step(LOOK); frames += LOOK; continue; }
    if (+[].concat(b.level)[0] >= 2) { reached = true; break; }
    if (bossAt < 0 && +[].concat(b['boss.plates'])[0] >= 0) bossAt = frames;
    const hp = b['hero.pos'];
    /* the rounds a tick apart, as the game kept them (view.rounds_prev is the tick before view.rounds) */
    bot.see(b['view.enemies'], b['view.rounds_prev'], b['view.rounds'], hp[0], hp[1], 1);
    [sx, sy] = bot.decide();
    const n = Math.max(1, Math.round(LOOK / tpf));
    await seq([stick(), n]);
    frames += n, lastStep = n;
    if (frames - lastLog >= 1200) { lastLog = frames; console.log(`  ${backend} ${tag} f ${frames} ticks ${await rd('ticks')} lives ${await rd('lives')} kills ${await rd('kills')} escapes ${await rd('escapes')} continues ${continues} boss plates ${[].concat(b['boss.plates'])[0]}`); }
    if (++shots % 600 === 0) await tool('frame', { op: 'stepAndShot', frames: 1, path: join(OUT, `${backend}-${tag}-${frames}.png`) });
  }
  await pads({});
  await tool('frame', { op: 'stepAndShot', frames: 1, path: join(OUT, `${backend}-${tag}-end.png`) });
  const r = { titleState, started, reached, wall, frames, continues, bossAt, lives: await rd('lives'), kills: await rd('kills') };
  console.log(`  ${tag}: title ${r.titleState}, launched ${r.started}, level 1 beaten ${r.reached} at ${r.frames} frames (boss at ${r.bossAt}), continues ${r.continues}${r.wall ? ', stopped at a second continue' : ''}, lives ${r.lives}, kills ${r.kills}`);
  return r;
}

const beat = (runs) => runs.filter((r) => r.clear >= 0 && r.continues <= 1).length;
for (const backend of BACKENDS) {
  console.log(`--- ${backend}`);
  if (LEG === 'all' || LEG === 'study') {
    await load(CART, backend);
    await cmd('sw old_boss 0');
    const s = (await cmd('study newcomer 20')).runs;
    const n = beat(s), one = s.filter((r) => r.clear >= 0 && r.continues === 0).length;
    const mean = (k) => (s.reduce((a, r) => a + r[k], 0) / s.length).toFixed(1);
    check('study: 20 newcomers beat level 1 taking at most one continue', n >= 12,
      `${n}/20 (${one} on one credit); deaths a run: ${mean('wave_deaths')} in the waves, ${mean('boss_deaths')} at the command craft`);
    await cmd('sw old_boss 1');
    const c = (await cmd('study newcomer 20')).runs, nc = beat(c);
    const meanc = (k) => (c.reduce((a, r) => a + r[k], 0) / c.length).toFixed(1);
    check('control (sw old_boss 1, the original command craft): the same newcomers hit the wall', nc <= 2,
      `${nc}/20 beat it; deaths a run: ${meanc('wave_deaths')} in the waves, ${meanc('boss_deaths')} at the command craft`);
    await cmd('sw old_boss 0');
  }
  if (LEG === 'all' || LEG === 'pad') {
    const runs = [];
    for (let seed = FIRST; seed < FIRST + SEEDS; seed++) runs.push(await play(CART, backend, `newcomer-${seed}`, seed, seed === 1));
    const n = runs.filter((r) => r.reached).length;
    check(`pad: newcomers from power-on (seed 1 beside an idle second pad) beat level 1`, runs.every((r) => r.started === 1) && n * 2 >= SEEDS,
      `${n}/${SEEDS} beat it; continues ${runs.map((r) => r.continues).join(', ')}`);
  }
  if ((LEG === 'all' || LEG === 'release') && CONTROL_CART) {
    const runs = [];
    for (let seed = FIRST; seed < FIRST + SEEDS; seed++) runs.push(await play(CONTROL_CART, backend, `release-${seed}`, seed, false));
    const n = runs.filter((r) => r.reached).length;
    check(`control (${CONTROL_CART.split('/').pop()}): the same newcomers hit the wall`, n * 2 < SEEDS,
      `${n}/${SEEDS} beat it; continues ${runs.map((r) => r.continues).join(', ')}`);
  }
}
console.log(`romdev requests: ${requests}`);
console.log(fail ? `${fail} FAILED` : 'all ok');
await bye(fail ? 1 : 0);
