// Start-up probe: loads derby's web export in Chromium with the CPU
// throttled and reports the longest stretch without a painted frame after
// the download, with the player's load steps (window.cwLoadTimes).
//
//   node games/derby/tools/startup-probe.mjs [--dir out/web/web/derby-0.1.0-web] [--throttle 4]
import { createServer } from 'node:http';
import { readFileSync, existsSync, statSync } from 'node:fs';
import { join, resolve, dirname, extname } from 'node:path';
import { fileURLToPath } from 'node:url';
import { launchChromium } from '../../../deps/cartwheel-engine/tools/lib/chromium.mjs';

const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '../../..');
const argv = process.argv.slice(2);
const opt = (k, d) => (argv.includes(k) ? argv[argv.indexOf(k) + 1] : d);
const DIR = resolve(ROOT, opt('--dir', 'out/web/web/derby-0.1.0-web'));
const THROTTLE = Number(opt('--throttle', '4'));
const MIME = { '.js': 'text/javascript', '.mjs': 'text/javascript', '.html': 'text/html', '.css': 'text/css', '.wasc': 'application/octet-stream', '.wasm': 'application/wasm', '.json': 'application/json' };

const server = createServer((req, res) => {
  let p = join(DIR, decodeURIComponent(req.url.split('?')[0]));
  if (existsSync(p) && statSync(p).isDirectory()) p = join(p, 'index.html');
  if (!p.startsWith(DIR) || !existsSync(p)) { res.writeHead(404); res.end(); return; }
  res.writeHead(200, { 'Content-Type': MIME[extname(p)] || 'application/octet-stream', 'Cross-Origin-Opener-Policy': 'same-origin', 'Cross-Origin-Embedder-Policy': 'require-corp' });
  res.end(readFileSync(p));
});
await new Promise((r) => server.listen(0, '127.0.0.1', r));
const url = `http://127.0.0.1:${server.address().port}/`;

const { chromium } = await import(join(ROOT, 'deps/cartwheel-engine/deps/wasmcart/node_modules/playwright/index.mjs'));
const { browser } = await launchChromium(chromium, join(ROOT, 'out', 'derby'));
const page = await browser.newPage({ viewport: { width: 960, height: 540 } });
const cdp = await page.context().newCDPSession(page);
await cdp.send('Emulation.setCPUThrottlingRate', { rate: THROTTLE });
// every animation frame's time: a gap between two is a stretch with nothing painted
await page.addInitScript(() => {
  window.__frames = [];
  window.__stage = [];
  window.__gl = [];
  // time spent inside the WebGL calls that compile and link programs, per frame
  let acc = { ms: 0, links: 0, draws: 0, drawMs: 0 };
  const P = WebGL2RenderingContext.prototype;
  for (const name of ['compileShader', 'linkProgram', 'getProgramParameter', 'getShaderParameter', 'useProgram', 'drawElements', 'drawArrays', 'drawElementsInstanced', 'drawArraysInstanced', 'texImage2D', 'texSubImage2D', 'generateMipmap', 'bufferData']) {
    const orig = P[name];
    P[name] = function (...a) {
      const t = performance.now();
      const r = orig.apply(this, a);
      const d = performance.now() - t;
      acc.ms += d;
      if (name === 'linkProgram') acc.links++;
      if (name.startsWith('draw')) acc.draws++, acc.drawMs += d;
      (acc[name] = (acc[name] || 0) + d);
      return r;
    };
  }
  // which programs link: a name from each shader's source (SHADER_NAME, else its first lines)
  const srcOf = new WeakMap(), shadersOf = new WeakMap();
  const origSrc = P.shaderSource, origAttach = P.attachShader, origLink = P.linkProgram;
  P.shaderSource = function (sh, src) { srcOf.set(sh, src); return origSrc.call(this, sh, src); };
  P.attachShader = function (pr, sh) { (shadersOf.get(pr) || shadersOf.set(pr, []).get(pr)).push(sh); return origAttach.call(this, pr, sh); };
  window.__links = [];
  P.linkProgram = function (pr) {
    const fs = (shadersOf.get(pr) || []).map((sh) => srcOf.get(sh) || '').find((x) => /gl_FragColor|out\s+(highp\s+|mediump\s+)?vec4/.test(x)) || '';
    const m = /SHADER_NAME\s+(\S+)/.exec(fs);
    const defs = (fs.match(/#define\s+(USE_\w+|NUM_\w+ \d+)/g) || []).map((d) => d.replace('#define ', '')).slice(0, 8).join(' ');
    window.__links.push([window.__frames.length, m ? m[1] : fs.replace(/\s+/g, ' ').slice(0, 90), defs, fs.length]);
    return origLink.call(this, pr);
  };
  const f = (t) => {
    window.__frames.push(performance.now());
    window.__gl.push(acc);
    acc = { ms: 0, links: 0, draws: 0, drawMs: 0 };
    window.__stage.push(document.getElementById('stage')?.textContent || '');
    requestAnimationFrame(f);
  };
  requestAnimationFrame(f);
});
await page.goto(url);
await page.waitForFunction(() => window.cwLoadTimes && window.cwLoadTimes.ready, null, { timeout: 120000 });
await page.waitForTimeout(3000);
const r = await page.evaluate(() => ({ times: window.cwLoadTimes, frames: window.__frames, stage: window.__stage, gl: window.__gl, links: window.__links }));
await browser.close();
server.close();

const from = r.times.compile ?? r.times.download ?? 0;
const gaps = [];
for (let i = 1; i < r.frames.length; i++) if (r.frames[i] >= from) gaps.push([r.frames[i] - r.frames[i - 1], r.frames[i - 1], r.stage[i - 1], r.stage[i], r.gl[i]]);
gaps.sort((a, b) => b[0] - a[0]);
const stepAt = (t) => Object.entries(r.times).filter(([, v]) => v <= t).sort((a, b) => b[1] - a[1])[0]?.[0] ?? '?';
console.log(`throttle ${THROTTLE}x  load steps (ms): ${JSON.stringify(r.times)}`);
console.log('longest unpainted stretches after the download:');
for (const [g, t, s0, s1, gl] of gaps.slice(0, 8)) {
  const top = Object.entries(gl || {}).filter(([k]) => !['ms', 'links', 'draws', 'drawMs'].includes(k)).sort((a, b) => b[1] - a[1]).slice(0, 3).map(([k, v]) => `${k} ${v.toFixed(0)}`).join(', ');
  console.log(`  ${g.toFixed(0).padStart(5)} ms at ${t.toFixed(0)} (during ${stepAt(t)}: "${s0}" -> "${s1}")  gl ${gl ? gl.ms.toFixed(0) : '?'} ms, ${gl?.links ?? 0} links, ${gl?.draws ?? 0} draws [${top}]`);
}
if (argv.includes('--links')) {
  const worst = gaps.filter((g) => g[4] && g[4].links > 2).slice(0, 2);
  for (const g of worst) {
    const i = r.frames.indexOf(g[1]) + 1;
    console.log(`programs linked in the ${g[0].toFixed(0)} ms frame:`);
    for (const l of r.links.filter((l) => l[0] === i)) console.log(`  ${l[3]} b  ${l[1]}  ${l[2]}`);
  }
}
