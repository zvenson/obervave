// SPDX-License-Identifier: GPL-3.0-only
// OberVave simulator, the page: builds the panel (27 keys, 14 buttons, 7 encoders + MASTER), starts the AudioWorklet
// (worklet.js runs obervave.wasm, the firmware), draws its 240 x 240 screen and LEDs, and feeds it the mouse, touch,
// the computer keyboard and Web MIDI. Everything on the screen and in the sound is the firmware's own doing.
(function () {
  'use strict';
  const $ = (s, r = document) => r.querySelector(s);
  const $$ = (s, r = document) => Array.from(r.querySelectorAll(s));
  const clamp = (v, a, b) => Math.max(a, Math.min(b, v));

  // the firmware's numbers (firmware/src/platform.c: BTN_*, ENC_*)
  const BTN = { octm: 0, octp: 1, fx: 2, sel: 3, env: 4, lfo: 5, edit: 6, glo: 7, home: 8, save: 9, arp: 10, seq: 11, play: 12, rec: 13 };
  const ENC = { select: 0, algo: 1, k1: 2, k2: 3, k3: 4, k4: 5, presets: 6 };
  const STORE_KEY = 'obervave-sim-store-3', MASTER_KEY = 'obervave-sim-master';

  const S = { node: null, ctx: null, on: false, starting: false, master: 900, midi: null, midiSel: '' };
  try { const m = parseInt(localStorage.getItem(MASTER_KEY), 10); if (m >= 0 && m <= 1023) S.master = m; } catch (e) {}
  // Matomo on thefirmwarehub.com (no cookies, if the page has it): who starts the sound, plays a note, brings a MIDI
  // keyboard, keeps playing a minute / five; nothing else, no notes or settings
  const ev = (a) => { try { if (window._paq) window._paq.push(['trackEvent', 'Play', 'OberVave: ' + a]); } catch (e) {} };
  let played = false;
  const send = (m) => {
    if (S.node) S.node.port.postMessage(m);
    if (!played && ((m.t === 'key' && m.down) || (m.t === 'midi' && (m.d[0] & 0xF0) === 0x90 && m.d[2] > 0))) { played = true; ev('first note'); }
  };

  // ---- the screen
  const cv = $('#screen'), cx = cv.getContext('2d'), img = cx.createImageData(240, 240);
  const R5 = new Uint8Array(32).map((_, i) => (i * 255 / 31) | 0), G6 = new Uint8Array(64).map((_, i) => (i * 255 / 63) | 0);
  const draw = (fb) => {
    const d = img.data;
    for (let k = 0, j = 0; k < 57600; k++, j += 4) {
      const v = fb[k];
      d[j] = R5[v >> 11]; d[j + 1] = G6[(v >> 5) & 63]; d[j + 2] = R5[v & 31]; d[j + 3] = 255;
    }
    cx.putImageData(img, 0, 0);
  };
  cx.fillStyle = '#0b0e16'; cx.fillRect(0, 0, 240, 240);

  // ---- storage (programs, settings, patterns: the firmware's flash sectors, kept in localStorage)
  const toB64 = (buf) => { const u = new Uint8Array(buf); let s = ''; for (let i = 0; i < u.length; i += 0x8000) s += String.fromCharCode.apply(null, u.subarray(i, i + 0x8000)); return btoa(s); };
  const fromB64 = (s) => { const b = atob(s), u = new Uint8Array(b.length); for (let i = 0; i < b.length; i++) u[i] = b.charCodeAt(i); return u.buffer; };
  const loadStore = () => { try { const s = localStorage.getItem(STORE_KEY); return s ? fromB64(s) : null; } catch (e) { return null; } };
  const saveStore = (buf) => { try { localStorage.setItem(STORE_KEY, toB64(buf)); } catch (e) {} };
  $('#resetStore').addEventListener('click', () => {
    if (!confirm('Forget your saved programs, settings and patterns in this browser and restart?')) return;
    try { localStorage.removeItem(STORE_KEY); } catch (e) {}
    location.reload();
  });

  // ---- power on: a browser starts audio only after a gesture
  const power = $('#power');
  const say = (a, b) => { power.querySelector('.p1').textContent = a; power.querySelector('.p2').textContent = b; };
  async function start() {
    if (S.on || S.starting) return;
    S.starting = true;
    say('Starting…', 'loading the firmware');
    try {
      if (!window.AudioWorkletNode) throw new Error('this browser has no AudioWorklet');
      const Ctx = window.AudioContext || window.webkitAudioContext;
      try { S.ctx = new Ctx({ sampleRate: 44100, latencyHint: 'interactive' }); } catch (e) { S.ctx = new Ctx({ latencyHint: 'interactive' }); }
      if (S.ctx.sampleRate !== 44100) throw new Error('the audio runs at ' + S.ctx.sampleRate + ' Hz, the firmware wants 44100');
      const resumed = S.ctx.resume();                     // (inside the gesture)
      const [wasm] = await Promise.all([fetch('obervave.wasm').then((r) => { if (!r.ok) throw new Error('obervave.wasm: ' + r.status); return r.arrayBuffer(); }),
        S.ctx.audioWorklet.addModule('worklet.js')]);
      S.node = new AudioWorkletNode(S.ctx, 'obervave', { numberOfInputs: 0, outputChannelCount: [2], processorOptions: { wasm, store: loadStore(), master: S.master } });
      S.node.port.onmessage = (msg) => {
        const m = msg.data;
        if (m.t === 'frame') { if (m.fb) draw(m.fb); leds(m.leds, m.keys, m.glow); }
        else if (m.t === 'store') saveStore(m.data);
        else if (m.t === 'ready') { S.on = true; power.hidden = true; ev('start'); setTimeout(() => ev('1 min'), 60e3); setTimeout(() => ev('5 min'), 300e3); }
        else if (m.t === 'error') { say('Error', m.message); S.starting = false; }
      };
      S.node.connect(S.ctx.destination);
      await resumed;
    } catch (e) {
      S.starting = false;
      say('Could not start', String((e && e.message) || e));
    }
  }
  power.addEventListener('click', start);
  if (/[?&]autostart\b/.test(location.search)) start();   // (?autostart: kiosks, headless checks; browsers may still want a gesture)
  const wake = () => { if (S.ctx && S.ctx.state === 'suspended') S.ctx.resume(); };   // (touch: pointerup is the gesture)
  ['pointerup', 'touchend', 'keydown', 'click'].forEach((t) => document.addEventListener(t, wake, true));
  document.addEventListener('visibilitychange', () => { if (S.ctx && !document.hidden && S.ctx.state === 'suspended') S.ctx.resume(); });

  // ---- LEDs from the firmware
  const btnEls = {};
  $$('.btn[data-btn]').forEach((el) => { btnEls[+el.dataset.btn] = el; });
  const keyEls = [];
  let ledPrev = -1, keyPrev = -1, glowPrev = -1;
  function leds(b, k, g) {
    if (b !== ledPrev) { ledPrev = b; for (const i in btnEls) btnEls[i].classList.toggle('lit', !!((b >> i) & 1)); }
    if (k !== keyPrev || g !== glowPrev) {
      keyPrev = k; glowPrev = g;
      keyEls.forEach((el, i) => { el.classList.toggle('lit', !!((k >> i) & 1)); el.classList.toggle('glow', !!((g >> i) & 1)); });
    }
  }

  // ---- buttons (mouse, touch: press and hold works, two fingers for a layer + a key)
  const btnHeld = new Set();
  const btnDown = (b) => { if (btnHeld.has(b)) return; btnHeld.add(b); btnEls[b].classList.add('down'); send({ t: 'btn', b, down: 1 }); };
  const btnUp = (b) => { if (!btnHeld.has(b)) return; btnHeld.delete(b); btnEls[b].classList.remove('down'); send({ t: 'btn', b, down: 0 }); };
  $$('.btn[data-btn]').forEach((el) => {
    const b = +el.dataset.btn;
    el.addEventListener('pointerdown', (e) => { e.preventDefault(); if (!S.on) start(); el.setPointerCapture(e.pointerId); btnDown(b); });
    el.addEventListener('pointerup', () => btnUp(b));
    el.addEventListener('pointercancel', () => btnUp(b));
    el.addEventListener('contextmenu', (e) => e.preventDefault());
  });

  // ---- knobs: the encoders turn in detents (drag up / down, the wheel), MASTER is a pot 0..1023
  const knobAng = {};
  $$('.knob').forEach((el) => {
    const id = el.dataset.id;
    el.innerHTML = '<div class="cap"></div><div class="lab"></div>';
    el.querySelector('.lab').textContent = el.dataset.label;
    const cap = el.querySelector('.cap');
    const show = () => { cap.style.transform = `rotate(${knobAng[id]}deg)`; };
    knobAng[id] = id === 'master' ? S.master / 1023 * 270 - 135 : 0;
    show();
    const turn = (steps) => {
      if (!steps) return;
      if (id === 'master') {
        S.master = clamp(S.master + steps * 24, 0, 1023);
        knobAng[id] = S.master / 1023 * 270 - 135;
        send({ t: 'master', v: S.master });
        try { localStorage.setItem(MASTER_KEY, String(S.master)); } catch (e) {}
      } else {
        knobAng[id] += steps * 15;
        send({ t: 'enc', e: ENC[id], steps });
      }
      show();
    };
    let drag = null;
    el.addEventListener('pointerdown', (e) => { e.preventDefault(); if (!S.on) start(); el.setPointerCapture(e.pointerId); drag = { y: e.clientY, acc: 0 }; cap.classList.add('turning'); });
    el.addEventListener('pointermove', (e) => {
      if (!drag) return;
      drag.acc += drag.y - e.clientY; drag.y = e.clientY;
      const st = (drag.acc / 7) | 0;                    // 7 px a detent
      if (st) { drag.acc -= st * 7; turn(st); }
    });
    const end = () => { drag = null; cap.classList.remove('turning'); };
    el.addEventListener('pointerup', end);
    el.addEventListener('pointercancel', end);
    let wacc = 0;
    el.addEventListener('wheel', (e) => {
      e.preventDefault();
      wacc += e.deltaMode === 0 ? e.deltaY / 40 : e.deltaY;
      const st = Math.trunc(wacc);
      if (st) { wacc -= st; turn(-st); }
    }, { passive: false });
    el.turn = turn;
  });
  const knob = (id) => $(`.knob[data-id="${id}"]`);

  // ---- the 27 keys, F3 .. G5 (key 0 = F3): 16 white, 11 black
  const BLACK = new Set([1, 3, 5, 8, 10, 13, 15, 17, 20, 22, 25]);
  const keysEl = $('#keys'), NW = 16, WW = 100 / NW;
  let wi = 0;
  for (let k = 0; k < 27; k++) {
    const el = document.createElement('div'), black = BLACK.has(k);
    el.className = 'key ' + (black ? 'b' : 'w');
    el.dataset.k = k;
    el.innerHTML = '<span class="dot"></span><span class="kl"></span>';
    if (black) { el.style.left = `calc(${wi * WW}% - ${WW * 0.31}%)`; el.style.width = `${WW * 0.62}%`; }
    else { el.style.left = `${wi * WW}%`; el.style.width = `${WW}%`; wi++; }
    keysEl.append(el);
    keyEls[k] = el;
  }
  const keyHeld = new Map();                             // key -> how many sources hold it (pointers, computer keys)
  const keyDown = (k) => { const n = keyHeld.get(k) || 0; keyHeld.set(k, n + 1); if (!n) { keyEls[k].classList.add('down'); send({ t: 'key', k, down: 1 }); } };
  const keyUp = (k) => { const n = keyHeld.get(k) || 0; if (!n) return; if (n > 1) { keyHeld.set(k, n - 1); return; } keyHeld.delete(k); keyEls[k].classList.remove('down'); send({ t: 'key', k, down: 0 }); };
  const ptrKey = new Map();                              // pointer -> key under it (slides from key to key)
  const keyAt = (x, y) => { const el = document.elementFromPoint(x, y); const k = el && el.closest('.key'); return k && keysEl.contains(k) ? +k.dataset.k : -1; };
  keysEl.addEventListener('pointerdown', (e) => {
    e.preventDefault(); if (!S.on) start();
    keysEl.setPointerCapture(e.pointerId);
    const k = keyAt(e.clientX, e.clientY);
    ptrKey.set(e.pointerId, k);
    if (k >= 0) keyDown(k);
  });
  keysEl.addEventListener('pointermove', (e) => {
    if (!ptrKey.has(e.pointerId)) return;
    const k = keyAt(e.clientX, e.clientY), was = ptrKey.get(e.pointerId);
    if (k === was) return;
    if (was >= 0) keyUp(was);
    if (k >= 0) keyDown(k);
    ptrKey.set(e.pointerId, k);
  });
  const ptrEnd = (e) => { if (!ptrKey.has(e.pointerId)) return; const k = ptrKey.get(e.pointerId); ptrKey.delete(e.pointerId); if (k >= 0) keyUp(k); };
  keysEl.addEventListener('pointerup', ptrEnd);
  keysEl.addEventListener('pointercancel', ptrEnd);
  keysEl.addEventListener('contextmenu', (e) => e.preventDefault());

  // ---- the computer keyboard (physical positions, e.code): two piano rows
  const CODEKEY = {
    KeyZ: 7, KeyS: 8, KeyX: 9, KeyD: 10, KeyC: 11, KeyV: 12, KeyG: 13, KeyB: 14, KeyH: 15, KeyN: 16, KeyJ: 17, KeyM: 18,
    Comma: 19, KeyL: 20, Period: 21, Semicolon: 22, Slash: 23,
    KeyQ: 0, Digit2: 1, KeyW: 2, Digit3: 3, KeyE: 4, Digit4: 5, KeyR: 6,
    KeyI: 24, Digit9: 25, KeyO: 26,
  };
  const CODEBTN = {
    Digit1: BTN.home, Digit5: BTN.env, Digit7: BTN.lfo, Digit8: BTN.arp, Digit0: BTN.seq, Minus: BTN.edit, Equal: BTN.glo,
    BracketLeft: BTN.fx, BracketRight: BTN.save, Enter: BTN.sel, NumpadEnter: BTN.sel, Space: BTN.play, Backslash: BTN.rec,
    ArrowLeft: BTN.octm, ArrowRight: BTN.octp,
  };
  const US = { Comma: ',', Period: '.', Semicolon: ';', Slash: '/', Minus: '-', Equal: '=', BracketLeft: '[', BracketRight: ']', Backslash: '\\', Enter: '⏎', Space: '␣', ArrowLeft: '←', ArrowRight: '→', NumpadEnter: '' };
  const label = (code, map) => (map && map.get(code)) ? map.get(code).toUpperCase() : (US[code] !== undefined ? US[code] : code.replace(/^Key|^Digit/, ''));
  const labelAll = (map) => {
    for (const [code, k] of Object.entries(CODEKEY)) keyEls[k].querySelector('.kl').textContent = label(code, map);
    for (const [code, b] of Object.entries(CODEBTN)) {
      if (code === 'NumpadEnter') continue;
      let s = btnEls[b].querySelector('.kb');
      if (!s) { s = document.createElement('span'); s.className = 'kb'; btnEls[b].append(s); }
      s.textContent = label(code, ['Enter', 'Space', 'ArrowLeft', 'ArrowRight'].includes(code) ? null : map);
    }
  };
  labelAll(null);
  if (navigator.keyboard && navigator.keyboard.getLayoutMap) navigator.keyboard.getLayoutMap().then(labelAll).catch(() => {});
  const typing = (e) => /input|textarea|select/i.test((e.target && e.target.tagName) || '');
  const codeHeld = new Set();
  addEventListener('keydown', (e) => {
    if (typing(e) || e.metaKey || e.ctrlKey || e.altKey) return;
    const k = CODEKEY[e.code], b = CODEBTN[e.code];
    if (e.code === 'ArrowUp' || e.code === 'ArrowDown') { e.preventDefault(); if (!S.on) start(); knob('presets').turn(e.code === 'ArrowUp' ? 1 : -1); return; }
    if (k === undefined && b === undefined) return;
    e.preventDefault();
    if (e.repeat || codeHeld.has(e.code)) return;
    if (!S.on) start();
    codeHeld.add(e.code);
    if (k !== undefined) keyDown(k); else btnDown(b);
  });
  addEventListener('keyup', (e) => {
    if (!codeHeld.has(e.code)) return;
    codeHeld.delete(e.code);
    const k = CODEKEY[e.code], b = CODEBTN[e.code];
    if (k !== undefined) keyUp(k); else if (b !== undefined) btnUp(b);
  });
  addEventListener('blur', () => {                       // (nothing stays stuck when the window loses the keyboard)
    for (const c of codeHeld) { const k = CODEKEY[c], b = CODEBTN[c]; if (k !== undefined) keyUp(k); else if (b !== undefined) btnUp(b); }
    codeHeld.clear();
  });

  // ---- Web MIDI: the inputs, one or all
  const midiBtn = $('#midiBtn'), midiSel = $('#midiSel'), midiMsg = $('#midiMsg');
  const onMidi = (port) => (m) => {
    if (S.midiSel && port.id !== S.midiSel) return;
    const d = m.data, st = d[0];
    if (!S.on || st < 0x80 || st === 0xF0 || st === 0xF7 || st === 0xFE) return;
    send({ t: 'midi', d: [st, d[1] || 0, d[2] || 0] });
  };
  const listMidi = () => {
    const acc = S.midi, keep = S.midiSel;
    midiSel.innerHTML = '';
    const all = document.createElement('option'); all.value = ''; all.textContent = 'All MIDI inputs'; midiSel.append(all);
    let n = 0;
    acc.inputs.forEach((p) => {
      n++;
      p.onmidimessage = onMidi(p);
      const o = document.createElement('option'); o.value = p.id; o.textContent = p.name || p.id; midiSel.append(o);
    });
    midiSel.value = [...midiSel.options].some((o) => o.value === keep) ? keep : '';
    S.midiSel = midiSel.value;
    midiSel.disabled = false;
    midiMsg.textContent = n ? `${n} MIDI input${n > 1 ? 's' : ''} found. Play!` : 'No MIDI input found: plug one in, it shows up here.';
  };
  midiBtn.addEventListener('click', async () => {
    if (!navigator.requestMIDIAccess) { midiMsg.textContent = 'This browser has no Web MIDI (Safari): try Chrome, Edge or Firefox.'; return; }
    try {
      S.midi = await navigator.requestMIDIAccess({ sysex: false });
      ev('midi');
      S.midi.onstatechange = listMidi;
      listMidi();
      midiBtn.textContent = 'MIDI connected';
      if (!S.on) start();
    } catch (e) { midiMsg.textContent = 'MIDI was not allowed: ' + ((e && e.message) || e); }
  });
  midiSel.addEventListener('change', () => { S.midiSel = midiSel.value; });
})();
