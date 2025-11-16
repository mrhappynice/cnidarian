// src/template.c
// Base template for a point-cloud animation that plugs into main.js.
// Copy this file, rename it (e.g. swirls.c / nebula.c), and customize lb_step().

#include <emscripten/emscripten.h>
#include <math.h>
#include <stdlib.h>
#include <stdint.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// ===== Global state (shared across frames) =====
static int   g_width       = 0;
static int   g_height      = 0;
static float g_baseDensity = 0.005f;   // points per pixel (controls N)
static float g_speed       = 1.0f;     // time multiplier
static float g_zoom        = 1.0f;     // optional, use if you want
static int   g_zoomAuto    = 0;        // optional, use if you want
static float g_t           = 0.0f;     // accumulated time

static const int G_MIN_POINTS = 6000;
static const int G_MAX_POINTS = 120000;

// Number of active points
static int    g_N         = 0;
// Positions output buffer: [x0,y0, x1,y1, ...] length = 2 * g_N
static float* g_positions = NULL;


// ===== Lifetime helpers =====

static void free_field(void) {
  free(g_positions);
  g_positions = NULL;
  g_N = 0;
}

// Recalculate how many points we want, and (re)allocate the positions buffer.
// Call this whenever width/height or density changes.
static void rebuild_field(void) {
  if (g_width <= 0 || g_height <= 0) return;

  double target_d = (double)g_baseDensity * (double)g_width * (double)g_height;
  int target = (int)target_d;
  if (target < G_MIN_POINTS)  target = G_MIN_POINTS;
  if (target > G_MAX_POINTS)  target = G_MAX_POINTS;

  if (target == g_N && g_positions) return;  // already correct size

  free_field();

  g_N = target;
  g_positions = (float*)malloc(sizeof(float) * g_N * 2);

  if (!g_positions) {
    free_field();
    return;
  }
}


// ===== Public API used from JS (must match main.js) =====

EMSCRIPTEN_KEEPALIVE
void lb_set_canvas(int width, int height) {
  if (width <= 0 || height <= 0) return;
  g_width  = width;
  g_height = height;
  rebuild_field();
}

EMSCRIPTEN_KEEPALIVE
void lb_set_density(float density) {
  if (density < 0.0005f) density = 0.0005f;
  if (density > 0.060f)  density = 0.060f;
  g_baseDensity = density;
  rebuild_field();
}

EMSCRIPTEN_KEEPALIVE
void lb_set_speed(float speed) {
  if (speed < 0.05f) speed = 0.05f;
  if (speed > 5.0f)  speed = 5.0f;
  g_speed = speed;
}

EMSCRIPTEN_KEEPALIVE
void lb_set_zoom(float zoom) {
  if (zoom < 0.5f) zoom = 0.5f;
  if (zoom > 4.0f) zoom = 4.0f;
  g_zoom = zoom;
}

// You can use this to drive a breathing zoom, or ignore it.
EMSCRIPTEN_KEEPALIVE
void lb_set_zoom_auto(int on) {
  g_zoomAuto = on ? 1 : 0;
}

EMSCRIPTEN_KEEPALIVE
void lb_reset(void) {
  g_t = 0.0f;
  // If you introduce per-point state later, reset it here.
}

EMSCRIPTEN_KEEPALIVE
int lb_get_point_count(void) {
  return g_N;
}

EMSCRIPTEN_KEEPALIVE
float lb_get_x(int i) {
  if (!g_positions || i < 0 || i >= g_N) return -1.0f;
  return g_positions[i * 2 + 0];
}

EMSCRIPTEN_KEEPALIVE
float lb_get_y(int i) {
  if (!g_positions || i < 0 || i >= g_N) return -1.0f;
  return g_positions[i * 2 + 1];
}


// ===== Core animation step (put your effect here) =====
//
// dt is the elapsed time in seconds since the last frame. main.js calls this
// each frame, then samples all points via lb_get_x / lb_get_y and draws them.
//
// IMPORTANT:
// - Fill g_positions[2*i + 0] and [2*i + 1] with screen coordinates
//   in [0, g_width] x [0, g_height] for each i in [0, g_N).
// - Use g_t, g_speed, g_zoom, etc. as needed.

EMSCRIPTEN_KEEPALIVE
void lb_step(float dt) {
  if (!g_positions || g_N <= 0) return;
  if (g_width <= 0 || g_height <= 0) return;

  if (dt < 0.0001f) dt = 0.0001f;
  if (dt > 0.1f)    dt = 0.1f;

  // Advance time with speed multiplier
  g_t += dt * g_speed;

  // Precompute a few handy values
  const float cx = (float)g_width  * 0.5f;
  const float cy = (float)g_height * 0.5f;
  const float minDim = (float)(g_width < g_height ? g_width : g_height);
  const float baseRadius = minDim * 0.35f * g_zoom;

  for (int i = 0; i < g_N; i++) {
    // u is a normalized index [0..1]
    const float u = (float)i / (float)g_N;

    // === Example effect: spiral-ish pattern ===
    // Feel free to completely replace this math.
    float angle = u * 16.0f + g_t * 0.8f;
    float r     = baseRadius * (0.3f + 0.7f * u);

    float wobble = 0.1f * sinf(6.0f * u + g_t * 1.5f);
    r *= (1.0f + wobble);

    float x = cx + cosf(angle) * r;
    float y = cy + sinf(angle * 1.3f) * r * 0.6f;

    // Write final screen-space coordinates
    g_positions[i * 2 + 0] = x;
    g_positions[i * 2 + 1] = y;
  }
}
