// Reads announcer logs (RACER_VOICE_LOG=file runs) and says how the voice paced the race:
//   node games/racer/tools/voice-log.mjs out/voice-a.txt [more logs...]
// For each log: lines said, cut, dropped; the must-say lines (the countdown, the final lap, the finish) apart from
// the rest; the mean and the longest gap between the other lines during the race; and any two lines whose words
// overlapped (it must be none: one voice at a time, a cut stops the line it cuts). The words' lengths come from
// voice_lines.h (tools/make-voice.mjs writes them).
import { readFileSync } from 'node:fs';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';

const HERE = dirname(fileURLToPath(import.meta.url));
const table = readFileSync(join(HERE, '..', 'voice_lines.h'), 'utf8');
const words = new Map(); /* id + text -> ms */
for (const m of table.matchAll(/\{ "([a-z0-9_]+)", \d+, \{[^}]*\}, \{ (.*?) \}, \{ ([\d, ]+) \} \}/g)) {
  const texts = JSON.parse('[' + m[2] + ']'), ms = m[3].split(',').map(Number);
  texts.forEach((t, i) => words.set(m[1] + '|' + t, ms[i]));
}
const MUST = new Set(['three', 'two', 'one', 'go', 'final_lap', 'winner', 'what_a_race', 'so_close', 'podium', 'finished', 'new_record', 'medal_bronze', 'medal_silver', 'medal_gold', 'medal_author', 'cup_winner', 'cup_trophy', 'cup_done']);
let bad = 0;
for (const path of process.argv.slice(2)) {
  const ev = [];
  for (const row of readFileSync(path, 'utf8').split('\n')) {
    const m = row.match(/^\s*([\d.]+)s\s+race\s+([\d.]+)s\s+(\w+)\s+(\w+)\s*(?:"(.*)")?/);
    if (m) ev.push({ t: +m[1], race: +m[2], what: m[3], id: m[4], text: m[5] });
  }
  const said = ev.filter((e) => e.what === 'SAY'), other = said.filter((e) => !MUST.has(e.id));
  const n = (w) => ev.filter((e) => e.what === w).length;
  /* the gaps between the lines that are not must-say, from the lights to the last line said */
  const go = said.find((e) => e.id === 'go'), from = go ? go.t : 0, to = said.length ? said[said.length - 1].t : 0;
  const times = other.map((e) => e.t).filter((t) => t >= from);
  let longest = 0, prev = from;
  for (const t of times) longest = Math.max(longest, t - prev), prev = t;
  const span = to - from, mean = times.length ? span / times.length : 0;
  /* the words never overlap: each line starts after the last one's words end, unless that one was cut */
  const overlaps = [];
  for (let i = 1; i < ev.length; i++) {
    if (ev[i].what !== 'SAY') continue;
    let j = i - 1;
    while (j >= 0 && ev[j].what !== 'SAY' && ev[j].what !== 'CUT') j--;
    if (j < 0 || ev[j].what === 'CUT') continue;
    const ms = words.get(ev[j].id + '|' + ev[j].text);
    if (ms === undefined) { overlaps.push(`${ev[j].id}: not in voice_lines.h`); continue; }
    if (ev[i].t + 0.02 < ev[j].t + ms / 1000) overlaps.push(`${ev[j].id} at ${ev[j].t}s ran into ${ev[i].id} at ${ev[i].t}s`);
  }
  const ids = new Map();
  for (const e of said) ids.set(e.id, (ids.get(e.id) || 0) + 1);
  console.log(`${path}: ${said.length} said (${said.length - other.length} must-say), ${n('CUT')} cut, ${n('STALE') + n('DROP')} dropped over ${span.toFixed(0)} s of race`);
  console.log(`  the other lines: one every ${mean.toFixed(1)} s on average, the longest quiet ${longest.toFixed(1)} s`);
  console.log('  ' + [...ids].map(([k, v]) => `${k} ${v}`).join(', '));
  if (overlaps.length) { bad++; console.log('  OVERLAP: ' + overlaps.join('; ')); }
}
process.exit(bad ? 1 : 0);
