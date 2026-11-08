// Part 1 of derby's grounding sweep (tests/quality-grounding.mjs): Very Low on GL.
import { spawnSync } from 'node:child_process';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';
const r = spawnSync('node', [join(dirname(fileURLToPath(import.meta.url)), 'quality-grounding.mjs'), '--apps', 'derby_auto', '--gpu', 'gl', '--preset', 'verylow', ...process.argv.slice(2)], { stdio: 'inherit' });
process.exit(r.status ?? 1);
