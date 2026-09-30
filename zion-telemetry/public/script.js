(() => {
'use strict';
const $ = (s) => document.querySelector(s);
const isNum = (v) => typeof v === 'number' && isFinite(v);
const store = {
  get(k, d){ try { const v = localStorage.getItem(k); return v ? JSON.parse(v) : d; } catch { return d; } },
  set(k, v){ try { localStorage.setItem(k, JSON.stringify(v)); } catch {} }
};

/* ==========================================================================
   SITE SETUP — fill in before uploading to the college website
   firebaseUrl: Realtime Database URL (Firebase console → Realtime Database)
   apiKey:      Web API key (Project settings → General)
   loginEmail:  the team account (Authentication → Users). Visitors then only
                type the password. Leave empty to ask for email + password.
   Leave firebaseUrl or apiKey empty for preview mode (demo kart, password "demo").
   ========================================================================== */
const SITE = {
  firebaseUrl: 'https://zion-racing-telemetry-123c0-default-rtdb.asia-southeast1.firebasedatabase.app',
  apiKey: 'AIzaSyAOMgNnFEJ5B5EsZO-sQd8G00y6fyN8_Zg',
  loginEmail: '',
  // One Firebase path per kart. Each board's DEVICE_NAME must match its path.
  karts: {
    cv: { path: 'zion/gokart_cv', name: 'CV kart' },
    ev: { path: 'zion/gokart_ev', name: 'EV kart' }
  }
};
const LIVE = !!(SITE.firebaseUrl && SITE.apiKey);

/* ---------- configuration ---------- */
const DEFAULTS = { source:LIVE ? 'firebase' : 'demo' };
let cfg = { source:(store.get('zion.cfg', {}) || {}).source || DEFAULTS.source };
if (!LIVE) cfg.source = 'demo';
let page = 'home';                                   // 'home' | 'cv' | 'ev'
const kartPath = () => (SITE.karts[page] || SITE.karts.cv).path;
const SPEED_MAX = 100;                               // CV gauge: 0–100 km/h
let topSpeed = 0;
const SV = { '.sv':'timestamp' };                // Firebase server timestamp
const RPM_MAX = 6000, REDLINE = 5400;           // TAFM max speed 3600–5400 rpm
const LIMITS = {                                 // VESC defaults: MOSFET 85/100 °C, motor 85/100 °C
  motorTemp:      { label:'Motor',      warn:85, crit:100, max:120 },
  controllerTemp: { label:'Controller', warn:75, crit:85,  max:100 },
  batteryTemp:    { label:'Battery',    warn:45, crit:55,  max:65  }
};

/* ---------- state ---------- */
let root = { live:{}, race:{} };
let lastRx = 0;
let livePush = false;            // true when the change came from the kart's /live node
const OFFLINE_AFTER = 10;        // seconds without a kart update before showing Offline
let lapClock = { lap:null, elapsed:0, at:0 };
let hist = [], trail = [], sfPoint = null, prevLap = null, prevBest = null, lapSig = '';
// Positions the kart held on-board through a network outage and replayed to
// /track once it reconnected. Merged into the trail so the driven line shows
// the real path instead of jumping across the gap.
let bufferedPath = [], trackSeen = {}, trackPollAt = 0;
// Samples are appended under both /live and /history. Keep the full-history
// polyline derived from Firebase rather than duplicating the data in memory.
const HISTORY_DRAW_MAX = 6000;   // cap so a long day cannot stall the canvas
let histPath = [], histDirty = false;
let tempLevel = {};
let noFixWarned = false;
let conn = { state:'demo', msg:'' };
let source = null;

/* ---------- gauge ---------- */
const G = { cx:200, cy:200, r:160, a0:135, a1:405 };
const pt = (a, r = G.r) => { const t = a * Math.PI / 180; return [G.cx + r * Math.cos(t), G.cy + r * Math.sin(t)]; };
const arc = (a0, a1, r = G.r) => {
  const [x0, y0] = pt(a0, r), [x1, y1] = pt(a1, r);
  return `M${x0.toFixed(2)} ${y0.toFixed(2)} A${r} ${r} 0 ${a1 - a0 > 180 ? 1 : 0} 1 ${x1.toFixed(2)} ${y1.toFixed(2)}`;
};
const rpmAngle = (rpm) => G.a0 + (G.a1 - G.a0) * Math.min(1, Math.max(0, rpm / RPM_MAX));
const gaugeAngle = (v, max) => G.a0 + (G.a1 - G.a0) * Math.min(1, Math.max(0, v / max));
$('#gTrack').setAttribute('d', arc(G.a0, G.a1));
$('#gVal').setAttribute('d', arc(G.a0, G.a1));
function buildGauge(mode){                           // 'rpm' (EV) or 'speed' (CV)
  const ns = 'http://www.w3.org/2000/svg', g = $('#gTicks');
  g.textContent = '';
  const max = mode === 'rpm' ? RPM_MAX : SPEED_MAX, minor = mode === 'rpm' ? 500 : 10, major = mode === 'rpm' ? 1000 : 20;
  $('#gRed').setAttribute('d', mode === 'rpm' ? arc(rpmAngle(REDLINE), G.a1, G.r + 16) : '');
  for (let v = 0; v <= max; v += minor) {
    const a = gaugeAngle(v, max), isMajor = v % major === 0;
    const [x0, y0] = pt(a, G.r - 11 - (isMajor ? 12 : 4)), [x1, y1] = pt(a, G.r - 11);
    const l = document.createElementNS(ns, 'line');
    l.setAttribute('x1', x0); l.setAttribute('y1', y0); l.setAttribute('x2', x1); l.setAttribute('y2', y1);
    l.setAttribute('class', isMajor ? 'g-tick' : 'g-tick minor'); g.appendChild(l);
    if (isMajor) {
      const [tx, ty] = pt(a, G.r - 40);
      const t = document.createElementNS(ns, 'text');
      t.setAttribute('x', tx); t.setAttribute('y', ty); t.setAttribute('class', 'g-label');
      t.textContent = mode === 'rpm' ? v / 1000 : v; g.appendChild(t);
    }
  }
  $('#rpmUnit').textContent = mode === 'rpm' ? 'rpm' : 'km/h top speed';
}
buildGauge('rpm');

/* ---------- temperature rows ---------- */
for (const [key, l] of Object.entries(LIMITS)) {
  const li = document.createElement('li');
  li.dataset.key = key;
  li.innerHTML = `<span>${l.label}</span>
    <div class="t-bar"><i></i><b style="left:${l.warn / l.max * 100}%"></b><b class="crit" style="left:${l.crit / l.max * 100}%"></b></div>
    <output>–</output>`;
  $('#tempList').appendChild(li);
}

/* ---------- formatting ---------- */
const fmtLap = (s) => {
  if (!isNum(s)) return '–';
  const m = Math.floor(s / 60), r = s - m * 60;
  return `${m}:${r.toFixed(2).padStart(5, '0')}`;
};
const fmtRun = (s) => { const m = Math.floor(s / 60), r = s - m * 60; return `${m}:${r.toFixed(1).padStart(4, '0')}`; };
const fix = (v, d) => isNum(v) ? v.toFixed(d) : '–';

/* ---------- event log ---------- */
function log(msg, level = 'info'){
  const li = document.createElement('li');
  li.dataset.level = level;
  const t = document.createElement('time'); t.textContent = new Date().toLocaleTimeString();
  const s = document.createElement('span'); s.textContent = msg;
  li.append(t, s);
  $('#logList').prepend(li);
  while ($('#logList').children.length > 50) $('#logList').lastChild.remove();
  $('#logEmpty').hidden = true;
}

/* ---------- data intake (Firebase-style tree) ---------- */
function applyAt(path, data, silent){
  const segs = String(path || '/').split('/').filter(Boolean);
  // Only the kart writing to /live counts as a heartbeat. Our own startPoint,
  // race and session writes also arrive through here and must not fake liveness.
  if (segs[0] === 'live' || (!segs.length && data && typeof data.live === 'object')) livePush = true;
  // The route polyline is rebuilt from the tree, not appended to, because the
  // kart reuses its batch keys once the retention window wraps. Marking it dirty
  // and re-deriving keeps the drawn route identical to what the database holds.
  if (segs[0] === 'history' || (!segs.length && data && typeof data.history === 'object')) histDirty = true;
  if (!segs.length) root = (data && typeof data === 'object') ? data : {};
  else {
    let o = root;
    for (let i = 0; i < segs.length - 1; i++) {
      if (typeof o[segs[i]] !== 'object' || o[segs[i]] === null) o[segs[i]] = {};
      o = o[segs[i]];
    }
    const k = segs[segs.length - 1];
    if (data === null) delete o[k]; else o[k] = data;
  }
  if (!root.live || typeof root.live !== 'object') root.live = {};
  if (!root.race || typeof root.race !== 'object') root.race = {};
  if (!silent) onData();
}

function latestLive(){
  const live = root.live;
  if (!live || typeof live !== 'object') return {};
  const records = Object.entries(live).filter(([, value]) =>
    value && typeof value === 'object' &&
    (isNum(value.latitude) || isNum(value.lat)) &&
    (isNum(value.longitude) || isNum(value.lon))
  );
  if (records.length) {
    records.sort(([a], [b]) => a < b ? -1 : a > b ? 1 : 0);
    const value = records[records.length - 1][1];
    return {
      ...value,
      latitude: isNum(value.latitude) ? value.latitude : value.lat,
      longitude: isNum(value.longitude) ? value.longitude : value.lon
    };
  }
  if (isNum(live.latitude) || isNum(live.lat)) {
    return {
      ...live,
      latitude: isNum(live.latitude) ? live.latitude : live.lat,
      longitude: isNum(live.longitude) ? live.longitude : live.lon
    };
  }
  return {};
}

function onData(){
  const now = performance.now();
  const L = latestLive();
  if (typeof L.name === 'string' && L.name.trim()) $('#kartName').textContent = L.name.trim();
  // Liveness is measured purely on ARRIVAL time of a real /live push. The kart's
  // own timestamp is synced once via AT+HTTPHEAD and then drifts, so letting it
  // into this calculation made every fresh upload look stale and the badge flickered.
  if (livePush) lastRx = now;
  livePush = false;
  if (isNum(L.lap)) {
    lapClock = { lap:L.lap, elapsed:isNum(L.lapElapsed) ? L.lapElapsed : 0, at:now };
    if (prevLap !== null && L.lap > prevLap && isNum(L.latitude)) { sfPoint = [L.latitude, L.longitude]; lapMarks.push(trail.length); }
    prevLap = L.lap;
  }
  if (isNum(L.latitude) && isNum(L.longitude) && !(L.latitude === 0 && L.longitude === 0)) {
    setOsmKart(L.latitude, L.longitude);
    const last = trail[trail.length - 1];
    if (!last || last[0] !== L.latitude || last[1] !== L.longitude) {
      trail.push([L.latitude, L.longitude]);
      if (trail.length > 4000) {
        const cutN = trail.length - 4000;
        trail.splice(0, cutN);
        lapMarks = lapMarks.map(i => i - cutN).filter(i => i >= 0);
      }
    }
  } else if (!noFixWarned && Object.keys(L).length) {
    noFixWarned = true;                       // data arriving, but not as latitude/longitude
    log('Kart data arrived without coordinates at ' + kartPath() + '/live. Check the ESP32 telemetry payload.', 'bad');
  }
  const power = isNum(L.power) ? L.power : (isNum(L.voltage) && isNum(L.current) ? L.voltage * L.current / 1000 : null);
  const t = Date.now();
  hist.push({ t, speed:isNum(L.speed) ? L.speed : null, power });
  while (hist.length && hist[0].t < t - 120000) hist.shift();
  scheduleRender();
}

/* ---------- rendering ---------- */
let rafPending = false, lastCanvas = 0;
function scheduleRender(){
  if (rafPending) return;
  rafPending = true;
  requestAnimationFrame(() => {
    rafPending = false;
    renderReadouts();
    renderConn(latestLive());
    renderLaps();
    renderSession();
    renderStartPoint();
    const now = performance.now();
    if (now - lastCanvas > 180) { lastCanvas = now; drawMap(); drawTrend(); }
  });
}

function renderReadouts(){
  const L = latestLive();
  $('#speed').textContent = isNum(L.speed) ? Math.round(L.speed) : '0';
  const gv = $('#gVal');
  if (page === 'cv') {                               // CV: the arc shows speed, bottom line shows top speed
    if (isNum(L.speed) && L.speed > topSpeed) topSpeed = L.speed;
    $('#rpm').textContent = Math.round(topSpeed);
    const pct = isNum(L.speed) ? Math.min(100, Math.max(0, L.speed / SPEED_MAX * 100)) : 0;
    gv.setAttribute('stroke-dasharray', `${pct.toFixed(2)} 100`);
    gv.classList.remove('over');
  } else {
    $('#rpm').textContent = isNum(L.rpm) ? Math.round(L.rpm) : '0';
    const pct = isNum(L.rpm) ? Math.min(100, Math.max(0, L.rpm / RPM_MAX * 100)) : 0;
    gv.setAttribute('stroke-dasharray', `${pct.toFixed(2)} 100`);
    gv.classList.toggle('over', isNum(L.rpm) && L.rpm >= REDLINE);
  }
  renderConn(L);

  const soc = isNum(L.soc) ? Math.min(100, Math.max(0, L.soc)) : null;
  $('#soc').textContent = soc === null ? '–' : `${Math.round(soc)}%`;
  const fill = $('#socFill');
  fill.style.width = `${soc ?? 0}%`;
  fill.style.setProperty('--c', soc === null ? 'var(--muted)' : soc < 20 ? 'var(--red)' : soc < 35 ? 'var(--amber)' : 'var(--green)');
  $('#socMeter').setAttribute('aria-valuenow', soc ?? 0);

  $('#voltage').textContent = fix(L.voltage, 1);
  $('#current').textContent = isNum(L.current) ? Math.round(L.current) : '–';
  $('#current').className = isNum(L.current) && L.current < -1 ? 'good' : '';
  const p = isNum(L.power) ? L.power : (isNum(L.voltage) && isNum(L.current) ? L.voltage * L.current / 1000 : null);
  $('#power').textContent = fix(p, 1);

  for (const li of $('#tempList').children) {
    const key = li.dataset.key, lim = LIMITS[key], v = L[key];
    li.querySelector('output').textContent = isNum(v) ? `${Math.round(v)}°` : '–';
    li.querySelector('.t-bar i').style.width = isNum(v) ? `${Math.min(100, Math.max(0, v / lim.max * 100))}%` : '0';
    const level = !isNum(v) ? 'none' : v >= lim.crit ? 'crit' : v >= lim.warn ? 'warn' : 'ok';
    li.dataset.level = level;
    const prev = tempLevel[key];
    if (prev && prev !== level && level !== 'none') {
      if (level === 'crit') log(`${lim.label} temperature ${Math.round(v)} °C — above the ${lim.crit} °C cut-back limit`, 'bad');
      else if (level === 'warn') log(`${lim.label} temperature ${Math.round(v)} °C — above the ${lim.warn} °C warning limit`, 'warn');
      else if (prev === 'warn' || prev === 'crit') log(`${lim.label} temperature back to normal (${Math.round(v)} °C)`, 'good');
    }
    tempLevel[key] = level;
  }
}

/* ---------- kart connection tiles (sent by the firmware) ---------- */
const CONN_LEVEL = {
  gps:    { RECEIVED:'ok', LAST_KNOWN:'warn', WAITING:'bad' },
  signal: { GOOD:'ok', FAIR:'warn', POOR:'bad', 'NO SIGNAL':'bad' },
  sim:    { OK:'ok', FAIL:'bad' },
  net:    { OK:'ok', FAIL:'bad' }
};
const CONN_TEXT = { RECEIVED:'Fix', LAST_KNOWN:'Last known', WAITING:'Searching', GOOD:'Good', FAIR:'Fair', POOR:'Poor', 'NO SIGNAL':'No signal', OK:'OK', FAIL:'Failed', UNKNOWN:'Unknown' };
let connPrev = {};

// Single source of truth for "is the kart talking right now". The Firebase
// Firebase retains pushed /live samples after the kart stops, so use arrival
// time rather than stored data to decide whether telemetry is fresh.
function kartIsLive(){
  if (cfg.source === 'demo') return true;
  if (conn.state === 'error') return false;
  if (!lastRx) return false;
  return (performance.now() - lastRx) / 1000 < OFFLINE_AFTER;
}

function renderConn(L){
  // Offline means the stored payload is history, not telemetry, so every value
  // is replaced by a placeholder. Only the last-seen clock survives, because
  // knowing when it went quiet is genuinely useful.
  const live = kartIsLive();
  const v = (k) => live ? L[k] : null;
  for (const [key, id] of [['gps', '#cGps'], ['signal', '#cSignal'], ['sim', '#cSim'], ['net', '#cNet']]) {
    const raw = v(key), el = $(id);
    el.querySelector('dd').textContent = typeof raw === 'string' ? (CONN_TEXT[raw] || raw) : '–';
    el.dataset.level = raw ? (CONN_LEVEL[key][raw] || 'warn') : '';
    if (raw && connPrev[key] && connPrev[key] !== raw) {
      const lvl = CONN_LEVEL[key][raw] || 'warn';
      log(`${el.querySelector('dt').textContent}: ${CONN_TEXT[raw] || raw}`, lvl === 'ok' ? 'good' : lvl === 'warn' ? 'warn' : 'bad');
    }
    if (raw) connPrev[key] = raw;
  }
  const batt = $('#cBatt');
  const soc = v('soc'), volts = v('voltage');
  if (isNum(soc)) {
    const pct = Math.round(soc);
    batt.querySelector('dd').textContent = isNum(volts) ? `${pct}% · ${volts.toFixed(2)} V` : `${pct}%`;
    batt.dataset.level = pct < 20 ? 'bad' : pct < 35 ? 'warn' : 'ok';
  } else {
    batt.querySelector('dd').textContent = '–';
    batt.dataset.level = '';
  }
  const parts = [];
  if (isNum(L.timestamp)) {
    parts.push(live ? `Last update from the kart: ${new Date(L.timestamp).toLocaleTimeString()}`
                    : `Last seen: ${new Date(L.timestamp).toLocaleTimeString()}`);
  }
  if (live) {
    if (isNum(L.latitude)) parts.push(`${L.latitude.toFixed(6)}, ${L.longitude.toFixed(6)}`);
    if (typeof L.name === 'string' && L.name.trim()) parts.push(`Board: ${L.name.trim()}`);
  } else {
    parts.push('Kart offline — live values hidden');
  }
  $('#connNote').textContent = parts.length ? parts.join('   ·   ') : 'Waiting for the kart…';
}

const sessionActive = () => !!(root.session && root.session.active === true && isNum(root.session.firstLap));

function lapsView(){
  const race = root.race || {};
  const all = Object.keys(race).filter(k => /^lap\d+$/.test(k))
    .map(k => ({ n:+k.slice(3), t:+race[k] })).filter(l => isNum(l.t) && l.t > 0)
    .sort((a, b) => a.n - b.n);
  const active = sessionActive();
  const first = active ? root.session.firstLap : 1;
  const laps = active ? all.filter(l => l.n >= first).map(l => ({ n:l.n - first + 1, t:l.t })) : all;
  const best = laps.length ? Math.min(...laps.map(l => l.t)) : null;
  return { laps, best, active };
}

function renderLaps(){
  const { laps, best, active } = lapsView();
  const sig = (active ? root.session.id : 'none') + '|' + laps.map(l => l.n + ':' + l.t).join(',');
  if (sig === lapSig) return;
  const sessionChanged = lapSig.split('|')[0] !== sig.split('|')[0];
  lapSig = sig;
  if (sessionChanged) prevBest = null;
  $('#lapScope').textContent = active ? `Laps in ${root.session.name || 'this session'}` : 'No session running. Showing every recorded lap.';
  const last = laps[laps.length - 1];
  $('#lastLap').textContent = last ? fmtLap(last.t) : '–';
  $('#bestLap').textContent = fmtLap(best);
  const d = $('#delta');
  if (last && isNum(best)) {
    const dv = last.t - best;
    d.textContent = dv < 0.005 ? 'Best' : `+${dv.toFixed(2)} s`;
    d.className = dv < 0.005 ? 'good' : dv < 1 ? '' : 'warn';
  } else { d.textContent = '–'; d.className = ''; }

  const ol = $('#lapList');
  ol.textContent = '';
  for (const l of laps.slice(-15).reverse()) {
    const li = document.createElement('li');
    const isBest = isNum(best) && Math.abs(l.t - best) < 0.005;
    if (isBest) li.className = 'best';
    li.innerHTML = `<span>Lap ${l.n}</span><span class="t">${fmtLap(l.t)}</span><span class="d">${isBest ? 'Best' : '+' + (l.t - best).toFixed(2)}</span>`;
    ol.appendChild(li);
  }
  $('#lapEmpty').textContent = active ? 'Laps appear here once the kart completes its first timed lap.' : 'Laps appear here each time the kart crosses the start/finish sensor.';
  $('#lapEmpty').hidden = laps.length > 0;

  if (isNum(best)) {
    if (prevBest !== null && best < prevBest - 0.001) log(`New best lap: ${fmtLap(best)}`, 'good');
    prevBest = best;
  }
}

function renderSession(){
  const active = sessionActive(), S = root.session || {};
  document.body.classList.toggle('in-session', active);
  $('#sessionInfo').hidden = !active;
  $('#sessName').textContent = active ? (S.name || 'Session') : '';
  const btn = $('#sessionBtn');
  btn.textContent = active ? 'End session' : 'Start session';
  btn.className = active ? 'btn danger' : 'btn';
  for (const id of ['#settingsBtn', '#signOutBtn']) {
    $(id).disabled = active;
    $(id).title = active ? 'End the session first' : '';
  }
  const key = active ? S.id : null;
  if (key !== renderSession.last) {
    if (renderSession.last !== undefined) log(active ? `Session started: ${S.name || 'Session'}` : 'Session ended', active ? 'good' : 'info');
    renderSession.last = key;
  }
}

/* ---------- circuit: COASST Karting Track, Coimbatore ----------
   Centre line traced from the track's turn-by-turn guide, starting at the
   start/finish line in the racing direction. Length is approximate.        */
const CIRCUIT = {
  lengthM:850,
  path:[[1060,130],[1150,138],[1200,150],[1225,180],[1232,230],[1240,290],[1255,330],[1275,380],[1300,450],[1315,510],
    [1305,545],[1270,562],[1200,560],[1100,552],[950,530],[800,505],[740,480],[715,440],[730,405],[770,395],[850,405],
    [950,440],[1030,470],[1080,465],[1110,440],[1105,410],[1070,350],[1030,300],[990,265],[940,255],[880,262],[820,285],
    [760,297],[720,290],[690,265],[650,230],[610,205],[560,190],[450,178],[370,176],[335,190],[322,215],[345,245],[420,285],
    [500,320],[530,345],[540,380],[515,405],[460,412],[400,400],[320,360],[220,290],[130,210],[100,160],[98,110],[120,78],
    [160,62],[220,60],[350,68],[500,82],[700,100],[900,118]],
  turns:[['T1',1225,180],['T2',1255,330],['T3',1295,550],['T4',720,440],['T5',1105,440],['T6',990,265],
    ['T7',745,296],['T8',630,218],['T9',328,205],['T10',535,385],['T11',100,165],['T12',130,72]]
};
const GEO = (() => {                                   // smooth, scale to metres (x east, y north)
  const P = CIRCUIT.path, n = P.length, px = [];
  for (let i = 0; i < n; i++) {
    const p0 = P[(i - 1 + n) % n], p1 = P[i], p2 = P[(i + 1) % n], p3 = P[(i + 2) % n];
    for (let k = 0; k < 10; k++) {
      const t = k / 10, t2 = t * t, t3 = t2 * t;
      px.push([0, 1].map(d => 0.5 * (2 * p1[d] + (-p0[d] + p2[d]) * t + (2 * p0[d] - 5 * p1[d] + 4 * p2[d] - p3[d]) * t2 + (-p0[d] + 3 * p1[d] - 3 * p2[d] + p3[d]) * t3)));
    }
  }
  const N = px.length, dist = (a, b) => Math.hypot(b[0] - a[0], b[1] - a[1]);
  let Lp = 0; for (let i = 0; i < N; i++) Lp += dist(px[i], px[(i + 1) % N]);
  const k = CIRCUIT.lengthM / Lp;
  const pts = px.map(([x, y]) => [x * k, -y * k]);
  const ds = [], cum = []; let L = 0;
  for (let i = 0; i < N; i++) { cum[i] = L; ds[i] = dist(pts[i], pts[(i + 1) % N]); L += ds[i]; }
  const turns = CIRCUIT.turns.map(([name, x, y]) => {
    let best = 0, bd = Infinity;
    px.forEach((p, i) => { const d = (p[0] - x) ** 2 + (p[1] - y) ** 2; if (d < bd) { bd = d; best = i; } });
    return { name, frac:cum[best] / L };
  });
  return { pts, ds, cum, L, N, turns };
})();
const DEMO_ORIGIN = { lat:11.0168, lon:76.9558 };
const toLatLon = ([x, y]) => [DEMO_ORIGIN.lat + y / 110540, DEMO_ORIGIN.lon + x / (111320 * Math.cos(DEMO_ORIGIN.lat * Math.PI / 180))];
let demoRef = null;          // demo: the circuit itself; live: last full GPS lap is used instead
let lapMarks = [];           // trail indices where the kart crossed start/finish

/* ---------- canvases ---------- */
const tok = (n) => getComputedStyle(document.documentElement).getPropertyValue(n).trim();
function fitCanvas(c){
  const dpr = window.devicePixelRatio || 1, w = c.clientWidth, h = c.clientHeight;
  if (c.width !== Math.round(w * dpr) || c.height !== Math.round(h * dpr)) { c.width = Math.round(w * dpr); c.height = Math.round(h * dpr); }
  const ctx = c.getContext('2d');
  ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
  ctx.clearRect(0, 0, w, h);
  return { ctx, w, h };
}
function emptyMsg(ctx, w, h, text){
  ctx.fillStyle = tok('--muted');
  ctx.font = `500 15px ${tok('--body')}`;
  ctx.textAlign = 'center';
  ctx.fillText(text, w / 2, h / 2);
}

/* ---------- retained sample history (everything the kart posted to /history) ---------- */
// Accept both one-record push children and the older batch-of-records shape.
function rebuildHistoryPath(){
  histDirty = false;
  histPath = [];
  const H = root.history;
  if (!H || typeof H !== 'object') return;
  const rows = [];
  const addSample = (groupKey, key, sample) => {
    if (!sample || typeof sample !== 'object') return;
    const latitude = isNum(sample.latitude) ? sample.latitude : sample.lat;
    const longitude = isNum(sample.longitude) ? sample.longitude : sample.lon;
    if (!isNum(latitude) || !isNum(longitude) || (latitude === 0 && longitude === 0)) return;
    rows.push({
      t: isNum(sample.timestamp) ? sample.timestamp : null,
      k: `${groupKey}/${key}`,
      p: [latitude, longitude]
    });
  };
  for (const [groupKey, value] of Object.entries(H)) {
    if (!value || typeof value !== 'object') continue;
    if (isNum(value.lat) || isNum(value.latitude)) {
      addSample(groupKey, groupKey, value);
      continue;
    }
    for (const [key, sample] of Object.entries(value)) addSample(groupKey, key, sample);
  }
  if (rows.length < 2) return;
  rows.sort((a, b) => {
    if (a.t !== null && b.t !== null) return a.t - b.t || a.k.localeCompare(b.k);
    if (a.t !== null) return 1;
    if (b.t !== null) return -1;
    return a.k.localeCompare(b.k, undefined, { numeric:true });
  });
  for (const r of (rows.length > HISTORY_DRAW_MAX ? rows.slice(-HISTORY_DRAW_MAX) : rows)) histPath.push(r.p);
}

function drawMap(){
  const c = $('#mapCanvas'); if (!c.clientWidth) return;
  const { ctx, w, h } = fitCanvas(c);
  if (histDirty) rebuildHistoryPath();
  const ref = demoRef || (lapMarks.length >= 2 ? trail.slice(lapMarks[lapMarks.length - 2], lapMarks[lapMarks.length - 1] + 1) : null);
  const base = ref || trail;
  if (base.length < 2) { emptyMsg(ctx, w, h, 'Waiting for a GPS fix…'); return; }
  const lat0 = base[0][0], kx = Math.cos(lat0 * Math.PI / 180) * 111320, ky = 110540;
  const proj = ([la, lo]) => [lo * kx, -la * ky];
  const B = base.map(proj), T = trail.map(proj);
  // Points replayed from the kart's on-board buffer, drawn as a dashed line so
  // a recovered outage is visually distinct from live tracking.
  const K = bufferedPath.map(proj);
  // The whole driven route for the retained window. Kept out of the fit unless it
  // has real content, so one stray fix cannot zoom the map out to nothing.
  const H = histPath.length > 1 ? histPath.map(proj) : [];
  let minX = Infinity, minY = Infinity, maxX = -Infinity, maxY = -Infinity;
  for (const [x, y] of B.concat(ref ? [] : T).concat(K).concat(H)) { if (x < minX) minX = x; if (x > maxX) maxX = x; if (y < minY) minY = y; if (y > maxY) maxY = y; }
  const pad = 30, bw = Math.max(maxX - minX, 20), bh = Math.max(maxY - minY, 20);
  const s = Math.min((w - pad * 2) / bw, (h - pad * 2) / bh);
  const ox = (w - bw * s) / 2 - minX * s, oy = (h - bh * s) / 2 - minY * s;
  const X = (p) => [p[0] * s + ox, p[1] * s + oy];
  const path = (pts, close) => { ctx.beginPath(); pts.forEach((p, i) => { const [x, y] = X(p); i ? ctx.lineTo(x, y) : ctx.moveTo(x, y); }); if (close) ctx.closePath(); };
  ctx.lineJoin = ctx.lineCap = 'round';
  path(B, !!ref); ctx.strokeStyle = tok('--trackc'); ctx.lineWidth = 12; ctx.stroke();
  // On top of the reference band but under the live trail, so the driven line
  // stays visible while the current position still reads as the brightest part.
  if (H.length > 1) { path(H); ctx.strokeStyle = tok('--muted'); ctx.lineWidth = 1.6; ctx.stroke(); }
  if (K.length > 1) { ctx.save(); ctx.setLineDash([7, 6]); path(K); ctx.strokeStyle = tok('--amber'); ctx.lineWidth = 2.5; ctx.stroke(); ctx.restore(); }
  if (T.length > 1) { path(T.slice(-260)); ctx.strokeStyle = tok('--blue'); ctx.lineWidth = 3.5; ctx.stroke(); }

  // start/finish
  const sf = ref ? B[0] : (sfPoint ? proj(sfPoint) : null);
  if (sf) { const [x, y] = X(sf); ctx.fillStyle = tok('--red'); ctx.fillRect(x - 3, y - 11, 6, 22); }

  // turn numbers, placed by distance along the lap
  if (ref) {
    const S = B.map(X), cum = [0];
    for (let i = 1; i < S.length; i++) cum[i] = cum[i - 1] + Math.hypot(S[i][0] - S[i - 1][0], S[i][1] - S[i - 1][1]);
    const total = cum[cum.length - 1];
    const cx = S.reduce((a, p) => a + p[0], 0) / S.length, cy = S.reduce((a, p) => a + p[1], 0) / S.length;
    ctx.font = `600 13px ${tok('--display')}`; ctx.textAlign = 'center'; ctx.textBaseline = 'middle';
    for (const t of GEO.turns) {
      const d = t.frac * total; let i = 1;
      while (i < cum.length - 1 && cum[i] < d) i++;
      const a = S[i - 1], b = S[i], f = (d - cum[i - 1]) / Math.max(1e-6, cum[i] - cum[i - 1]);
      const px = a[0] + (b[0] - a[0]) * f, py = a[1] + (b[1] - a[1]) * f;
      let nx = -(b[1] - a[1]), ny = b[0] - a[0]; const nl = Math.hypot(nx, ny) || 1; nx /= nl; ny /= nl;
      if (nx * (px - cx) + ny * (py - cy) < 0) { nx = -nx; ny = -ny; }        // push label to the outside
      const lx = px + nx * 17, ly = py + ny * 17, tw = ctx.measureText(t.name).width + 10;
      ctx.fillStyle = tok('--panel'); ctx.strokeStyle = tok('--line'); ctx.lineWidth = 1;
      ctx.beginPath(); ctx.roundRect ? ctx.roundRect(lx - tw / 2, ly - 9, tw, 18, 5) : ctx.rect(lx - tw / 2, ly - 9, tw, 18); ctx.fill(); ctx.stroke();
      ctx.fillStyle = tok('--muted'); ctx.fillText(t.name, lx, ly + 1);
    }
    ctx.textBaseline = 'alphabetic';
  } else {
    ctx.fillStyle = tok('--muted'); ctx.font = `500 13px ${tok('--body')}`; ctx.textAlign = 'left';
    ctx.fillText('Turn numbers appear after the first full lap.', 12, h - 10);
  }

  // kart
  if (T.length) {
    const [kxp, kyp] = X(T[T.length - 1]);
    ctx.beginPath(); ctx.arc(kxp, kyp, 11, 0, Math.PI * 2); ctx.fillStyle = tok('--panel'); ctx.fill();
    ctx.beginPath(); ctx.arc(kxp, kyp, 7, 0, Math.PI * 2); ctx.fillStyle = tok('--amber'); ctx.fill();
  }
}

function drawTrend(){
  const c = $('#trendCanvas'); if (!c.clientWidth) return;
  const { ctx, w, h } = fitCanvas(c);
  if (hist.length < 2) { emptyMsg(ctx, w, h, 'Waiting for data…'); return; }
  const now = Date.now(), t0 = now - 120000;
  const showPower = page !== 'cv';
  const L = 38, R = showPower ? 42 : 14, T = 24, B = 26, pw = w - L - R, ph = h - T - B;
  const sMax = Math.max(90, ...hist.map(p => p.speed || 0));
  const pv = hist.map(p => p.power).filter(isNum);
  const pMax = Math.max(10, Math.ceil(Math.max(...pv, Math.abs(Math.min(...pv, 0)) * 2) / 10) * 10), pMin = -pMax / 2;
  const x = (t) => L + (t - t0) / 120000 * pw;
  const ys = (v) => T + ph - v / sMax * ph;
  const yp = (v) => T + ph - (v - pMin) / (pMax - pMin) * ph;
  ctx.font = `500 13px ${tok('--body')}`;
  ctx.strokeStyle = tok('--line'); ctx.lineWidth = 1; ctx.fillStyle = tok('--muted');
  for (let i = 0; i <= 3; i++) {
    const y = T + ph * i / 3;
    ctx.beginPath(); ctx.moveTo(L, y); ctx.lineTo(w - R, y); ctx.stroke();
    ctx.textAlign = 'right'; ctx.fillText(Math.round(sMax * (1 - i / 3)), L - 6, y + 4);
    if (showPower) { ctx.textAlign = 'left'; ctx.fillText((pMax - (pMax - pMin) * i / 3).toFixed(0), w - R + 6, y + 4); }
  }
  ctx.textAlign = 'center';
  for (let s = 0; s <= 120; s += 30) ctx.fillText(s === 120 ? 'now' : `−${120 - s} s`, L + s / 120 * pw, h - 6);
  ctx.textAlign = 'left'; ctx.fillText('km/h', 0, 12);
  if (showPower) {
    ctx.textAlign = 'right'; ctx.fillText('kW', w, 12);
    ctx.setLineDash([4, 4]); ctx.beginPath(); ctx.moveTo(L, yp(0)); ctx.lineTo(w - R, yp(0)); ctx.stroke(); ctx.setLineDash([]);
  }
  const line = (key, y, color, width) => {
    ctx.beginPath(); let started = false;
    for (const p of hist) { if (!isNum(p[key])) { started = false; continue; } const X = x(p.t), Y = y(p[key]); started ? ctx.lineTo(X, Y) : ctx.moveTo(X, Y); started = true; }
    ctx.strokeStyle = color; ctx.lineWidth = width; ctx.lineJoin = 'round'; ctx.stroke();
  };
  if (showPower) line('power', yp, tok('--amber'), 1.6);
  line('speed', ys, tok('--blue'), 2.4);
}

/* ---------- OpenStreetMap: live kart marker fed by the Firebase listener ---------- */
const OSM_HOME = { lat:11.0168, lon:76.9558 };   // COASST Karting Track, Coimbatore
let osmMap = null, osmMarker = null, osmStart = null, osmPlaced = false;

function initOsm(){
  if (osmMap) return;
  const el = $('#osmMap');
  if (!el) return;
  if (typeof L === 'undefined') {                        // CDN blocked or offline
    el.dataset.osm = 'nofile';
    el.textContent = 'Map library could not be loaded (unpkg.com unreachable). Coordinates above are still live.';
    return;
  }
  if (!el.clientWidth) return;                           // still hidden behind the sign-in screen
  delete el.dataset.osm;
  el.textContent = '';
  osmMap = L.map(el, { scrollWheelZoom:false }).setView([OSM_HOME.lat, OSM_HOME.lon], 16);
  L.tileLayer('https://{s}.tile.openstreetmap.org/{z}/{x}/{y}.png', {
    attribution:'&copy; <a href="https://www.openstreetmap.org/copyright">OpenStreetMap</a> contributors',
    maxZoom:19
  }).addTo(osmMap);
  osmMarker = L.circleMarker([OSM_HOME.lat, OSM_HOME.lon], {
    radius:9, weight:3, color:tok('--panel'), fillColor:tok('--amber'), fillOpacity:1
  }).addTo(osmMap);
  osmMarker.bindTooltip('Go-Kart', { direction:'top' });
}
const themeOsmMarker = () => { if (osmMarker) osmMarker.setStyle({ color:tok('--panel'), fillColor:tok('--amber') }); if (osmStart) osmStart.setStyle({ color:tok('--panel'), fillColor:tok('--green') }); };
function setOsmKart(lat, lon){
  $('#latOut').textContent = lat.toFixed(6);            // always shown, map or no map
  $('#lonOut').textContent = lon.toFixed(6);
  initOsm();
  if (!osmMarker) return;
  osmMarker.setLatLng([lat, lon]);
  if (!osmPlaced) { osmPlaced = true; osmMap.setView([lat, lon], 17); }
}
function resetOsm(){
  osmPlaced = false;
  if (osmMarker) osmMarker.setLatLng([OSM_HOME.lat, OSM_HOME.lon]);
  if (osmStart) { osmMap.removeLayer(osmStart); osmStart = null; }
  $('#latOut').textContent = '–';
  $('#lonOut').textContent = '–';
}

/* ---------- start / finish point (lap line) ---------- */
const startPoint = () => {
  const s = root.startPoint;
  return (s && isNum(s.latitude) && isNum(s.longitude)) ? s : null;
};
function renderStartPoint(){
  const s = startPoint();
  const info = $('#startInfo');
  info.className = s ? 'start-info set' : 'start-info';
  info.textContent = s
    ? `Start point: ${s.latitude.toFixed(6)}, ${s.longitude.toFixed(6)}`
    : 'No start point set — park the kart on the line, then press the button.';
  if (!s) return;
  initOsm();
  if (!osmMap) return;
  if (!osmStart) {
    osmStart = L.circleMarker([s.latitude, s.longitude], {
      radius:8, weight:3, color:tok('--panel'), fillColor:tok('--green'), fillOpacity:1
    }).addTo(osmMap).bindTooltip('Start / finish line', { direction:'top' });
  } else {
    osmStart.setLatLng([s.latitude, s.longitude]);
  }
}
async function setStartPoint(){
  const btn = $('#setStartBtn');
  const live = latestLive();
  if (!isNum(live.latitude) || !isNum(live.longitude)) {
    log('No GPS fix yet, so there is no position to store. Wait for the kart to report a location.', 'warn');
    return;
  }
  btn.disabled = true;
  btn.textContent = 'Saving…';
  try {
    await dbWrite('PUT', 'startPoint', {
      latitude:live.latitude, longitude:live.longitude, setAt:SV
    });
    log('Start point stored. The kart picks it up within 30 s, then a lap is counted each time it crosses.', 'good');
  } catch (x) {
    log(x.message, 'bad');
  } finally {
    btn.disabled = false;
    btn.textContent = 'Set start point';
  }
}

/* ---------- 10 Hz tick: running lap clock, session clock, connection status ---------- */
/* ---------- replayed offline track (kart-side buffer) ---------- */
const TRACK_POLL_MS = 4000, TRACK_MAX_POINTS = 2000;
function markTrackSeen(){
  if (cfg.source === 'demo') return;
  const now = performance.now();
  if (now - trackPollAt < TRACK_POLL_MS) return;
  trackPollAt = now;
  fetch(kartUrl('track'))
    .then(r => r.ok ? r.json() : null)
    .then(d => {
      if (!d || typeof d !== 'object') return;
      let added = 0;
      // Each key is one batch: [[lat,lon,ts?], ...]
      for (const k of Object.keys(d).sort()) {
        if (trackSeen[k]) continue;
        trackSeen[k] = true;
        const batch = d[k];
        if (!Array.isArray(batch)) continue;
        for (const p of batch) {
          if (!Array.isArray(p) || !isNum(p[0]) || !isNum(p[1])) continue;
          if (p[0] === 0 && p[1] === 0) continue;
          bufferedPath.push([p[0], p[1]]);
          added++;
        }
      }
      if (!added) return;
      if (bufferedPath.length > TRACK_MAX_POINTS) bufferedPath.splice(0, bufferedPath.length - TRACK_MAX_POINTS);
      // Drop stale keys so a long-running page does not grow without bound.
      const keys = Object.keys(trackSeen);
      if (keys.length > 120) for (const k of keys.slice(0, keys.length - 120)) delete trackSeen[k];
      const live = latestLive();
      const q = isNum(live.queued) ? live.queued : 0;
      log(`${added} buffered position(s) replayed from the kart${q ? `, ${q} still queued on board` : ''}.`, 'good');
      drawMap();
    })
    .catch(() => {});
}

function setStatus(state, text){
  const s = $('#status');
  if (s.dataset.state !== state) s.dataset.state = state;
  if ($('#statusText').textContent !== text) $('#statusText').textContent = text;
}
setInterval(() => {
  if (document.body.classList.contains('locked')) return;
  const now = performance.now(), age = lastRx ? (now - lastRx) / 1000 : Infinity;
  markTrackSeen();
  if (lapClock.lap !== null) {
    const active = sessionActive();
    const shown = active ? lapClock.lap - root.session.firstLap + 1 : lapClock.lap;
    if (active && shown < 1) {
      $('#lapNo').textContent = '–';
      $('#lapNow').textContent = 'Waiting';
    } else {
      $('#lapNo').textContent = shown;
        const run = lapClock.elapsed + (age < OFFLINE_AFTER ? Math.min((now - lapClock.at) / 1000, OFFLINE_AFTER) : 0);
      $('#lapNow').textContent = fmtRun(run);
    }
  }
  if (sessionActive() && isNum(root.session.startedAt)) {
    const sec = Math.max(0, (Date.now() - root.session.startedAt) / 1000);
    const h = Math.floor(sec / 3600), m = Math.floor(sec / 60) % 60, ss = Math.floor(sec % 60);
    $('#sessTime').textContent = (h ? h + ':' + String(m).padStart(2, '0') : m) + ':' + String(ss).padStart(2, '0');
  }
    if (cfg.source === 'demo') setStatus('demo', 'Demo data');
    else if (conn.state === 'error') setStatus('offline', 'Offline');
    else if (!lastRx) setStatus('offline', 'Connecting…');
    else if (age < OFFLINE_AFTER) setStatus('live', 'Live');
    else setStatus('offline', `Offline — no data for ${Math.round(age)} s`);
    // Re-render the connection panel on the tick so the placeholders appear and
    // disappear with the liveness window, not only when fresh data arrives.
    renderConn(latestLive());
}, 100);

/* ---------- sign-in (Firebase Authentication REST API) ---------- */
const authMsg = (code = '') => {
  if (/INVALID_LOGIN_CREDENTIALS|INVALID_PASSWORD|EMAIL_NOT_FOUND|INVALID_EMAIL|MISSING_PASSWORD/.test(code))
    return SITE.loginEmail ? 'Wrong password.' : 'Wrong email or password.';
  if (/TOO_MANY_ATTEMPTS/.test(code)) return 'Too many wrong tries. Wait a few minutes, then try again.';
  if (/USER_DISABLED/.test(code)) return 'This team account has been disabled in Firebase.';
  if (/API_KEY|PERMISSION_DENIED|CONFIGURATION_NOT_FOUND|OPERATION_NOT_ALLOWED/.test(code)) return 'Sign-in isn\'t set up yet. Check the API key and that Email/Password sign-in is enabled in Firebase.';
  return 'Sign-in failed (' + code + ').';
};
let refreshTimer = null;
const auth = {
  user:null,
  async signIn(email, password){
    if (!LIVE) {
      if (password !== 'demo') throw new Error('Wrong password.');
      this.set({ email:'demo', idToken:'', refreshToken:'demo', exp:0 });
      return;
    }
    let r, j;
    try {
      r = await fetch(`https://identitytoolkit.googleapis.com/v1/accounts:signInWithPassword?key=${encodeURIComponent(SITE.apiKey)}`, {
        method:'POST', headers:{ 'Content-Type':'application/json' },
        body:JSON.stringify({ email, password, returnSecureToken:true })
      });
      j = await r.json();
    } catch { throw new Error('Can\'t reach the sign-in server. Check the internet connection.'); }
    if (!r.ok) throw new Error(authMsg(j && j.error && j.error.message));
    this.set({ email, idToken:j.idToken, refreshToken:j.refreshToken, exp:Date.now() + (+j.expiresIn) * 1000 });
  },
  async refresh(){
    if (!LIVE) return;
    const r = await fetch(`https://securetoken.googleapis.com/v1/token?key=${encodeURIComponent(SITE.apiKey)}`, {
      method:'POST', headers:{ 'Content-Type':'application/x-www-form-urlencoded' },
      body:'grant_type=refresh_token&refresh_token=' + encodeURIComponent(this.user.refreshToken)
    });
    const j = await r.json();
    if (!r.ok) throw new Error('expired');
    this.set({ email:this.user.email, idToken:j.id_token, refreshToken:j.refresh_token, exp:Date.now() + (+j.expires_in) * 1000 });
  },
  set(u){
    this.user = u;
    store.set('zion.auth', { email:u.email, refreshToken:u.refreshToken });
    clearTimeout(refreshTimer);
    if (LIVE && u.exp) refreshTimer = setTimeout(renewLogin, Math.max(60000, u.exp - Date.now() - 300000));
  },
  signOut(){ this.user = null; clearTimeout(refreshTimer); store.set('zion.auth', null); }
};
async function renewLogin(){
  try { await auth.refresh(); if (cfg.source === 'firebase') connect(false); }
  catch { log('The sign-in expired. Sign in again to keep watching.', 'warn'); lock(); }
}
document.addEventListener('visibilitychange', () => {
  if (!document.hidden && LIVE && auth.user && auth.user.exp && Date.now() > auth.user.exp - 60000) renewLogin();
});

/* ---------- database writes (sessions) ---------- */
const kartUrl = (path) => {
  const u = new URL(SITE.firebaseUrl.replace(/\/+$/, '') + '/' + kartPath().replace(/^\/+|\/+$/g, '') + (path ? '/' + path : '') + '.json');
  if (auth.user && auth.user.idToken) u.searchParams.set('auth', auth.user.idToken);
  return u;
};
const resolveSV = (v) => (v && typeof v === 'object')
  ? (v['.sv'] === 'timestamp' ? Date.now() : Object.fromEntries(Object.entries(v).map(([k, x]) => [k, resolveSV(x)])))
  : v;
async function dbWrite(method, path, body){
  if (cfg.source === 'demo') {                    // demo kart: keep everything on this screen
    const b = resolveSV(body);
    if (method === 'PATCH') { for (const k in b) applyAt('/' + path + '/' + k, b[k], true); onData(); }
    else applyAt('/' + path, method === 'DELETE' ? null : b);
    return;
  }
  const u = kartUrl(path);
  u.searchParams.set('x-http-method-override', method);   // POST + override avoids a CORS preflight
  let r;
  try { r = await fetch(u, { method:'POST', body:body === undefined ? '' : JSON.stringify(resolveSV(body)) }); }
  catch { throw new Error('Can\'t reach the database. Check the internet connection.'); }
  if (!r.ok) throw new Error(r.status === 401 || r.status === 403
    ? 'The database refused the change. Check the rules allow signed-in users to write sessions.'
    : `Couldn't save to the database (error ${r.status}).`);
}

/* ---------- sources ---------- */
function startFirebase(){
  demoRef = null;
  const fail = (msg) => { conn = { state:'error', msg }; log(msg, 'bad'); return { stop(){} }; };
  let url;
  try { url = kartUrl(''); } catch { return fail('The database URL in the site setup is not a valid web address.'); }
  let es;
  try { es = new EventSource(url.toString()); }
  catch { return fail('This page is not allowed to open a connection to Firebase.'); }
  conn = { state:'connecting', msg:'' };
  let reportedError = false;
  es.addEventListener('open', () => { conn.state = 'open'; reportedError = false; });
  es.addEventListener('put', (e) => { try { const m = JSON.parse(e.data); applyAt(m.path, m.data); } catch {} });
  es.addEventListener('patch', (e) => {
    try { const m = JSON.parse(e.data); const base = m.path.replace(/\/$/, ''); for (const k in m.data) applyAt(base + '/' + k, m.data[k], true); onData(); } catch {}
  });
  es.addEventListener('cancel', () => { conn.state = 'error'; es.close(); log('Firebase refused to share the kart data. Check that the rules allow signed-in users to read it.', 'bad'); });
  es.addEventListener('auth_revoked', () => { es.close(); renewLogin(); });
  es.onerror = () => {
    if (es.readyState === EventSource.CLOSED) conn.state = 'error';
    if (!reportedError) { reportedError = true; log('Lost the connection to Firebase. Retrying automatically…', 'warn'); }
  };
  return { stop(){ es.close(); } };
}

function startDemo(){
  const sim = makeSim();
  demoRef = GEO.pts.map(toLatLon);
  conn = { state:'demo', msg:'' };
  const id = setInterval(() => {
    const u = sim.step(0.1);
    applyAt('/live', u.live, true);
    if (u.lap) applyAt('/race/lap' + u.lap.n, u.lap.t, true);
    onData();
  }, 100);
  log('Showing the demo kart.');
  return { stop(){ clearInterval(id); } };
}

function resetSession(){
  root = { live:{}, race:{} }; hist = []; trail = []; sfPoint = null; prevLap = null; prevBest = null;
  lapSig = ''; tempLevel = {}; lapMarks = []; lastRx = 0; livePush = false; lapClock = { lap:null, elapsed:0, at:0 };
  bufferedPath = []; trackSeen = {}; trackPollAt = 0;
  noFixWarned = false; topSpeed = 0; connPrev = {};
  $('#logList').textContent = ''; $('#logEmpty').hidden = false;
  renderSession.last = undefined;
  resetOsm();
  $('#lapNo').textContent = '–'; $('#lapNow').textContent = '0:00.0';
  renderReadouts(); renderConn(latestLive()); renderLaps(); renderSession(); drawMap(); drawTrend();
}

function connect(reset = true){
  if (source) source.stop();
  if (reset) resetSession();
  if (page === 'home') { source = null; return; }
  const k = SITE.karts[page];
  $('#kartName').textContent = cfg.source === 'demo' ? `${k.name} (demo)` : k.name;
  source = cfg.source === 'firebase' ? startFirebase() : startDemo();
}

/* ---------- demo simulator: a kart lapping a ~820 m circuit ---------- */
function makeSim(){
  const { pts:P, ds, cum, L:Ltot, N } = GEO;
  const W = 8;                                        // curvature over ±8 points (~±11 m) to smooth tracing noise
  const vC = P.map((p, i) => {
    const a = P[(i - W + N) % N], b = P[(i + W) % N];
    let dh = Math.atan2(b[1] - p[1], b[0] - p[0]) - Math.atan2(p[1] - a[1], p[0] - a[0]);
    dh = Math.atan2(Math.sin(dh), Math.cos(dh));
    let seg = 0; for (let k = -W; k < W; k++) seg += ds[(i + k + N) % N];
    return Math.min(22.5, Math.sqrt(9 / Math.max(Math.abs(dh) / seg, 1e-4)));
  });
  const vT = vC.map((_, i) => { let m = Infinity; for (let k = -3; k <= 3; k++) m = Math.min(m, vC[(i + k + N) % N]); return m; });
  for (let pass = 0; pass < 3; pass++)
    for (let i = N - 1; i >= 0; i--) { const j = (i + 1) % N; vT[i] = Math.min(vT[i], Math.sqrt(vT[j] ** 2 + 2 * 7 * ds[i])); }

  let s = 0, idx = 0, v = 6, t = 0, lap = 1, lapStart = 0, best = null;
  let soc = 0.86, mT = 31, cT = 29, bT = 30, Iprev = 0;
  return {
    step(dt){
      t += dt;
      const tgt = vT[idx] * (0.965 + 0.035 * Math.sin(t * 0.21 + lap));
      const v0 = v;
      if (tgt > v) v += Math.min(tgt - v, 4.8 * (1 - v / 26) * dt); else v = Math.max(tgt, v - 8.5 * dt);
      const a = (v - v0) / dt;
      s += v * dt;
      let lapDone = null;
      if (s >= Ltot) {
        s -= Ltot; idx = 0;
        const lt = +(t - lapStart).toFixed(2);
        lapStart = t; lapDone = { n:lap, t:lt };
        if (best === null || lt < best) best = lt;
        lap++;
      }
      while (idx < N - 1 && cum[idx + 1] <= s) idx++;
      const p0 = P[idx], p1 = P[(idx + 1) % N], f = Math.min(1, (s - cum[idx]) / ds[idx]);
      const x = p0[0] + (p1[0] - p0[0]) * f, y = p0[1] + (p1[1] - p0[1]) * f;
      const m = 190, F = m * a + 0.5 * 1.2 * 0.9 * v * v + 0.018 * m * 9.81;
      const Pm = F * v, Pe = Pm > 0 ? Pm / 0.9 : Math.max(Pm * 0.55, -3500);
      const Voc = 60 + 24 * soc;
      let I = Math.max(-40, Math.min(200, Pe / Voc));
      I = Iprev + (I - Iprev) * 0.5; Iprev = I;
      const V = Voc - I * 0.045;
      soc = Math.max(0, soc - I * dt / (3600 * 40));
      mT += (26 + Math.abs(I) * 0.34 - mT) * dt / 45;
      cT += (26 + Math.abs(I) * 0.27 - cT) * dt / 35;
      bT += (27 + Math.abs(I) * 0.10 - bT) * dt / 220;
      const kmh = v * 3.6;
      return {
        live:{
          speed:+kmh.toFixed(1), rpm:Math.round(kmh * 66), soc:+(soc * 100).toFixed(1),
          voltage:+V.toFixed(1), current:+I.toFixed(1), power:+(V * I / 1000).toFixed(2),
          motorTemp:+mT.toFixed(1), controllerTemp:+cT.toFixed(1), batteryTemp:+bT.toFixed(1),
          latitude:toLatLon([x, y])[0], longitude:toLatLon([x, y])[1],
          lap, lapElapsed:+(t - lapStart).toFixed(2), timestamp:Date.now(),
          gps:'RECEIVED', signal:'GOOD', sim:'OK', net:'OK'
        },
        lap:lapDone, best
      };
    }
  };
}

/* ---------- pages: #cv, #ev, home ---------- */
function setPage(p){
  if (!['home', 'cv', 'ev'].includes(p)) p = 'home';
  const changed = p !== page;
  page = p;
  document.body.dataset.page = p;
  for (const a of document.querySelectorAll('.tabs a')) {
    if (a.dataset.page === p) a.setAttribute('aria-current', 'page'); else a.removeAttribute('aria-current');
  }
  document.title = p === 'home' ? 'Zion Racing · Telemetry' : `${SITE.karts[p].name} · Zion Racing`;
  if (p !== 'home') {
    buildGauge(p === 'cv' ? 'speed' : 'rpm');
    $('.hero').setAttribute('aria-label', p === 'cv' ? 'Speed' : 'Speed and motor RPM');
  } else $('#kartName').textContent = 'Telemetry';
  if (document.body.classList.contains('locked')) return;
  if (changed || !source) {
    connect(true);
    if (p !== 'home') requestAnimationFrame(() => { initOsm(); if (osmMap) osmMap.invalidateSize(); drawMap(); drawTrend(); });
  }
}
window.addEventListener('hashchange', () => setPage(location.hash.replace('#', '')));

/* ---------- UI wiring ---------- */
function lock(){
  if (source) { source.stop(); source = null; }
  document.body.classList.add('locked');
  $('#login').hidden = false;
  $('#emailRow').hidden = !!SITE.loginEmail || !LIVE;
  $('#demoHint').hidden = LIVE;
  setTimeout(() => (($('#emailRow').hidden ? $('#loginForm').password : $('#loginForm').email)).focus(), 0);
}
function unlock(){
  $('#login').hidden = true;
  document.body.classList.remove('locked');
  source = null;
  setPage(location.hash.replace('#', ''));
}

$('#loginForm').addEventListener('submit', async (e) => {
  e.preventDefault();
  const f = e.target, btn = $('#loginBtn'), err = $('#loginError');
  const email = SITE.loginEmail || f.email.value.trim(), pw = f.password.value;
  if (!pw) { err.textContent = 'Enter the password.'; return; }
  btn.disabled = true; btn.textContent = 'Signing in…'; err.textContent = '';
  try { await auth.signIn(email, pw); f.password.value = ''; unlock(); }
  catch (x) { err.textContent = x.message; f.password.select(); }
  finally { btn.disabled = false; btn.textContent = 'Sign in'; }
});

$('#signOutBtn').addEventListener('click', () => {
  if (sessionActive()) return;
  auth.signOut(); lock();
});

/* set the start / finish line at the kart's current position */
$('#setStartBtn').addEventListener('click', setStartPoint);

/* start / end session */
const startDlg = $('#startDlg'), endDlg = $('#endDlg');
$('#sessionBtn').addEventListener('click', () => {
  if (sessionActive()) {
    $('#endName').textContent = root.session.name || 'this session';
    $('#endForm').password.value = ''; $('#endError').textContent = '';
    endDlg.showModal();
  } else {
    $('#startForm').name.value = '';
    startDlg.showModal();
  }
});
startDlg.addEventListener('close', async () => {
  if (startDlg.returnValue !== 'start') return;
  const L = latestLive();
  const name = $('#startForm').name.value.trim() || `Session ${new Date().toLocaleDateString()}`;
  try {
    await dbWrite('PUT', 'session', {
      active:true, id:String(Date.now()), name, startedAt:SV,
      firstLap:(isNum(L.lap) ? L.lap : 0) + 1          // count laps from the next start-line crossing
    });
  } catch (x) { log(x.message, 'bad'); }
});
$('#endCancel').addEventListener('click', () => endDlg.close());
$('#endForm').addEventListener('submit', async (e) => {
  e.preventDefault();
  const pw = e.target.password.value, err = $('#endError'), btn = $('#endConfirm');
  if (!pw) { err.textContent = 'Enter the password.'; return; }
  btn.disabled = true; btn.textContent = 'Checking…'; err.textContent = '';
  try {
    await auth.signIn(auth.user.email, pw);          // real check against Firebase, not a local compare
    const S = root.session, { laps, best } = lapsView();
    const lapObj = Object.fromEntries(laps.map(l => ['lap' + l.n, l.t]));
    await dbWrite('PUT', 'sessions/' + S.id, {
      name:S.name || '', startedAt:S.startedAt || null, endedAt:SV,
      laps:lapObj, lapCount:laps.length, bestLap:best
    });
    await dbWrite('PATCH', 'session', { active:false, endedAt:SV });
    await dbWrite('DELETE', 'race');                 // saved in sessions/, so start the next one clean
    endDlg.close();
    log(`Saved ${laps.length} lap${laps.length === 1 ? '' : 's'} from ${S.name || 'the session'}.`, 'good');
  } catch (x) { err.textContent = x.message; e.target.password.select(); }
  finally { btn.disabled = false; btn.textContent = 'End session'; }
});
window.addEventListener('beforeunload', (e) => { if (sessionActive() && cfg.source === 'firebase') { e.preventDefault(); e.returnValue = ''; } });

/* data source */
const dlg = $('#settings'), form = $('#settingsForm');
$('#settingsBtn').addEventListener('click', () => {
  if (sessionActive()) return;
  form.source.value = cfg.source;
  form.querySelector('input[value="firebase"]').disabled = !LIVE;
  dlg.showModal();
});
dlg.addEventListener('close', () => {
  if (dlg.returnValue !== 'save') return;
  cfg = { source:LIVE ? (form.source.value || 'firebase') : 'demo' };
  store.set('zion.cfg', cfg);
  connect(true);
});

document.body.dataset.page = 'home';
const q = new URLSearchParams(location.search);
if (q.has('reset')) { cfg = Object.assign({}, DEFAULTS); store.set('zion.cfg', cfg); }
if (q.has('demo')) cfg.source = 'demo';
window.addEventListener('resize', () => { drawMap(); drawTrend(); if (osmMap) osmMap.invalidateSize(); });
matchMedia('(prefers-color-scheme: dark)').addEventListener?.('change', () => { drawMap(); drawTrend(); themeOsmMarker(); });

/* boot: stay signed in on this device until someone signs out */
(async () => {
  const saved = store.get('zion.auth', null);
  if (saved && saved.refreshToken) {
    try {
      if (LIVE) { auth.user = { email:saved.email, refreshToken:saved.refreshToken }; await auth.refresh(); }
      else auth.set({ email:'demo', idToken:'', refreshToken:'demo', exp:0 });
      unlock(); return;
    } catch { auth.signOut(); }
  }
  lock();
})();
})();
