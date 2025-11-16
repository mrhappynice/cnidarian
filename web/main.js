// web/main.js
import createLiveBg from "../pkg/livebg.js";
import createFire   from "../pkg/fire.js";

// ===== Effect registry & wrappers =====

const effects = {};
let activeEffect = "livebg";

async function loadEffect(name) {
  if (effects[name]) return effects[name];

  let createFn;
  if (name === "livebg")      createFn = createLiveBg;
  else if (name === "fire")   createFn = createFire;
  else throw new Error("Unknown effect: " + name);

  const mod = await createFn();
  const c = {
    set_canvas:      mod.cwrap("lb_set_canvas", null, ["number", "number"]),
    set_density:     mod.cwrap("lb_set_density", null, ["number"]),
    set_speed:       mod.cwrap("lb_set_speed", null, ["number"]),
    set_zoom:        mod.cwrap("lb_set_zoom", null, ["number"]),
    set_zoom_auto:   mod.cwrap("lb_set_zoom_auto", null, ["number"]),
    step:            mod.cwrap("lb_step", null, ["number"]),
    get_point_count: mod.cwrap("lb_get_point_count", "number", []),
    get_x:           mod.cwrap("lb_get_x", "number", ["number"]),
    get_y:           mod.cwrap("lb_get_y", "number", ["number"]),
    reset:           mod.cwrap("lb_reset", null, []),
  };

  effects[name] = { mod, c };
  return effects[name];
}

function cur() {
  return effects[activeEffect].c;
}

// C exports (wrappers that always use the active effect)
function lb_set_canvas(width, height)    { cur().set_canvas(width, height); }
function lb_set_density(density)         { cur().set_density(density); }
function lb_set_speed(speed)             { cur().set_speed(speed); }
function lb_set_zoom(zoom)               { cur().set_zoom(zoom); }
function lb_set_zoom_auto(on)            { cur().set_zoom_auto(on); }
function lb_step(dt)                     { cur().step(dt); }
function lb_get_point_count()            { return cur().get_point_count(); }
function lb_get_x(i)                     { return cur().get_x(i); }
function lb_get_y(i)                     { return cur().get_y(i); }
function lb_reset()                      { cur().reset(); }


// ===== Canvas & DPI =====
const canvas = document.getElementById("c");
const ctx = canvas.getContext("2d", { alpha: true });

let W = 0, H = 0, DPR = 1;

function fitCanvas() {
  DPR = Math.max(1, Math.min(3, window.devicePixelRatio || 1));
  W = Math.floor(window.innerWidth);
  H = Math.floor(window.innerHeight);
  canvas.width  = Math.floor(W * DPR);
  canvas.height = Math.floor(H * DPR);
  ctx.setTransform(DPR, 0, 0, DPR, 0, 0);

  lb_set_canvas(W, H);
  updatePointStat();
}
window.addEventListener("resize", fitCanvas);

// ===== UI elements =====
const fpsEl     = document.getElementById("fps");
const ptsEl     = document.getElementById("pts");
const spdEl     = document.getElementById("spd");
const zoomStat  = document.getElementById("zoomStat");

const speedRange = document.getElementById("speed");
const speedVal   = document.getElementById("speedVal");
const densRange  = document.getElementById("density");
const densVal    = document.getElementById("densVal");
const zoomRange  = document.getElementById("zoom");
const zoomVal    = document.getElementById("zoomVal");
const zoomAutoCb = document.getElementById("zoomAuto");

const btnToggle  = document.getElementById("toggle");
const btnReset   = document.getElementById("reset");
const effectSelect = document.getElementById("effectSelect"); // optional selector

// ===== State mirrored in C =====
let speed       = 0.05;
let baseDensity = 0.005;
let zoom        = 1.0;
let zoomAuto    = false;
let running     = true;

let last  = performance.now();
let fpsEMA = 60;

// ===== Helpers to sync UI <-> C =====
function updatePointStat() {
  const n = lb_get_point_count();
  ptsEl.textContent = "points: " + n.toLocaleString();
}

function applySpeed() {
  lb_set_speed(speed);
  spdEl.textContent    = "speed: " + speed.toFixed(2);
  speedVal.textContent = speed.toFixed(2);
}

function applyDensity() {
  lb_set_density(baseDensity);
  densVal.textContent = baseDensity.toFixed(3);
  updatePointStat();
}

function applyZoom() {
  lb_set_zoom(zoom);
  zoomVal.textContent  = zoom.toFixed(2) + "×";
  zoomStat.textContent = "zoom: " + zoom.toFixed(2) + "×";
}

function applyZoomAuto() {
  lb_set_zoom_auto(zoomAuto ? 1 : 0);
}

// ===== UI wiring =====
speedRange.addEventListener("input", () => {
  speed = parseFloat(speedRange.value);
  applySpeed();
});

densRange.addEventListener("input", () => {
  baseDensity = parseFloat(densRange.value);
  applyDensity();
});

zoomRange.addEventListener("input", () => {
  zoom = parseFloat(zoomRange.value);
  applyZoom();
});

zoomAutoCb.addEventListener("change", () => {
  zoomAuto = zoomAutoCb.checked;
  applyZoomAuto();
});

btnToggle.addEventListener("click", () => {
  running = !running;
  btnToggle.textContent = running ? "Pause" : "Resume";
  if (running) {
    last = performance.now();
    requestAnimationFrame(loop);
  }
});

btnReset.addEventListener("click", () => {
  lb_reset();
});

// Effect selector (if present in HTML)
if (effectSelect) {
  effectSelect.addEventListener("change", async () => {
    const next = effectSelect.value;
    if (next === activeEffect) return;

    running = false;

    await loadEffect(next);
    activeEffect = next;

    // Re-apply canvas size & current settings to the new effect
    lb_set_canvas(W, H);
    applySpeed();
    applyDensity();
    applyZoom();
    applyZoomAuto();
    lb_reset();

    running = true;
    last = performance.now();
    requestAnimationFrame(loop);
  });
}

// Wheel: speed; Shift + Wheel: zoom
canvas.addEventListener(
  "wheel",
  (ev) => {
    if (ev.shiftKey) {
      ev.preventDefault();
      zoom *= Math.pow(1.1, -ev.deltaY / 100);
      zoom = Math.min(4, Math.max(0.5, zoom));
      zoomRange.value = zoom.toFixed(2);
      applyZoom();
      return;
    }

    ev.preventDefault();
    speed *= Math.pow(1.1, -ev.deltaY / 100);
    speed = Math.max(0.05, Math.min(5, speed));
    speedRange.value = speed.toFixed(2);
    applySpeed();
  },
  { passive: false }
);

// Keyboard +/- for zoom
window.addEventListener("keydown", (e) => {
  if (e.key === "+" || e.key === "=") {
    zoom = Math.min(4, zoom * 1.1);
  } else if (e.key === "-" || e.key === "_") {
    zoom = Math.max(0.5, zoom / 1.1);
  } else {
    return;
  }
  zoomRange.value = zoom.toFixed(2);
  applyZoom();
});


// ===== Initial setup =====
await loadEffect("livebg"); // make sure default effect is ready

fitCanvas();
applySpeed();
applyDensity();
applyZoom();
applyZoomAuto();


// ===== Animation loop =====
let frameCount = 0;

function loop(now) {
  if (!running) return;

  const dt = Math.max(0.0001, (now - last) / 1000);
  last = now;

  // FPS (EMA)
  const fps = 1 / dt;
  fpsEMA = fpsEMA * 0.9 + fps * 0.1;
  fpsEl.textContent = "fps: " + fpsEMA.toFixed(0);

  // Step simulation in C
  lb_step(dt);

  const count = lb_get_point_count() | 0;

  // Canvas setup
  ctx.setTransform(DPR, 0, 0, DPR, 0, 0);
  ctx.globalAlpha = 1;
  ctx.globalCompositeOperation = "source-over";

  // Background
  ctx.clearRect(0, 0, W, H);
  ctx.fillStyle = "#0a0a0a";
  ctx.fillRect(0, 0, W, H);

  if (count <= 0) {
    zoomStat.textContent = `zoom: ${zoom.toFixed(2)}× | sample: (none)`;
    requestAnimationFrame(loop);
    return;
  }

  // Update point count occasionally
  if (frameCount++ % 30 === 0) {
    ptsEl.textContent = "points: " + count.toLocaleString();
  }

  // Sample first point for debugging
  const sx = lb_get_x(0);
  const sy = lb_get_y(0);

  if (Number.isFinite(sx) && Number.isFinite(sy)) {
    zoomStat.textContent =
      `zoom: ${zoom.toFixed(2)}× | sample: (${sx.toFixed(1)}, ${sy.toFixed(1)}) [${activeEffect}]`;
  } else {
    zoomStat.textContent =
      `zoom: ${zoom.toFixed(2)}× | sample: NaN [${activeEffect}]`;
  }

  // Draw all points
  ctx.fillStyle = "rgba(255,255,255,0.8)";
  const size = 3;

  for (let i = 0; i < count; i++) {
    const x = lb_get_x(i);
    const y = lb_get_y(i);

    if (!Number.isFinite(x) || !Number.isFinite(y)) continue;

    if (x >= 0 && x < W && y >= 0 && y < H) {
      ctx.fillRect(x, y, size, size);
    }
  }

  // Tiny overlay so you can see numbers directly on the canvas
  ctx.fillStyle = "#ffffff";
  ctx.font = "12px ui-sans-serif, system-ui, -apple-system, BlinkMacSystemFont";
  ctx.fillText(`N=${count}`, 10, 20);
  ctx.fillText(`sample=(${sx.toFixed(1)}, ${sy.toFixed(1)})`, 10, 36);

  requestAnimationFrame(loop);
}

requestAnimationFrame(loop);

