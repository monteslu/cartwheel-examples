// Compare the _auto replays' named state across platforms: each directory
// holds the <game>.json files tests/native-replay.mjs wrote on one platform
// and GPU API; every one must equal the reference field for field.
//
//   node tests/compare-replays.mjs <reference dir> <dir> [<dir> ...]
//
// Prints a table (Markdown, also appended to $GITHUB_STEP_SUMMARY when set).
// Exit status 1 when any state differs or a game is missing.
import { appendFileSync, existsSync, readFileSync, readdirSync } from 'node:fs';
import { basename, join } from 'node:path';

const [refDir, ...dirs] = process.argv.slice(2);
if (!refDir || !dirs.length) {
  console.error('usage: node tests/compare-replays.mjs <reference dir> <dir> [<dir> ...]');
  process.exit(2);
}
const load = (d, g) => (existsSync(join(d, `${g}.json`)) ? JSON.parse(readFileSync(join(d, `${g}.json`), 'utf8')) : null);
const games = readdirSync(refDir).filter((f) => f.endsWith('.json') && !f.endsWith('.state.json')).map((f) => basename(f, '.json')).sort();
let bad = 0;
const rows = [];
for (const d of [refDir, ...dirs]) {
  const cells = games.map((g) => {
    const ref = load(refDir, g), run = load(d, g);
    if (!run) { bad++; return 'missing'; }
    const diff = Object.keys(ref.state).filter((k) => JSON.stringify(ref.state[k]) !== JSON.stringify(run.state[k]));
    if (diff.length) { bad++; return `differs: ${diff.join(', ')}`; }
    return d === refDir ? `reference (${run.api})` : `identical (${run.api}, ${run.seconds} s)`;
  });
  const any = games.map((g) => load(d, g)).find(Boolean);
  rows.push(`| ${any ? `${any.platform} ${any.api}` : basename(d)} | ${cells.join(' | ')} |`);
}
const table = [`| run | ${games.join(' | ')} |`, `|---|${games.map(() => '---').join('|')}|`, ...rows].join('\n');
console.log(table);
if (process.env.GITHUB_STEP_SUMMARY) appendFileSync(process.env.GITHUB_STEP_SUMMARY, `## _auto replays across platforms\n\n${table}\n`);
process.exit(bad ? 1 : 0);
