// Part 8 of the games' quality range gate (tests/quality-range.mjs). The order between presets with 15% slack: chromite's
// WebGPU Low measured 6.75 ms against Medium's 5.96 (its own pass for the height fog at Low).
import { runPart } from './quality-range.mjs';
process.exit(runPart('8', ['--slack', '0.15', ...process.argv.slice(2)]));
