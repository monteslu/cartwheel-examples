// bomberdude on the cart through romdev, GL and WebGPU, by the pad, as a newcomer plays it: from power-on, the
// title's QUICK BATTLE (one press), then at GO the controls line names {bomb}, so the newcomer drops a bomb and stands
// on it (the classic first mistake) and is knocked out. A knocked-out person rides the rim: the screen says how
// ("ride the wall ... throw a bomb in: {bomb} ... hit someone to swap back in", debug field hud.ride_hint names the
// player it is drawn for). The run waits for that line, reads it for a second, presses the button it names and must
// have thrown a bomb within ten seconds of the knockout, then the round must end. With a second pad that joined on the
// players screen (BATTLE) and is never touched, the round still ends and P1 still throws.
// Control: ride_hint 0 (a knocked-out person is told nothing, as the game once left them) must leave the newcomer,
// who only does what the screen says, without a single throw.
//   node tests/bomberdude-newcomer.mjs [--gl] [--webgpu] [--cart <path>]
import { ROMDEV_URL } from './lib/romdev-url.mjs';
import { mkdirSync } from 'node:fs';
import { resolve, dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';

const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '..'), SESSION = process.env.BOMBERDUDE_SESSION || 'bomberdude-newcomer';
const OUT = join(ROOT, 'out/bomberdude-newcomer');
const arg = (n, d) => { const i = process.argv.indexOf(n); return i > 0 ? process.argv[i + 1] : d; };
const CART = resolve(arg('--cart', join(ROOT, 'build/cart-wgpu/carts/bomberdude.wasc')));
const BACKENDS = process.argv.includes('--gl') ? ['gl'] : process.argv.includes('--webgpu') ? ['webgpu'] : ['gl', 'webgpu'];
mkdirSync(OUT, { recursive: true });
// every exit tears this session's romdev host down, a failed run too: a host left loaded holds the server's memory
const bye = async (code) => {
  try { await fetch(ROMDEV_URL+'/tool/host', { method: 'POST', headers: { 'Content-Type': 'application/json', 'x-romdev-session': SESSION }, body: '{"op":"shutdown"}', signal: AbortSignal.timeout(30000) }); } catch {}
  process.exit(code);
};
const tool = async (name, args) => {
  const r = await fetch(`${ROMDEV_URL}/tool/${name}`, { method: 'POST', headers: { 'Content-Type': 'application/json', 'x-romdev-session': SESSION }, body: JSON.stringify(args) });
  const t = await r.text();
  let j; try { j = JSON.parse(t); } catch { j = t; }
  if (!r.ok || j?.error) { console.error(`romdev ${name}: ${t.slice(0, 400)}`); await bye(3); }
  return j;
};
const rd = async (n) => { const v = await tool('wasm', { op: 'read', name: n }); return v.values ?? v.value; };
const step = (frames) => tool('frame', { op: 'step', frames });
const pads = (p0, p1 = {}) => tool('input', { op: 'set', ports: [p0, p1] });
/* ranges, not loops: the pad held for a stretch of frames and those frames stepped, in one request */
const hold = (frames, p0, p1 = {}) => tool('input', { op: 'sequence', steps: [{ input: { ports: [p0, p1] }, frames }] });
const press = async (port, b) => { const a = [{}, {}]; a[port] = { [b]: true }; await hold(4, ...a); await hold(20, {}, {}); };
const GS_PLAY = 4, GS_ROUND_END = 5;
let fail = 0;
const check = (name, ok, detail) => { console.log(`${ok ? 'ok  ' : 'FAIL'} ${name}: ${detail}`); if (!ok) fail++; };
const until = async (cond, max, every = 10) => { for (let f = 0; f < max; f += every) { if (await cond()) return f; await step(every); } return -1; };

async function run(backend, tag, idle, pre) {
  const l = await tool('loadMedia', { platform: 'wasmcart', path: CART, gpuPower: process.env.CW_GPU_POWER || 'low-power', ...(backend === 'gl' ? { wgpu: false } : {}) });
  if (!l.loaded) { console.error(JSON.stringify(l).slice(0, 300)); await bye(3); }
  await step(240);
  if (pre) await tool('wasm', { op: 'command', text: pre });
  if (idle) {                                               /* BATTLE, a second pad joins on the players screen, FIGHT, arena 1 */
    await press(0, 'right'); await press(0, 'south');
    await press(1, 'south');                                /* "pads 2-4 press A to join", then never touched again */
    await press(0, 'south'); await press(0, 'south');
  } else await press(0, 'south');                           /* QUICK BATTLE, selected on the title */
  const toPlay = await until(async () => +(await rd('game.state')) === GS_PLAY, 600);
  await step(30);                                           /* GO: the controls line names {bomb} */
  await press(0, 'south');                                  /* a bomb, dropped where the newcomer stands, and stays */
  const ko = await until(async () => (await rd('players.riding'))[0] === 1, 600);
  const koTick = +(await rd('round.tick'));
  const hinted = await until(async () => +(await rd('hud.ride_hint')) === 0, 240);
  let thrown = -1;
  if (hinted >= 0) {
    await step(60);                                         /* reads the line */
    await tool('frame', { op: 'stepAndShot', frames: 1, path: join(OUT, `${backend}-${tag}-hint.png`) });
    await press(0, 'south');                                /* the button it names: a throw */
    await step(36);
    await tool('frame', { op: 'stepAndShot', frames: 1, path: join(OUT, `${backend}-${tag}-throw.png`) });
    thrown = (await rd('players.throws'))[0];
  } else await step(300);
  const throwTick = +(await rd('round.tick'));
  const throws = (await rd('players.throws'))[0];
  const end = await until(async () => +(await rd('game.state')) === GS_ROUND_END, 13000, 60);
  const idleMax = +(await rd('round.idle_max'));
  await tool('frame', { op: 'stepAndShot', frames: 1, path: join(OUT, `${backend}-${tag}-end.png`) });
  return { toPlay, ko, hinted, thrown, throws, secs: (throwTick - koTick) / 60, end, idleMax };
}

for (const backend of BACKENDS) {
  console.log(`--- ${backend}`);
  const one = await run(backend, 'alone', false);
  check('newcomer: out, told how to ride, throws within ten seconds, the round ends', one.toPlay >= 0 && one.ko >= 0 && one.hinted >= 0 && one.throws >= 1 && one.secs < 10 && one.end >= 0,
    `play ${one.toPlay >= 0}, knocked out ${one.ko >= 0}, hint drawn ${one.hinted >= 0}, throws ${one.throws} (${one.secs.toFixed(1)} s after the knockout), round over ${one.end >= 0}, longest seat idle ${(one.idleMax / 60).toFixed(1)} s`);
  const two = await run(backend, 'idle-seat', true);
  check('a second seat that joined and idles: P1 still throws, the round still ends', two.toPlay >= 0 && two.hinted >= 0 && two.throws >= 1 && two.end >= 0,
    `play ${two.toPlay >= 0}, hint drawn ${two.hinted >= 0}, P1 throws ${two.throws}, round over ${two.end >= 0}`);
  const ctl = await run(backend, 'control', false, 'ride_hint 0');
  check('control: ride_hint 0, the newcomer is told nothing and never throws', ctl.ko >= 0 && ctl.hinted < 0 && ctl.throws === 0,
    `knocked out ${ctl.ko >= 0}, hint drawn ${ctl.hinted >= 0}, throws ${ctl.throws}`);
}
await pads({}, {});
console.log(fail ? `${fail} FAILED` : 'all ok');
await bye(fail ? 1 : 0);
