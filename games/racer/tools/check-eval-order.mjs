#!/usr/bin/env node
// games/racer/tools/check-eval-order.mjs [files...]: finds expressions whose result depends on the order the compiler
// evaluates their operands in. C leaves that order unspecified for a call's arguments, a braced initializer's items and the
// two sides of a binary operator, and GCC (the native build) and clang (the carts) pick different orders: two random draws
// in one call, `rnd(&r) - rnd(&r)`, or `A[n++] = f(n)` made native and the carts build different scenery from the same
// seed (racer-temple failed native-vs-romdev on its canopy). Exit 1 when any is found; it prints file:line and the reason.
//
// Fix a hit by giving each side effect a statement (or declarator) of its own, in the order clang takes them: left to
// right, so the carts keep their look and native follows them.
//
// A side effect is ++, --, an assignment, or a call to a function that changes state (it assigns through a pointer
// parameter, to a file-scope or static variable, or calls such a function; cw_rng_* too). Default: every .c and .inc
// file of the racer.
import { readFileSync, readdirSync } from 'node:fs';
import { join, dirname, relative, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

const RACER = resolve(dirname(fileURLToPath(import.meta.url)), '..');
const files = process.argv.length > 2 ? process.argv.slice(2).map((f) => resolve(f))
  : [RACER, join(RACER, 'maps')].flatMap((d) => readdirSync(d).filter((f) => /\.(c|inc)$/.test(f)).map((f) => join(d, f)));

// ── tokens ──
const PUNCT = ['<<=', '>>=', '...', '->', '++', '--', '<<', '>>', '<=', '>=', '==', '!=', '&&', '||', '+=', '-=', '*=', '/=', '%=', '&=', '|=', '^=', '##'];
function tokenize(src) {
  const t = [];
  let i = 0, line = 1, bol = true;
  while (i < src.length) {
    const c = src[i];
    if (c === '\n') { line++; i++; bol = true; continue; }
    if (c === ' ' || c === '\t' || c === '\r') { i++; continue; }
    if (bol && c === '#') { // a preprocessor line (with its continuations)
      while (i < src.length && src[i] !== '\n') { if (src[i] === '\\' && src[i + 1] === '\n') { i++; line++; } i++; }
      continue;
    }
    bol = false;
    if (c === '/' && src[i + 1] === '/') { while (i < src.length && src[i] !== '\n') i++; continue; }
    if (c === '/' && src[i + 1] === '*') { i += 2; while (i < src.length && !(src[i] === '*' && src[i + 1] === '/')) { if (src[i] === '\n') line++; i++; } i += 2; continue; }
    if (c === '"' || c === "'") {
      let j = i + 1;
      while (j < src.length && src[j] !== c) { if (src[j] === '\\') j++; j++; }
      t.push({ k: 'lit', v: src.slice(i, j + 1), line });
      i = j + 1; continue;
    }
    if (/[A-Za-z_]/.test(c)) { let j = i; while (/\w/.test(src[j] || '')) j++; t.push({ k: 'id', v: src.slice(i, j), line }); i = j; continue; }
    if (/[0-9]/.test(c) || (c === '.' && /[0-9]/.test(src[i + 1]))) {
      let j = i; while (/[\w.]/.test(src[j] || '') || ((src[j] === '+' || src[j] === '-') && /[eEpP]/.test(src[j - 1]))) j++;
      t.push({ k: 'num', v: src.slice(i, j), line }); i = j; continue;
    }
    const p = PUNCT.find((q) => src.startsWith(q, i)) || c;
    t.push({ k: 'p', v: p, line }); i += p.length;
  }
  return t;
}

// ── type names (casts, compound literals, declarations) ──
const TYPE_KW = new Set(['void', 'char', 'short', 'int', 'long', 'float', 'double', 'signed', 'unsigned', 'bool', '_Bool', 'struct', 'union', 'enum',
  'const', 'volatile', 'static', 'extern', 'inline', 'register', 'restrict', 'size_t', 'uint8_t', 'uint16_t', 'uint32_t', 'uint64_t', 'int8_t', 'int16_t',
  'int32_t', 'int64_t', 'uintptr_t', 'intptr_t', 'ptrdiff_t', '__attribute__']);
const typedefs = new Set();
function collectTypedefs(src) {
  for (const m of src.matchAll(/\btypedef\b[^;{]*?(?:\{[^]*?\}\s*)?\(?\s*\*?\s*([A-Za-z_]\w*)\s*\)?\s*(?:\[[^\]]*\]|\([^)]*\))?\s*;/g)) typedefs.add(m[1]);
  for (const m of src.matchAll(/\btypedef\s+(?:struct|union|enum)\s*\w*\s*\{/g)) { // typedef struct {...} name;
    let d = 0, j = m.index + m[0].length - 1;
    for (; j < src.length; j++) { if (src[j] === '{') d++; else if (src[j] === '}' && --d === 0) break; }
    const n = /^\s*([A-Za-z_]\w*)/.exec(src.slice(j + 1));
    if (n) typedefs.add(n[1]);
  }
}
const isType = (tok) => tok && tok.k === 'id' && (TYPE_KW.has(tok.v) || typedefs.has(tok.v));
{
  const sdk = join(RACER, '../../deps/cartwheel-sdk/linux-x64');
  const walk = (d) => { try { for (const e of readdirSync(d, { withFileTypes: true })) { const p = join(d, e.name); if (e.isDirectory()) walk(p); else if (/\.h$/.test(e.name)) collectTypedefs(readFileSync(p, 'utf8')); } } catch { /* no SDK: racer types only */ } };
  walk(join(sdk, 'include'));
  walk(join(sdk, 'deps/three.c/include'));
  for (const d of [RACER, join(RACER, 'maps')]) for (const f of readdirSync(d)) if (/\.(c|h|inc)$/.test(f)) collectTypedefs(readFileSync(join(d, f), 'utf8'));
}

// ── functions and which of them change state ──
const ASSIGN = new Set(['=', '+=', '-=', '*=', '/=', '%=', '&=', '|=', '^=', '<<=', '>>=']);
function match(t, i) { // index of the bracket closing t[i]
  const open = t[i].v, close = { '(': ')', '[': ']', '{': '}' }[open];
  for (let d = 0, j = i; j < t.length; j++) { if (t[j].v === open) d++; else if (t[j].v === close && --d === 0) return j; }
  return t.length - 1;
}
function scanFile(path) {
  const t = tokenize(readFileSync(path, 'utf8'));
  const fns = [], globals = new Set();
  for (let i = 0, st = 0; i < t.length; i++) {
    const v = t[i].v;
    if (v === '{' || v === '(' || v === '[') {
      if (v === '{' && t[i - 1] && t[i - 1].v === ')') { // a function body: name ( params ) {
        let o = i - 1, d = 0;
        for (; o >= 0; o--) { if (t[o].v === ')') d++; else if (t[o].v === '(' && --d === 0) break; }
        const name = t[o - 1], end = match(t, i);
        if (name && name.k === 'id') {
          const head = t.slice(st, o - 1).map((x) => x.v);
          const params = [];
          for (let j = o + 1, last = null; j < i - 1; j++) { if (t[j].k === 'id') last = t[j].v; if (t[j].v === ',' || j === i - 2) { if (last) params.push(last); last = null; } }
          fns.push({ name: name.v, isStatic: head.includes('static'), isVoid: head.includes('void') && !head.includes('*') && !head.slice(head.indexOf('void') + 1).includes('*'), params, body: [i, end] });
        }
        i = end; st = i + 1; continue;
      }
      if (v === '{' && t[i - 1] && t[i - 1].v !== '=' ) { i = match(t, i); continue; } // struct / enum bodies
      i = match(t, i); continue;
    }
    if (v === ';') {
      // file-scope variables: the identifiers before = , ; [ in a declaration that is not a prototype
      const s = t.slice(st, i);
      if (s.length && !s.some((x) => x.v === 'typedef')) {
        let d = 0;
        for (let j = 0; j < s.length; j++) {
          const x = s[j];
          if (x.v === '(' || x.v === '[' || x.v === '{') d++;
          else if (x.v === ')' || x.v === ']' || x.v === '}') d--;
          else if (d === 0 && x.k === 'id' && s[j + 1] && ['=', ',', ';', '['].includes(s[j + 1].v)) globals.add(x.v);
        }
        if (s.length && s[s.length - 1].k === 'id' && d === 0) globals.add(s[s.length - 1].v);
      }
      st = i + 1;
    }
  }
  return { path, t, fns, globals };
}
const scanned = files.map(scanFile);
// What a function changes: 2 (G) state outside it (a file-scope or static variable, or through a G function), 1 (P) only
// what its pointer parameters reach (an RNG's state passed as &r, a builder), 0 nothing. A call's effect counts at its call
// site: two G calls clash, two P calls clash when they are handed the same thing.
const level = new Map(); // "file|name" (static) or "name" -> 0 | 1 | 2
function inDecl(t, i, a) { // is the = at i a declaration's initializer?
  let j = i;
  while (j > a && ![';', '{', '}'].includes(t[j - 1].v)) j--;
  return t[j].v === 'static' || isType(t[j]) || (t[j].k === 'id' && t[j + 1] && t[j + 1].k === 'id' && t[j].v !== 'return');
}
function directLevel(f, fn) {
  const t = f.t, [a, b] = fn.body;
  const statics = new Set();
  for (let i = a; i < b; i++) if (t[i].v === 'static') { for (let j = i + 1; j < b && t[j].v !== ';'; j++) if (t[j].k === 'id' && t[j + 1] && ['=', ';', '[', ','].includes(t[j + 1].v)) statics.add(t[j].v); }
  const locals = new Set();
  for (let i = a; i < b; i++) if (isType(t[i]) || (t[i].k === 'id' && t[i + 1] && t[i + 1].k === 'id')) { /* rough: names declared in the body shadow globals */
    for (let j = i + 1; j < b && t[j].v !== ';' && t[j].v !== ')'; j++) if (t[j].k === 'id' && t[j + 1] && ['=', ';', ',', '['].includes(t[j + 1].v) && !isType(t[j])) locals.add(t[j].v);
  }
  fn.locals = locals;
  const root = (i) => { // the lvalue ending at token i-1: walk back to its root identifier and say how it is reached
    let j = i - 1, deref = false;
    while (j >= a) {
      const v = t[j].v;
      if (v === ']' || v === ')') { let d = 0; for (; j >= a; j--) { if (t[j].v === v) d++; else if (t[j].v === (v === ']' ? '[' : '(') && --d === 0) break; } deref = true; j--; continue; }
      if (t[j].k === 'id' && t[j - 1] && (t[j - 1].v === '->' || t[j - 1].v === '.')) { if (t[j - 1].v === '->') deref = true; j -= 2; continue; }
      if (t[j].k === 'id') { if (t[j - 1] && t[j - 1].v === '*') deref = true; return { name: t[j].v, deref }; }
      break;
    }
    return null;
  };
  let lvl = 0;
  fn.writes = new Set(); // the parameters (by position) whose pointees it changes
  for (let i = a; i < b; i++) {
    const v = t[i].v;
    let target = null;
    if ((ASSIGN.has(v) && !(v === '=' && inDecl(t, i, a))) || ((v === '++' || v === '--') && t[i - 1] && (t[i - 1].k === 'id' || t[i - 1].v === ']' || t[i - 1].v === ')'))) target = root(i);
    else if ((v === '++' || v === '--') && t[i + 1] && t[i + 1].k === 'id') { let j = i + 1; while (t[j + 2] && (t[j + 1].v === '->' || t[j + 1].v === '.')) j += 2; target = { name: t[i + 1].v, deref: j > i + 1 && t.slice(i + 1, j + 1).some((x) => x.v === '->') }; }
    if (!target) continue;
    if (statics.has(target.name)) return 2;
    if (f.globals.has(target.name) && !locals.has(target.name) && !fn.params.includes(target.name)) return 2;
    if (fn.params.includes(target.name) && target.deref) { lvl = 1; fn.writes.add(fn.params.indexOf(target.name)); }
  }
  return lvl;
}
// a call's arguments, each as the identifiers in it
function argIds(t, i) { // t[i] is the call's '('
  const e = match(t, i), out = [];
  for (let j = i + 1, cur = [], d = 0; j <= e; j++) {
    const v = t[j].v;
    if (v === '(' || v === '[' || v === '{') d++; else if ((v === ')' && j < e) || v === ']' || v === '}') d--;
    if ((d === 0 && v === ',') || j === e) { out.push(cur); cur = []; } else if (t[j].k === 'id') cur.push(t[j].v);
  }
  return out;
}
const callsOf = new Map();
const fnOf = new Map();
for (const f of scanned) for (const fn of f.fns) {
  const key = fn.isStatic ? `${f.path}|${fn.name}` : fn.name;
  level.set(key, directLevel(f, fn));
  fnOf.set(key, fn);
  const c = [];
  for (let i = fn.body[0]; i < fn.body[1]; i++) if (f.t[i].k === 'id' && f.t[i + 1] && f.t[i + 1].v === '(' && !['if', 'while', 'for', 'switch', 'return', 'sizeof'].includes(f.t[i].v))
    c.push({ name: f.t[i].v, args: argIds(f.t, i + 1) });
  callsOf.set(key, { f, fn, c });
}
const resolveFn = (path, name) => (level.has(`${path}|${name}`) ? `${path}|${name}` : level.has(name) ? name : null);
// functions whose only state is a cache that gives the same answers whichever call fills it
const PURE = new Set(['road_index_build']);
const levelOf = (path, name) => (/^cw_rng_/.test(name) || name === 'rand' ? 2 : PURE.has(name) ? 0 : level.get(resolveFn(path, name)) || 0);
const writesOf = (path, name) => { const k = resolveFn(path, name); return (k && fnOf.get(k).writes) || new Set(); };
for (let changed = true; changed;) { // what a function's calls change, it changes
  changed = false;
  for (const [key, { f, fn, c }] of callsOf) {
    let l = level.get(key);
    const nw = fn.writes.size;
    for (const call of c) {
      const cl = levelOf(f.path, call.name);
      if (cl === 2) l = 2;
      else if (cl === 1) for (const k of writesOf(f.path, call.name)) for (const x of call.args[k] || []) {
        if (fn.params.includes(x)) { fn.writes.add(fn.params.indexOf(x)); if (l < 1) l = 1; }
        else if (f.globals.has(x) && !fn.locals.has(x)) l = 2;
      }
    }
    if (l !== level.get(key) || fn.writes.size !== nw) { level.set(key, l); changed = true; }
  }
}
const isMut = (path, name) => levelOf(path, name) > 0;

// ── expressions ──
// node: { kind: 'call'|'bin'|'seq'|'cond'|'assign'|'inc'|'list'|'leaf'|'wrap', kids, line, name, var, ids }
const BIN = [['||'], ['&&'], ['|'], ['^'], ['&'], ['==', '!='], ['<', '>', '<=', '>='], ['<<', '>>'], ['+', '-'], ['*', '/', '%']];
const PREC = new Map(BIN.flatMap((ops, i) => ops.map((o) => [o, i + 1])));
function parseExpr(t, lo, hi, path) {
  let i = lo;
  const peek = () => (i < hi ? t[i] : null);
  const leaf = (tok) => ({ kind: 'leaf', kids: [], line: tok ? tok.line : 0, path: tok && tok.k === 'id' ? tok.v : null, ids: tok && tok.k === 'id' ? [tok.v] : [] });
  const typeAt = (j) => { // ( type ) at j: index of ')' or -1
    if (t[j].v !== '(' || !isType(t[j + 1])) return -1;
    let k = j + 1;
    while (k < hi && (isType(t[k]) || t[k].v === '*' || (t[k].k === 'id' && (t[k - 1].v === 'struct' || t[k - 1].v === 'enum' || t[k - 1].v === 'union')))) k++;
    return t[k] && t[k].v === ')' ? k : -1;
  };
  function list(a, b) { // { items } -> list node
    const kids = [];
    for (let j = a + 1, s = a + 1, d = 0; j <= b; j++) {
      const v = t[j].v;
      if (v === '(' || v === '[' || v === '{') d++;
      else if (v === ')' || v === ']' || (v === '}' && j < b)) d--;
      if ((d === 0 && v === ',') || j === b) {
        let s2 = s;
        if (t[s2] && t[s2].v === '.' ) { while (s2 < j && t[s2].v !== '=') s2++; s2++; } // .field = value
        if (t[s2] && t[s2].v === '[') { s2 = match(t, s2) + 1; if (t[s2] && t[s2].v === '=') s2++; } // [i] = value
        if (s2 < j) kids.push(t[s2].v === '{' ? list(s2, match(t, s2)) : parseExpr(t, s2, j, path));
        s = j + 1;
      }
    }
    return { kind: 'list', kids, line: t[a].line };
  }
  function primary() {
    const tok = peek();
    if (!tok) return leaf(null);
    if (tok.v === '(') {
      const ct = typeAt(i);
      if (ct >= 0) {
        i = ct + 1;
        if (peek() && peek().v === '{') { const e = match(t, i); const n = list(i, e); i = e + 1; return postfix(n); }
        return { kind: 'wrap', kids: [unary()], line: tok.line };
      }
      const e = match(t, i);
      const n = parseExpr(t, i + 1, e, path);
      i = e + 1;
      return postfix(n);
    }
    if (tok.v === '{') { const e = match(t, i); const n = list(i, e); i = e + 1; return n; }
    i++;
    if (tok.v === 'sizeof') {
      if (peek() && peek().v === '(') { const e = match(t, i); i = e + 1; return leaf(null); }
      unary(); return leaf(null);
    }
    return postfix(leaf(tok));
  }
  function postfix(n) {
    for (;;) {
      const tok = peek();
      if (!tok) return n;
      if (tok.v === '(') {
        const e = match(t, i), args = [];
        for (let j = i + 1, s = i + 1, d = 0; j <= e; j++) {
          const v = t[j].v;
          if (v === '(' || v === '[' || v === '{') d++; else if ((v === ')' && j < e) || v === ']' || v === '}') d--;
          if ((d === 0 && v === ',') || j === e) { if (j > s) args.push(parseExpr(t, s, j, path)); s = j + 1; }
        }
        const name = n.kind === 'leaf' && n.ids.length === 1 ? n.ids[0] : null;
        n = { kind: 'call', kids: [n, ...args], line: tok.line, name, level: name ? levelOf(path, name) : 0, args: new Set(args.flatMap((x) => paths(x).map(rootOf))),
          touched: new Set(name ? [...writesOf(path, name)].flatMap((k) => (args[k] ? paths(args[k]).map(rootOf) : [])) : []), effect: name && isMut(path, name) ? `${name}()` : null };
        i = e + 1;
      } else if (tok.v === '[') {
        const e = match(t, i);
        n = { kind: 'bin', kids: [n, parseExpr(t, i + 1, e, path)], line: tok.line, path: n.path ? `${n.path}[${t.slice(i + 1, e).map((x) => x.v).join('')}]` : null };
        i = e + 1;
      } else if (tok.v === '.' || tok.v === '->') {
        const fld = t[i + 1] ? t[i + 1].v : '?';
        i += 2;
        n = { kind: 'wrap', kids: [n], line: tok.line, path: n.path ? `${n.path}${tok.v}${fld}` : null };
      } else if (tok.v === '++' || tok.v === '--') { i++; n = { kind: 'inc', kids: [n], line: tok.line, var: n.path, effect: `${n.path || '?'}${tok.v}` }; }
      else return n;
    }
  }
  function unary() {
    const tok = peek();
    if (!tok) return leaf(null);
    if (tok.v === '++' || tok.v === '--') { i++; const x = unary(); return { kind: 'inc', kids: [x], line: tok.line, var: x.path, effect: `${tok.v}${x.path || '?'}` }; }
    if (['-', '+', '!', '~', '*', '&'].includes(tok.v)) {
      i++;
      const x = unary();
      return { kind: 'wrap', kids: [x], line: tok.line, path: tok.v === '*' && x.path ? `*${x.path}` : null };
    }
    return primary();
  }
  function binary(minPrec) {
    let l = unary();
    for (;;) {
      const tok = peek();
      const p = tok && PREC.get(tok.v);
      if (!p || p < minPrec) return l;
      i++;
      const r = binary(p + 1);
      l = { kind: tok.v === '&&' || tok.v === '||' ? 'seq' : 'bin', kids: [l, r], line: tok.line, op: tok.v };
    }
  }
  function cond() {
    const c = binary(1);
    if (peek() && peek().v === '?') {
      const q = peek(); i++;
      const a = assign();
      if (peek() && peek().v === ':') i++;
      const b = cond();
      return { kind: 'seq', kids: [c, a, b], line: q.line };
    }
    return c;
  }
  function assign() {
    const l = cond();
    const tok = peek();
    if (tok && ASSIGN.has(tok.v)) { i++; const r = assign(); return { kind: 'assign', kids: [l, r], line: tok.line, var: l.path, effect: `${l.path || '?'} ${tok.v}` }; }
    return l;
  }
  function comma() {
    let l = assign();
    while (peek() && peek().v === ',') { const tok = peek(); i++; l = { kind: 'seq', kids: [l, assign()], line: tok.line }; }
    return l;
  }
  const n = comma();
  return n;
}
function paths(n, out = []) { if (n.path) out.push(n.path); for (const k of n.kids) paths(k, out); return out; }
// does a read of r see the variable p change? (the same variable, or a part of it)
const touches = (r, p) => r === p || r.startsWith(`${p}.`) || r.startsWith(`${p}->`) || r.startsWith(`${p}[`);
const rootOf = (p) => { const r = /^\**([A-Za-z_]\w*)/.exec(p || '')?.[1]; return r === 'NULL' ? undefined : r; };
// two side effects in operands whose order is unspecified: can they touch the same state?
function clash(a, b, f) {
  const call = (e) => e.kind === 'call';
  const global = (x) => f.globals.has(x);
  if (call(a) && call(b)) return a.level === 2 || b.level === 2 || [...a.touched].some((x) => x && b.args.has(x)) || [...b.touched].some((x) => x && a.args.has(x)); // the same RNG state
  if (call(a) || call(b)) { const c = call(a) ? a : b, v = call(a) ? b : a, r = rootOf(v.var); return !r || (c.level === 2 ? global(r) : c.touched.has(r)); }
  return !a.var || !b.var || touches(a.var, b.var) || touches(b.var, a.var);
}
function effects(n) { // every side effect in n
  const out = [];
  (function go(m) { if (m.effect) out.push(m); for (const k of m.kids) go(k); })(n);
  return out;
}
const hits = [];
function check(n, f) {
  if (n.kind === 'call' || n.kind === 'bin' || n.kind === 'list' || n.kind === 'assign') {
    // the operands of these are unsequenced (a call's are indeterminately sequenced) against each other
    const per = n.kids.map(effects);
    let why = null;
    for (let x = 0; x < per.length && !why; x++)
      for (let y = x + 1; y < per.length && !why; y++)
        for (const a of per[x]) for (const b of per[y]) if (!why && clash(a, b, f)) why = `${a.effect} | ${b.effect}`;
    for (let k = 0; k < per.length && !why; k++) // one operand changes a variable another reads (A[n++] = f(n))
      for (const e of per[k]) if (!why && e.var && (e.kind === 'inc' || e.kind === 'assign')) {
        const r = n.kids.flatMap((kid, j) => (j !== k ? paths(kid) : [])).find((p) => touches(p, e.var));
        if (r) why = `${e.effect} and a read of ${r}`;
      }
    if (why) hits.push({ f, line: n.line, kind: n.kind === 'call' ? `arguments of ${n.name || 'a call'}()` : n.kind === 'list' ? 'braced initializer' : n.kind === 'assign' ? 'assignment' : `operator ${n.op || '[]'}`, why });
  }
  for (const k of n.kids) check(k, f);
}

// ── statements: every full expression of every function body ──
function body(f, a, b) {
  const t = f.t;
  const full = (lo, hi) => { if (hi > lo) check(parseExpr(t, lo, hi, f.path), f); };
  const isDecl = (j) => isType(t[j]) || (t[j].k === 'id' && t[j + 1] && t[j + 1].k === 'id') || (t[j].k === 'id' && t[j + 1] && t[j + 1].v === '*' && t[j + 2] && t[j + 2].k === 'id' && t[j + 3] && ['=', ';', ',', '['].includes(t[j + 3].v));
  function decl(lo, hi) { // declarators: each initializer is a full expression
    for (let j = lo, d = 0, eq = -1, s = lo; j <= hi; j++) {
      const v = j < hi ? t[j].v : ',';
      if (v === '(' || v === '[' || v === '{') d++; else if (v === ')' || v === ']' || v === '}') d--;
      else if (d === 0 && v === '=' && eq < 0) eq = j;
      else if (d === 0 && v === ',') { if (eq >= 0) full(eq + 1, j); eq = -1; s = j + 1; }
    }
  }
  function simple(lo, hi) { if (hi <= lo) return; if (isDecl(lo)) decl(lo, hi); else full(lo, hi); }
  function stmtEnd(j) { for (let d = 0; j < b; j++) { const v = t[j].v; if (v === '(' || v === '[' || v === '{') d++; else if (v === ')' || v === ']' || v === '}') d--; else if (d === 0 && v === ';') return j; } return b; }
  function stmt(j) { // returns the index after the statement
    const v = t[j].v;
    if (v === '{') { const e = match(t, j); block(j + 1, e); return e + 1; }
    if (v === ';') return j + 1;
    if (v === 'if' || v === 'while' || v === 'switch') {
      const e = match(t, j + 1); full(j + 2, e);
      let k = stmt(e + 1);
      if (v === 'if' && t[k] && t[k].v === 'else') k = stmt(k + 1);
      return k;
    }
    if (v === 'for') {
      const e = match(t, j + 1);
      const parts = [];
      for (let k = j + 2, s = j + 2, d = 0; k <= e; k++) { const w = t[k].v; if (w === '(' || w === '[' || w === '{') d++; else if ((w === ')' && k < e) || w === ']' || w === '}') d--; if ((d === 0 && w === ';') || k === e) { parts.push([s, k]); s = k + 1; } }
      if (parts[0]) simple(...parts[0]);
      if (parts[1]) full(...parts[1]);
      if (parts[2]) full(...parts[2]);
      return stmt(e + 1);
    }
    if (v === 'do') { const k = stmt(j + 1); const e = match(t, k + 1); full(k + 2, e); return e + 2; }
    if (v === 'return') { const e = stmtEnd(j); full(j + 1, e); return e + 1; }
    if (v === 'case') { let k = j; while (t[k].v !== ':') k++; return k + 1; }
    if (v === 'default' && t[j + 1].v === ':') return j + 2;
    if (t[j].k === 'id' && t[j + 1] && t[j + 1].v === ':' ) return j + 2; // a label
    if (['break', 'continue', 'goto'].includes(v)) return stmtEnd(j) + 1;
    const e = stmtEnd(j);
    simple(j, e);
    return e + 1;
  }
  function block(lo, hi) { for (let j = lo; j < hi;) j = stmt(j); }
  block(a + 1, b);
}
for (const f of scanned) for (const fn of f.fns) body(f, fn.body[0], fn.body[1]);

const seen = new Set();
for (const h of hits.sort((x, y) => (x.f.path < y.f.path ? -1 : x.f.path > y.f.path ? 1 : x.line - y.line))) {
  const k = `${h.f.path}:${h.line}:${h.why}`;
  if (seen.has(k)) continue;
  seen.add(k);
  console.log(`${relative(process.cwd(), h.f.path)}:${h.line}: ${h.kind}: ${h.why}`);
}
if (process.env.CHECK_EVAL_ORDER_LIST) console.log('changes state:', [...level].filter(([, v]) => v).map(([k, v]) => `${k.replace(/^.*\//, '')}:${'-PG'[v]}`).sort().join(' '));
process.exit(seen.size ? 1 : 0);
