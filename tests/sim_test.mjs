// SPDX-License-Identifier: GPL-3.0-only
// The browser simulator without a browser: loads docs/sim/obervave.wasm in node, boots it, plays a chord, MIDI notes,
// a button, knob turns, renders seconds of sound and checks: it sounds, no NaN, no clipping, the screen changes, and
// it renders far faster than real time. Then the same script through the native host build (tests/host.c): the
// sound must be the same samples (the simulator renders a block ahead, as the device's ISR: 256 frames later in
// time) and the screens the same pixels.
//   node tests/sim_test.mjs [docs/sim/obervave.wasm]      (after sh web/sim/build.sh)
import fs from 'fs';
import path from 'path';
import { execFileSync } from 'child_process';
import { fileURLToPath } from 'url';

const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const WASM = process.argv[2] || path.join(ROOT, 'docs/sim/obervave.wasm');
let fail = 0;
const check = (ok, what) => { console.log((ok ? '  ok    ' : '  FAIL  ') + what); if (!ok) fail = 1; };

const { instance } = await WebAssembly.instantiate(fs.readFileSync(WASM), {});
const ex = instance.exports;
console.log(`sim: ${WASM} (${fs.statSync(WASM).size} B)`);
check(WebAssembly.Module.imports(new WebAssembly.Module(fs.readFileSync(WASM))).length === 0, 'no imports (standalone)');
check(ex.sim_init(44100) === 0, 'sim_init(44100)');
ex.sim_master(1024);                                    // unity, as the host's WAV

// the script, in tests/host.c's words (T ms: keys MASK / btn ID / enc ID N / midi ST D1 D2 / shot / end)
const SCRIPT = `
2600 shot
3000 keys 0x4880
3400 shot
3800 keys 0
4000 enc 6 1
4300 midi 0x90 60 100
4400 midi 0x90 64 90
5000 midi 0x80 60 0
5000 midi 0x80 64 0
5200 btn 2
5300 enc 2 5
5600 shot
6000 end
`;
const ev = SCRIPT.trim().split('\n').map((l) => l.trim().split(/\s+/)).map(([t, w, ...a]) => ({ t: +t, w, a: a.map(Number) }));
const END = ev.find((e) => e.w === 'end').t;
const MAXF = Math.ceil(END * 44.1) + 512;
const L = new Float32Array(MAXF), R = new Float32Array(MAXF), shots = {};
let nf = 0, ei = 0;
const fb = () => new Uint16Array(ex.memory.buffer, ex.sim_fb(), 240 * 240).slice();
const hashes = new Set();
let lastMs = ex.sim_ms();
while (lastMs < END) {
  // the events and shots for the next millisecond go in before its pass (as host.c's platform_poll)
  while (ei < ev.length && ev[ei].t <= lastMs + 1) {
    const e = ev[ei++];
    if (e.w === 'keys') for (let k = 0; k < 27; k++) ex.sim_key(k, (e.a[0] >> k) & 1);
    else if (e.w === 'btn') { ex.sim_button(e.a[0], 1); ex.sim_button(e.a[0], 0); }   // a tap
    else if (e.w === 'enc') ex.sim_enc(e.a[0], e.a[1]);
    else if (e.w === 'midi') ex.sim_midi(e.a[0], e.a[1], e.a[2]);
    else if (e.w === 'shot') shots[e.t] = fb();
  }
  ex.sim_render(1);
  L[nf] = new Float32Array(ex.memory.buffer, ex.sim_out_l(), 1)[0];
  R[nf] = new Float32Array(ex.memory.buffer, ex.sim_out_r(), 1)[0];
  nf++;
  if (ex.sim_ms() !== lastMs) { lastMs = ex.sim_ms(); if (lastMs % 100 === 0) hashes.add(ex.sim_fb_hash()); }
}

let peak = 0, nan = 0, e2 = 0, e2chord = 0;
for (let i = 0; i < nf; i++) {
  for (const v of [L[i], R[i]]) { if (!Number.isFinite(v)) nan++; else { peak = Math.max(peak, Math.abs(v)); e2 += v * v; } }
  if (i >= 3000 * 44.1 && i < 3800 * 44.1) e2chord += L[i] * L[i];
}
const rms = Math.sqrt(e2 / (2 * nf)), rmsChord = Math.sqrt(e2chord / (800 * 44.1));
console.log(`  ${nf} frames, peak ${peak.toFixed(3)}, rms ${rms.toFixed(4)}, chord rms ${rmsChord.toFixed(4)}`);
check(nan === 0, 'no NaN / Inf');
check(rmsChord > 0.005, 'the chord sounds');
check(peak < 32767 / 32768, 'no clipping (peak below full scale)');
check(hashes.size >= 10, `the screen changes (${hashes.size} different screens at 100 ms steps)`);
check(ex.sim_prog() === 1, `PRESETS +1 -> program 02 (prog ${ex.sim_prog()})`);
check(ex.sim_leds_buttons() !== 0, `button LEDs (0x${ex.sim_leds_buttons().toString(16)})`);

// speed: 10 s of a held chord (DARK ARP's arp + effects) in 128-frame quanta, as the worklet asks
{
  for (const k of [7, 11, 14, 18]) ex.sim_key(k, 1);
  const t0 = performance.now();
  for (let i = 0; i < 44100 * 10 / 128; i++) ex.sim_render(128);
  const ms = performance.now() - t0;
  console.log(`  speed: 10 s of audio in ${ms.toFixed(0)} ms (${(10000 / ms).toFixed(0)}x real time; 1 s in ${(ms / 10).toFixed(1)} ms)`);
  check(ms < 2000, 'renders at least 5x faster than real time');
  for (const k of [7, 11, 14, 18]) ex.sim_key(k, 0);
}
// the heaviest program: each of the 32 with an 8-note chord for a second (all voices, its effects)
{
  let worst = 0, wp = 0;
  const chord = [0, 4, 7, 11, 14, 16, 19, 23];
  for (let p = 0; p < 32; p++) {
    if (p) ex.sim_enc(6, 1);
    for (let i = 0; i < 44100 * 0.3 / 128; i++) ex.sim_render(128);
    for (const k of chord) ex.sim_key(k, 1);
    const t0 = performance.now();
    for (let i = 0; i < 44100 / 128; i++) ex.sim_render(128);
    const ms = performance.now() - t0;
    if (ms > worst) { worst = ms; wp = p; }
    for (const k of chord) ex.sim_key(k, 0);
  }
  console.log(`  heaviest program: ${String(wp + 1).padStart(2, '0')}, 1 s with 8 keys in ${worst.toFixed(1)} ms`);
  check(worst < 250, 'every program renders at least 4x faster than real time');
  for (let i = 0; i < 44100 * 3 / 128; i++) ex.sim_render(128);   // (the releases and the reverb die away)
}

// storage: SAVE twice keeps the program in the settings sector (written ~2 s later, when quiet); a new instance with
// those bytes boots on it
{
  check(ex.sim_blob_size() === 16384 && ex.sim_blob(0) !== 0 && ex.sim_blob(9) === 0, 'storage blobs exported');
  const h0 = ex.sim_store_hash(), tap = (b) => { ex.sim_button(b, 1); for (let i = 0; i < 20; i++) ex.sim_render(128); ex.sim_button(b, 0); for (let i = 0; i < 20; i++) ex.sim_render(128); };
  tap(8);                                               // HOME: PERFORM
  ex.sim_enc(2, 7);                                     // KNOB 1 (CUTOFF) +7: the program is edited
  for (let i = 0; i < 100; i++) ex.sim_render(128);
  tap(9); tap(9);                                       // SAVE, SAVE
  for (let i = 0; i < 44100 * 4 / 128; i++) ex.sim_render(128);
  const h1 = ex.sim_store_hash(), size = ex.sim_blob_size(), prog = ex.sim_prog();
  check(h1 !== h0, 'SAVE twice -> the settings sector changes');
  const ex2 = (await WebAssembly.instantiate(fs.readFileSync(WASM), {})).instance.exports;
  for (let i = 0; i < 9; i++) new Uint8Array(ex2.memory.buffer, ex2.sim_blob(i), size).set(new Uint8Array(ex.memory.buffer, ex.sim_blob(i), size));
  ex2.sim_init(44100);
  for (let i = 0; i < 44100 * 3 / 128; i++) ex2.sim_render(128);
  check(ex2.sim_prog() === prog && ex2.sim_store_hash() === h1, `a new instance with the saved bytes boots on program ${String(prog + 1).padStart(2, '0')}`);
}

// the page's AudioWorklet (worklet.js) in a stand-in for the browser's worklet scope: boots, renders 128-frame
// quanta, sends the screen and LEDs, takes a key, keeps the storage it is given
{
  const vm = await import('vm');
  const posted = [];
  let Proc = null;
  const ctx = {
    AudioWorkletProcessor: class { constructor() { this.port = { postMessage: (m) => posted.push(m), onmessage: null }; } },
    registerProcessor: (name, c) => { if (name === 'obervave') Proc = c; },
    currentTime: 0, WebAssembly, Float32Array, Uint16Array, Uint8Array, Error, String,
  };
  vm.runInNewContext(fs.readFileSync(path.join(path.dirname(WASM), 'worklet.js'), 'utf8'), ctx);
  const size = ex.sim_blob_size(), store = new Uint8Array(9 * size);
  for (let i = 0; i < 9; i++) store.set(new Uint8Array(ex.memory.buffer, ex.sim_blob(i), size), i * size);
  const w = new Proc({ processorOptions: { wasm: fs.readFileSync(WASM), store: store.buffer, master: 900 } });
  const out = [[new Float32Array(128), new Float32Array(128)]];
  let e2 = 0, frames = 0;
  for (let q = 0; q < 44100 * 4 / 128; q++) {
    ctx.currentTime = q * 128 / 44100;
    if (q === Math.round(3 * 44100 / 128)) w.port.onmessage({ data: { t: 'key', k: 7, down: 1 } });
    w.process([], out);
    if (q > 3 * 44100 / 128) for (const v of out[0][0]) e2 += v * v;
  }
  for (const m of posted) if (m.t === 'frame' && m.fb) frames++;
  check(posted[0] && posted[0].t === 'ready', 'worklet.js: boots (ready)');
  check(frames >= 10, `worklet.js: sends the screen when it changes (${frames} frames in 4 s)`);
  check(e2 > 0.01, 'worklet.js: a key sounds through process()');
  check(w.ex.sim_prog() === ex.sim_prog(), 'worklet.js: boots with the storage it was given');
}

// the native host build, same script: the same samples and the same screens
const host = path.join(ROOT, 'build/host/obervave-simcmp');
let haveCC = true;
try {
  fs.mkdirSync(path.join(ROOT, 'build/host'), { recursive: true });
  execFileSync('cc', ['-O2', '-w', '-DOBV_HOST', '-Ifirmware/src', '-Ifirmware/gen', '-o', host, 'firmware/src/obervave.c'], { cwd: ROOT });
} catch (e) { haveCC = false; console.log('  (no cc: the comparison with the host build is skipped)'); }
if (haveCC) {
  fs.writeFileSync(host + '.txt', SCRIPT);
  execFileSync(host, [host, host + '.txt'], { cwd: ROOT });
  const wav = fs.readFileSync(host + '.wav'), hs = new Int16Array(wav.buffer, wav.byteOffset + 44, (wav.length - 44) >> 1);
  const hn = hs.length >> 1;
  let diff = 0, maxd = 0;
  for (let i = 0; i < hn && i + 256 < nf; i++) {
    const dl = Math.abs(Math.round(L[i + 256] * 32768) - hs[2 * i]), dr = Math.abs(Math.round(R[i + 256] * 32768) - hs[2 * i + 1]);
    if ((dl || dr) && !diff) console.log(`  first difference at host frame ${i} (${(i / 44.1).toFixed(1)} ms): sim ${Math.round(L[i + 256] * 32768)} host ${hs[2 * i]}`);
    if (dl || dr) diff++;
    maxd = Math.max(maxd, dl, dr);
  }
  check(diff === 0, `sound = the host build's, sample for sample (${hn} frames, ${diff} differ, max ${maxd})`);
  for (const t of Object.keys(shots)) {
    const ppm = fs.readFileSync(`${host}-${t}.ppm`), off = ppm.length - 240 * 240 * 3, s = shots[t];
    let bad = 0;
    for (let k = 0; k < 57600; k++) {
      const c = s[k], r = ((c >> 11) * 255 / 31) | 0, g = ((c >> 5 & 63) * 255 / 63) | 0, b = ((c & 31) * 255 / 31) | 0;
      if (ppm[off + 3 * k] !== r || ppm[off + 3 * k + 1] !== g || ppm[off + 3 * k + 2] !== b) bad++;
    }
    check(bad === 0, `screen at ${t} ms = the host build's (${bad} pixels differ)`);
  }
}
console.log(fail ? 'SIM TEST FAILED' : 'SIM TEST PASSED');
process.exit(fail);
