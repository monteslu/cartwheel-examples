// chromite's rules and score system, natively (GL, or WebGPU with --webgpu), each check with a control that must fail:
//   game over   the last ship goes (the `die` command): the results card draws GAME OVER once (over.headings 1);
//               control sw double_over 1 (the dying HUD's own GAME OVER under the card, as it once was) draws two
//   modern      a pilot who never fires lets the first waves through: they cost score and the chain, never a ship
//               (escapes > 0, losses 0); control sw rules 1 (classic, the original's rule) spends ships
//   continue    modern rules: {fire} on the results card plays on from where the last ship went (3 ships, the score from
//               zero); control: classic, the same press goes to the title
//   chain       the autopilot's first 40 s score more with the chain than with flat kills (sw no_chain 1, the control), and
//               the chain grows past 3
//   boss        a command craft brought up and finished (`boss`, `boss low`): the fight holds 3 ticks, the pad rumbles,
//               the craft's fire turns to score chips; control: never fire at it (no hit-stop)
//   phases      a boss down to 45% has lost three of its six rim plates and charges its second attack (boss.tells);
//               control: an unhurt boss keeps six plates and never tells
//   bomb        the self-destruct's wave turns the rounds it passes into chips (cancelled > 0); control: arm only
//   node tests/chromite-rules.mjs [--webgpu]
import { spawnSync } from 'node:child_process';
import { readFileSync, mkdirSync } from 'node:fs';
import { resolve, dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';

const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '..'), OUT = join(ROOT, 'out/chromite-rules');
const RUN = join(ROOT, 'deps/cartwheel-engine/tools/run-cart.mjs');
const GPU = process.argv.includes('--webgpu') ? 'webgpu' : 'gl';
mkdirSync(OUT, { recursive: true });
let fail = 0;
const check = (name, ok, detail) => { console.log(`${ok ? 'ok  ' : 'FAIL'} ${name}: ${detail}`); if (!ok) fail++; };

function run(tag, frames, { input, cmds = [], app = 'chromite' } = {}) {
  const state = join(OUT, `${tag}.${GPU}.json`), shot = join(OUT, `${tag}.${GPU}.png`);
  const args = [RUN, '--host', 'native', '--gpu', GPU, '--app', app, '--frames', String(frames), '--state', state, '--shot', shot];
  if (input) args.push('--input', join(ROOT, 'tests', input));
  if (cmds.length) args.push('--start-cmds', cmds.join(';'));
  const r = spawnSync('node', args, { cwd: ROOT, encoding: 'utf8', env: { ...process.env, CW_PROJECT_ROOT: ROOT }, timeout: 300000 });
  if (r.status !== 0) { console.log(`FAIL ${tag}: run failed\n${(r.stderr || r.stdout).trim().split('\n').slice(-6).join('\n')}`); process.exit(1); }
  return JSON.parse(readFileSync(state, 'utf8'));
}

// game over: start, the last ship goes at frame 120, the card is up by frame 420
const over = run('over', 440, { input: 'chromite-start.json', cmds: ['@120 die'] });
check('game over: one GAME OVER on the card', over['game.state'] === 4 && over['over.headings'] === 1, `state ${over['game.state']}, headings ${over['over.headings']}`);
const overCtl = run('over-control', 440, { input: 'chromite-start.json', cmds: ['sw double_over 1', '@120 die'] });
check('control (sw double_over 1): the card shows two', overCtl['game.state'] === 4 && overCtl['over.headings'] === 2, `state ${overCtl['game.state']}, headings ${overCtl['over.headings']}`);

// a continue: after the last ship goes (frame 120), {fire} on the card (frame 500) plays on where it ended
const cont = run('continue', 560, { input: 'chromite-continue.json', cmds: ['@120 die'] });
check('modern: {fire} on the card continues the run', cont['game.state'] === 1 && cont.continues === 1 && cont.lives === 2,
  `state ${cont['game.state']}, continues ${cont.continues}, lives ${cont.lives}`);
const contCtl = run('continue-control', 560, { input: 'chromite-continue.json', cmds: ['sw rules 1', '@120 die'] });
check('control (classic): the same press goes to the title', contCtl['game.state'] === 0 && contCtl.continues === 0,
  `state ${contCtl['game.state']}, continues ${contCtl.continues}`);

// modern rules: never fire for 40 s; the first waves get past
const idle = run('modern', 2400, { input: 'chromite-start.json' });
check('modern: escapes cost score and the chain, no ship', idle.rules === 0 && idle.escapes > 0 && idle.losses === 0,
  `rules ${idle.rules}, escapes ${idle.escapes}, lives ${idle.lives}, losses ${idle.losses}, score ${idle.score}`);
const idleCtl = run('modern-control', 2400, { input: 'chromite-start.json', cmds: ['sw rules 1'] });
check('control (sw rules 1, classic): escapes spend ships', idleCtl.rules === 1 && idleCtl.losses > 0,
  `rules ${idleCtl.rules}, losses ${idleCtl.losses}, lives ${idleCtl.lives}`);

// chain: the autopilot's first 40 s with the chain against flat kills
const chain = run('chain', 2400, { app: 'chromite_auto' });
const flat = run('chain-control', 2400, { app: 'chromite_auto', cmds: ['sw no_chain 1'] });
check('chain: kills in a row pay more than flat kills', chain['chain.best'] >= 3 && chain.score > flat.score && chain.kills === flat.kills,
  `chain best ${chain['chain.best']}, score ${chain.score} vs flat ${flat.score}, kills ${chain.kills} / ${flat.kills}, point blanks ${chain.point_blanks}`);
check('control (sw no_chain 1) scores no more than itself', !(flat.score > flat.score), `flat ${flat.score}`);
check('feel: hits and kills rumble the pad', chain.rumbles > 0, `${chain.rumbles} rumble requests`);

// boss: up at frame 60 (after the start), one hit left at 200, the fight holds as it falls
const boss = run('boss', 420, { input: 'chromite-fight.json', cmds: ['@60 boss', '@200 boss low'] });
check('boss: the fall holds 3 ticks, rumbles, its fire turns to chips', boss['hitstop.ticks'] === 3 && boss.rumbles > 0 && boss.cancelled > 0,
  `hit-stop ticks ${boss['hitstop.ticks']}, rumbles ${boss.rumbles}, cancelled ${boss.cancelled}, score ${boss.score}`);
const bossCtl = run('boss-control', 420, { input: 'chromite-start.json', cmds: ['@60 boss', '@200 boss low'] });
check('control (never fire): no hit-stop', bossCtl['hitstop.ticks'] === 0, `hit-stop ticks ${bossCtl['hitstop.ticks']}`);

// boss phases: down to 45%, half its plates are gone and its second attack charges, then fires
const half = run('phase', 600, { input: 'chromite-start.json', cmds: ['@60 boss', '@200 boss half'] });
check('boss: half its armour off, the second attack is charged (told) before it fires', half['boss.plates'] === 3 && half['boss.tells'] >= 1,
  `plates ${half['boss.plates']}, tells ${half['boss.tells']}`);
const whole = run('phase-control', 600, { input: 'chromite-start.json', cmds: ['@60 boss'] });
check('control (never hurt): six plates, no second attack', whole['boss.plates'] === 6 && whole['boss.tells'] === 0, `plates ${whole['boss.plates']}, tells ${whole['boss.tells']}`);

// the self-destruct's wave
const bomb = run('bomb', 300, { input: 'chromite-destruct.json' });
const bombCtl = run('bomb-control', 300, { input: 'chromite-destruct-once.json' });
check('bomb: the wave turns enemy rounds into chips', bomb.cancelled > 0, `cancelled ${bomb.cancelled}, lives ${bomb.lives}`);
check('control (armed only): nothing cancelled', bombCtl.cancelled === 0, `cancelled ${bombCtl.cancelled}`);

console.log(fail ? `${fail} FAILED` : 'all ok');
process.exit(fail ? 1 : 0);
