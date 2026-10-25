// games/racer/tests/mkinput.mjs "<frame>:<pad button or key>[+more] ..." > input.json
// Each token presses for 3 frames then lets go. A token beginning with k: is a keyboard key
// (k:return), p2: / p3: / p4: is that pad's button (p2:south), m: is a click ("m:480,300").
const toks = process.argv[2].trim().split(/\s+/);
const ev = [];
for (const t of toks) {
  const [f, what] = t.split(/:(.*)/s);
  const frame = +f;
  const down = { frame };
  const up = { frame: frame + 3 };
  for (const w of what.split('+')) {
    if (w.startsWith('k:')) (down.keys ??= []).push(w.slice(2));
    else if (w.startsWith('m:')) {
      const [x, y] = w.slice(2).split(',').map(Number);
      down.mouse = { x, y, buttons: ['left'] };
      up.mouse = { x, y, buttons: [] };
    } else if (/^p[234]:/.test(w)) {
      const pad = 'pad' + w[1];
      (down[pad] ??= { buttons: [] }).buttons.push(w.slice(3));
      up[pad] = { buttons: [] };
    } else (down.buttons ??= []).push(w);
  }
  ev.push(down, up);
}
ev.sort((a, b) => a.frame - b.frame);
const pads = new Set(ev.flatMap((e) => Object.keys(e).filter((k) => /^pad\d/.test(k))));
for (const e of ev) for (const p of pads) e[p] ??= { buttons: [] }; // a pad stays plugged in once used
if (pads.size) ev.unshift(Object.fromEntries([["frame", 1], ...[...pads].map((p) => [p, { buttons: [] }])]));
console.log(JSON.stringify(ev));
