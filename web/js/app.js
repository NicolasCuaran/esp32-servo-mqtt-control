/* ============================================================
   ESP32 Servo Control - frontend
   ------------------------------------------------------------
   - Pide /api/config para saber cuantos servos pintar y en
     que canales del PCA9685 estan.
   - Habla con el backend FastAPI (REST) y escucha el WS /ws.
   ============================================================ */

let NUM_SERVOS   = 1;
let SERVO_PINS   = [0];
let localAngles  = [];
let localPresets = [];
let isConnected  = false;

// ---------- Helpers ----------
const $ = (sel, root = document) => root.querySelector(sel);
const el = (tag, props = {}, children = []) => {
  const n = Object.assign(document.createElement(tag), props);
  for (const c of children) n.append(c);
  return n;
};

function log(msg, cls = "info") {
  const pre = $("#log");
  if (!pre) return;
  const time = new Date().toLocaleTimeString();
  const line = document.createElement("div");
  line.innerHTML = `<span class="info">[${time}]</span> <span class="${cls}">${msg}</span>`;
  pre.prepend(line);
  while (pre.children.length > 200) pre.removeChild(pre.lastChild);
}

async function api(path, body) {
  try {
    const res = await fetch(path, {
      method: body ? "POST" : "GET",
      headers: { "Content-Type": "application/json" },
      body: body ? JSON.stringify(body) : undefined,
    });
    if (!res.ok) {
      const t = await res.text();
      throw new Error(`${res.status} ${t}`);
    }
    return await res.json();
  } catch (e) {
    log(`Error API ${path}: ${e.message}`, "err");
    throw e;
  }
}

function setConnection(status) {
  const pill = $("#connPill"), text = $("#connText");
  pill.classList.remove("online", "offline", "connecting");
  pill.classList.add(status);
  text.textContent =
    status === "online"     ? "ESP32 en linea" :
    status === "offline"    ? "ESP32 desconectado" :
                              "Conectando...";
  isConnected = status === "online";
}

function updateMeta(snapshot) {
  const m = $("#metaInfo");
  if (!m) return;
  const rssi = snapshot.rssi ?? "-";
  const up   = snapshot.uptime_s ?? 0;
  m.textContent = `RSSI ${rssi} dBm - uptime ${up}s`;
  $("#deviceId").textContent = snapshot.device_id || "-";
}

// ---------- Gauge SVG ----------
function buildGauge() {
  const NS = "http://www.w3.org/2000/svg";
  const svg = document.createElementNS(NS, "svg");
  svg.setAttribute("viewBox", "0 0 200 100");
  svg.innerHTML = `
    <defs>
      <linearGradient id="gaugeGrad" x1="0" x2="1" y1="0" y2="0">
        <stop offset="0%"   stop-color="#7c9cff"/>
        <stop offset="100%" stop-color="#5fe1c1"/>
      </linearGradient>
    </defs>
    <path class="bg" d="M10 90 A 90 90 0 0 1 190 90"
          fill="none" stroke-width="14" stroke-linecap="round"/>
    <path class="fg" d="M10 90 A 90 90 0 0 1 190 90"
          fill="none" stroke-width="14" stroke-linecap="round"
          pathLength="100" stroke-dasharray="100" stroke-dashoffset="50"/>
  `;
  return svg;
}

// ---------- Servo card ----------
function buildServoCard(index) {
  const card = el("div", { className: "servo" });
  card.dataset.servo = index;

  card.innerHTML = `
    <div class="servo-head">
      <div class="servo-title">Servo ${index + 1}</div>
      <div class="servo-pin">PCA9685 - canal ${SERVO_PINS[index]}</div>
    </div>

    <div class="gauge">
      <div class="needle"></div>
      <div class="center"></div>
      <div class="angle-readout"><span class="ang">90</span><small>grados</small></div>
    </div>

    <div class="slider-row">
      <input type="range" min="0" max="180" value="90" class="slider"/>
      <input type="number" min="0" max="180" value="90" class="num"/>
    </div>

    <div class="preset-row">
      <div class="preset-block">
        <div class="label">Preset <b>1</b> - <span class="p0val">90 grados</span></div>
        <div class="buttons">
          <button class="btn btn-save"  data-action="save" data-slot="0">Guardar</button>
          <button class="btn"           data-action="load" data-slot="0">Ir</button>
        </div>
      </div>
      <div class="preset-block">
        <div class="label">Preset <b>2</b> - <span class="p1val">90 grados</span></div>
        <div class="buttons">
          <button class="btn btn-save"  data-action="save" data-slot="1">Guardar</button>
          <button class="btn"           data-action="load" data-slot="1">Ir</button>
        </div>
      </div>
    </div>
  `;

  card.querySelector(".gauge").prepend(buildGauge());

  const slider = card.querySelector(".slider");
  const num    = card.querySelector(".num");

  let throttle = null;
  const sendMove = (angle) => {
    clearTimeout(throttle);
    throttle = setTimeout(() => {
      api("/api/move", { servo: index, angle }).catch(() => {});
    }, 35);
  };

  const onChange = (angle) => {
    angle = Math.max(0, Math.min(180, Number(angle) | 0));
    slider.value = angle;
    num.value    = angle;
    localAngles[index] = angle;
    paint(index);
    sendMove(angle);
  };

  slider.addEventListener("input", e => onChange(e.target.value));
  num.addEventListener("change",  e => onChange(e.target.value));

  card.querySelectorAll("button[data-action]").forEach(btn => {
    btn.addEventListener("click", () => {
      const slot   = Number(btn.dataset.slot);
      const action = btn.dataset.action;
      if (action === "save") {
        api("/api/preset/save", { servo: index, slot, angle: localAngles[index] })
          .then(() => log(`Servo ${index+1}: preset ${slot+1} = ${localAngles[index]} grados`, "ok"));
      } else {
        api("/api/preset/load", { servo: index, slot })
          .then(() => log(`Servo ${index+1}: cargado preset ${slot+1}`, "ok"));
      }
    });
  });

  return card;
}

// ---------- Pintar UI desde estado ----------
function paint(servoIdx) {
  const card = document.querySelector(`.servo[data-servo="${servoIdx}"]`);
  if (!card) return;
  const angle = localAngles[servoIdx] ?? 90;

  card.querySelector(".ang").textContent = angle;
  card.querySelector(".slider").value = angle;
  card.querySelector(".num").value    = angle;

  const fg = card.querySelector("svg .fg");
  if (fg) fg.setAttribute("stroke-dashoffset", String(100 - (angle / 180) * 100));

  const needleDeg = (angle / 180) * 180 - 90;
  card.querySelector(".needle").style.transform =
    `translateX(-50%) rotate(${needleDeg}deg)`;

  const p = localPresets[servoIdx] || [90, 90];
  card.querySelector(".p0val").textContent = p[0] + " grados";
  card.querySelector(".p1val").textContent = p[1] + " grados";
}

function paintAll() {
  for (let i = 0; i < NUM_SERVOS; i++) paint(i);
}

// ---------- Render dinamico segun config ----------
function renderServos() {
  const grid = $("#servoGrid");
  grid.innerHTML = "";
  // Asegura arrays del tamano correcto
  localAngles  = Array.from({ length: NUM_SERVOS }, (_, i) => localAngles[i]  ?? 90);
  localPresets = Array.from({ length: NUM_SERVOS }, (_, i) => localPresets[i] ?? [90, 90]);
  for (let i = 0; i < NUM_SERVOS; i++) grid.append(buildServoCard(i));
  paintAll();
}

// ---------- WebSocket ----------
function startWS() {
  const proto = location.protocol === "https:" ? "wss" : "ws";
  const ws = new WebSocket(`${proto}://${location.host}/ws`);
  ws.onopen = () => log("WS conectado", "ok");
  ws.onclose = () => {
    log("WS desconectado, reintentando...", "err");
    setConnection("connecting");
    setTimeout(startWS, 1500);
  };
  ws.onmessage = (ev) => {
    try {
      const data = JSON.parse(ev.data);
      setConnection(data.online ? "online" : "offline");

      // Si el ESP32 reporta otra cantidad de servos, re-renderizar
      if (typeof data.num_servos === "number" && data.num_servos !== NUM_SERVOS) {
        NUM_SERVOS = data.num_servos;
        if (Array.isArray(data.channels) && data.channels.length >= NUM_SERVOS)
          SERVO_PINS = data.channels.slice(0, NUM_SERVOS);
        renderServos();
      }

      if (Array.isArray(data.angles))
        for (let i = 0; i < NUM_SERVOS; i++)
          if (typeof data.angles[i] === "number") localAngles[i] = data.angles[i];
      if (Array.isArray(data.presets))
        for (let i = 0; i < NUM_SERVOS; i++)
          if (Array.isArray(data.presets[i])) localPresets[i] = data.presets[i];
      updateMeta(data);
      paintAll();
    } catch (e) {
      log("WS payload invalido: " + e.message, "err");
    }
  };
  setInterval(() => { try { ws.readyState === 1 && ws.send("ping"); } catch {} }, 25000);
}

// ---------- Acciones globales ----------
function wireGlobalControls() {
  document.querySelectorAll("[data-load-all]").forEach(btn => {
    btn.addEventListener("click", () => {
      const slot = Number(btn.dataset.loadAll);
      api("/api/preset/load_all", { slot })
        .then(() => log(`Cargado preset ${slot+1} en todos los servos`, "ok"));
    });
  });
  $("#centerAllBtn").addEventListener("click", () => {
    const angles = Array(NUM_SERVOS).fill(90);
    api("/api/move_all", { angles })
      .then(() => log("Centrado todos los servos a 90 grados", "ok"));
  });
  $("#refreshBtn").addEventListener("click", () => {
    api("/api/request_state").then(() => log("Estado solicitado", "info"));
  });
  $("#clearLogBtn").addEventListener("click", () => { $("#log").innerHTML = ""; });
}

// ---------- Bootstrap ----------
async function init() {
  wireGlobalControls();
  setConnection("connecting");

  try {
    const cfg = await api("/api/config");
    NUM_SERVOS = cfg.num_servos || 1;
    SERVO_PINS = (cfg.channels && cfg.channels.length >= NUM_SERVOS)
      ? cfg.channels.slice(0, NUM_SERVOS)
      : Array.from({ length: NUM_SERVOS }, (_, i) => i);
  } catch {
    NUM_SERVOS = 1;
    SERVO_PINS = [0];
  }
  renderServos();

  try {
    const s = await api("/api/status");
    if (Array.isArray(s.angles))  s.angles.forEach((a,i)  => localAngles[i]  = a);
    if (Array.isArray(s.presets)) s.presets.forEach((p,i) => localPresets[i] = p);
    setConnection(s.online ? "online" : "offline");
    updateMeta(s);
    paintAll();
  } catch { /* ignorar */ }

  startWS();
}

document.addEventListener("DOMContentLoaded", init);
