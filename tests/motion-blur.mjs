// Motion blur at speed, on the carts through romdev, GL and WebGPU. The engine's PostProcess motion_blur is turned on
// by the game only while the player is fast; the debug field cam.blur is the shutter the game set this frame (x1000, 0:
// the pass off). The engine's own gates hold the pass itself (post-motion-blur, viewmodel-blur); this holds the games'
// use of it:
//   racer    a solo race, the CPU driving the player's kart (bot 1): a boost opens the shutter (cam.blur >= 250) and
//            it closes again after the boost (0); a two-view race never opens it (the post chain is shared)
//   pugball  the night stadium, P1 held on the throttle and the boost from kickoff: past 14 m/s the shutter opens and
//            reaches 250 or more at supersonic; let go, it closes again
// Control: sw blur 0 (the game never sets the blur) must fail the opening check in both games.
// A frame at the open shutter is kept for each game and backend to look at (out/motion-blur).
//   node tests/motion-blur.mjs [--gl] [--webgpu] [--games racer,pugball]
import { ROMDEV_URL } from './lib/romdev-url.mjs';
import { mkdirSync } from 'node:fs';
import { resolve, dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';
import { request } from 'node:http';

const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '..'), SESSION = 'motion-blur', OUT = join(ROOT, 'out/motion-blur');
const arg = (n, d) => { const i = process.argv.indexOf(n); return i > 0 ? process.argv[i + 1] : d; };
const BACKENDS = process.argv.includes('--gl') ? ['gl'] : process.argv.includes('--webgpu') ? ['webgpu'] : ['gl', 'webgpu'];
const GAMES = arg('--games', 'racer,pugball').split(',');
const OPEN = 250;
mkdirSync(OUT, { recursive: true });
let requests = 0;
const post = (name, body) => new Promise((ok, no) => {
  requests++;
  const q = request(`${ROMDEV_URL}/tool/${name}`, { method: 'POST', headers: { 'Content-Type': 'application/json', 'x-romdev-session': SESSION, 'x-romdev-agent': SESSION } }, (res) => {
    let t = '';
    res.setEncoding('utf8');
    res.on('data', (c) => (t += c));
    res.on('end', () => ok({ status: res.statusCode, text: t }));
  });
  q.on('error', no);
  q.end(body);
});
const bye = async (code) => {
  try { await post('host', '{"op":"shutdown"}'); } catch {}
  console.log(`romdev requests: ${requests}`);
  process.exit(code);
};
const tool = async (name, args) => {
  let r;
  try { r = await post(name, JSON.stringify(args)); }
  catch (e) { console.error(`romdev ${name}: ${e.message} (the server is down)`); await bye(3); }
  let j; try { j = JSON.parse(r.text); } catch { j = r.text; }
  if (r.status !== 200 || j?.error) { console.error(`romdev ${name}: ${r.text.slice(0, 400)}`); await bye(3); }
  return j;
};
const cmd = async (text, maxFrames = 8) => (await tool('wasm', { op: 'command', text, maxFrames })).reply;
const load = (game, backend) => tool('loadMedia', { platform: 'wasmcart', path: join(ROOT, `build/cart-wgpu/carts/${game}.wasc`), gpuPower: 'low-power', ...(backend === 'gl' ? { wgpu: false } : {}) });
// step until cam.blur meets the condition, at most `frames`; the frame it stopped at, or false
const until = async (cond, frames, input) => {
  const r = input
    ? await tool('input', { op: 'sequence', steps: [{ input, frames }], stopWhen: { field: 'cam.blur', ...cond }, fields: ['cam.blur'] })
    : await tool('frame', { op: 'step', frames, stopWhen: { field: 'cam.blur', ...cond }, fields: ['cam.blur'] });
  return r.stopped ? r.stopped.frameCount ?? r.stopped.step ?? true : false;
};
const shot = (name) => tool('frame', { op: 'screenshot', path: join(OUT, name) });

// each leg: the setup, then { opened, closed } with control sw blur 0 run on the same setup
const LEGS = {
  racer: {
    async setup(views) {
      await tool('frame', { op: 'step', frames: 400 });
      await cmd(`race 0 ${views}`, 600);
      await cmd('bot 1');
    },
    async run(views) {
      const opened = await until({ atLeast: OPEN }, 3600);
      if (opened && views === 1) await shot(`racer-${this.backend}.png`);
      const closed = opened ? await until({ atMost: 0 }, 600) : false;
      return { opened, closed };
    },
  },
  pugball: {
    async setup() {
      await tool('frame', { op: 'step', frames: 400 });
      await cmd('stadium 2', 600);
      await tool('frame', { op: 'step', frames: 420 }); // the kickoff's count
    },
    async run() {
      const opened = await until({ atLeast: OPEN }, 420, { ports: [{ r: true, east: true }] });
      if (opened) await shot(`pugball-${this.backend}.png`);
      const closed = opened ? await until({ atMost: 0 }, 420, { ports: [{}] }) : false;
      return { opened, closed };
    },
  },
};

let fails = 0;
const say = (ok, line) => { if (!ok) fails++; console.log(`${ok ? 'ok  ' : 'FAIL'} ${line}`); };
for (const backend of BACKENDS) {
  for (const game of GAMES) {
    const leg = { ...LEGS[game], backend };
    await load(game, backend);
    await leg.setup(1);
    const r = await leg.run(1);
    say(r.opened && r.closed, `${game} ${backend}: the shutter opens at speed (${r.opened ? `frame ${r.opened}` : 'never'}) and closes after (${r.closed ? `frame ${r.closed}` : 'never'})`);
    if (game === 'racer') {
      await load(game, backend);
      await leg.setup(2);
      const s = await leg.run(2);
      say(!s.opened, `racer ${backend}: two views never open it (${s.opened ? `opened at frame ${s.opened}` : 'never opened'})`);
    }
    await load(game, backend);
    await leg.setup(1);
    await cmd('sw blur 0');
    const c = await leg.run(1);
    say(!c.opened, `${game} ${backend}: control (sw blur 0) never opens it, so the check above can fail (${c.opened ? 'OPENED: the control passes' : 'never opened'})`);
  }
}
await bye(fails ? 1 : 0);
