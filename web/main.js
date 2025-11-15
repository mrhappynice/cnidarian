// web/main.js
import createLiveBg from "../pkg/livebg.js";

const LB = await createLiveBg();

// C exports
const lb_set_canvas      = LB.cwrap("lb_set_canvas", null, ["number", "number"]);
const lb_set_density     = LB.cwrap("lb_set_density", null, ["number"]);
const lb_set_speed       = LB.cwrap("lb_set_speed", null, ["number"]);
const lb_set_zoom        = LB.cwrap("lb_set_zoom", null, ["number"]);
const lb_set_zoom_auto   = LB.cwrap("lb_set_zoom_auto", null, ["number"]);
const lb_step            = LB.cwrap("lb_step", null, ["number"]);
const lb_get_point_count = LB.cwrap("lb_get_point_count", "number", []);
const lb_get_positions   = LB.cwrap("lb_get_positions", "number", []);
const lb_reset           = LB.cwrap("lb_reset", null, []);
const lb_get_x           = LB.cwrap("lb_get_x", "number", ["number"]);
const lb_get_y           = LB.cwrap("lb_get_y", "number", ["number"]);


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

// ===== UI elements (from livebg.html) =====
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

// ===== UI wiring (same behavior as original JS) =====
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

// Wheel: speed; Shift + Wheel: zoom  (same UX)
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
      `zoom: ${zoom.toFixed(2)}× | sample: (${sx.toFixed(1)}, ${sy.toFixed(1)})`;
  } else {
    zoomStat.textContent =
      `zoom: ${zoom.toFixed(2)}× | sample: NaN`;
  }

  // Draw all points by calling into C per point
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








