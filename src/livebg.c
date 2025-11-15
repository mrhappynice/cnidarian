// src/livebg.c
#include <emscripten/emscripten.h>
#include <math.h>
#include <stdlib.h>
#include <stdint.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// ===== Global state =====
static int   g_width        = 0;
static int   g_height       = 0;
static float g_baseDensity  = 0.005f;   // points per screen pixel
static float g_speed        = 0.05f;
static float g_zoom         = 1.0f;
static int   g_zoomAuto     = 0;        // 0/1
static float g_zoomPhase    = 0.0f;
static float g_t            = 0.0f;

static const int   G_MIN_POINTS = 6000;
static const int   G_MAX_POINTS = 120000;

static int    g_N          = 0;
static float* g_xVals      = NULL;
static float* g_yVals      = NULL;
static float* g_positions  = NULL;  // [x0, y0, x1, y1, ...] length = 2*N

static void free_field(void) {
  free(g_xVals);      g_xVals = NULL;
  free(g_yVals);      g_yVals = NULL;
  free(g_positions);  g_positions = NULL;
  g_N = 0;
}

static void rebuild_field(void) {
  if (g_width <= 0 || g_height <= 0) return;

  // target number of points = baseDensity * pixels (clamped)
  double target_d = (double)g_baseDensity * (double)g_width * (double)g_height;
  int target = (int)target_d;
  if (target < G_MIN_POINTS)  target = G_MIN_POINTS;
  if (target > G_MAX_POINTS)  target = G_MAX_POINTS;

  if (target == g_N && g_xVals && g_yVals && g_positions) return;

  free_field();

  g_N = target;
  g_xVals     = (float*)malloc(sizeof(float) * g_N);
  g_yVals     = (float*)malloc(sizeof(float) * g_N);
  g_positions = (float*)malloc(sizeof(float) * g_N * 2);

  if (!g_xVals || !g_yVals || !g_positions) {
    free_field();
    return;
  }

  // Same pattern as the JS:
  //   const ii = i + 1;
  //   xVals[i] = ii % 200;
  //   yVals[i] = ii / 43;
  for (int i = 0; i < g_N; i++) {
    int ii = i + 1;
    g_xVals[i] = (float)(ii % 200);
    g_yVals[i] = (float)(ii / 43);
  }
}

// ===== Public API (called from JS) =====

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

EMSCRIPTEN_KEEPALIVE
void lb_set_zoom_auto(int on) {
  g_zoomAuto = on ? 1 : 0;
}

EMSCRIPTEN_KEEPALIVE
void lb_reset(void) {
  g_t         = 0.0f;
  g_zoomPhase = 0.0f;
}

EMSCRIPTEN_KEEPALIVE
int lb_get_point_count(void) {
  return g_N;
}

EMSCRIPTEN_KEEPALIVE
float* lb_get_positions(void) {
  return g_positions;
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

// Core step: update time & positions
EMSCRIPTEN_KEEPALIVE
void lb_step(float dt) {
  if (!g_xVals || !g_yVals || !g_positions || g_N <= 0) return;
  if (g_width <= 0 || g_height <= 0) return;

  if (dt < 0.0001f) dt = 0.0001f;

  // Same time scaling as JS:
  //   const targetRate = Math.PI / 20 * 60;
  const float targetRate = (float)M_PI / 20.0f * 60.0f;
  g_t += targetRate * dt * g_speed;

  // Zoom (with optional auto pulse)
  float zoomNow = g_zoom;
  if (g_zoomAuto) {
    g_zoomPhase += dt;
    const float amp = 0.06f; // ±6%
    const float hz  = 0.08f; // cycles/sec
    zoomNow = g_zoom * (1.0f + amp * sinf(2.0f * (float)M_PI * hz * g_zoomPhase));
  }

  // First pass: bounding box in function-space
  float minX =  1e9f, maxX = -1e9f;
  float minY =  1e9f, maxY = -1e9f;

  for (int i = 0; i < g_N; i++) {
    float xv = g_xVals[i];
    float yv = g_yVals[i];

    float k = 5.0f * cosf(xv / 14.0f) * cosf(yv / 30.0f);
    float e = yv / 8.0f - 13.0f;
    float d = (k * k + e * e) / 59.0f + 4.0f;

    float q = 60.0f
      - 3.0f * sinf(atan2f(k, e) * e)
      + k * (3.0f + (4.0f / d) * sinf(d * d - g_t * 2.0f));

    float c = d / 2.0f + e / 99.0f - g_t / 18.0f;

    float mx = q * sinf(c);
    float my = (q + d * 9.0f) * cosf(c);

    if (mx < minX) minX = mx;
    if (mx > maxX) maxX = mx;
    if (my < minY) minY = my;
    if (my > maxY) maxY = my;
  }

  float bw = maxX - minX;
  float bh = maxY - minY;
  if (bw < 1e-4f) bw = 1.0f;
  if (bh < 1e-4f) bh = 1.0f;

  const float margin    = 0.92f;
  float baseScale = (float)g_width * margin / bw;
  float tmp       = (float)g_height * margin / bh;
  if (tmp < baseScale) baseScale = tmp;

  float scale = baseScale * zoomNow;

  float cx = (float)g_width  * 0.5f;
  float cy = (float)g_height * 0.5f;
  float midX = (minX + maxX) * 0.5f;
  float midY = (minY + maxY) * 0.5f;
  float offX = cx - (midX * scale);
  float offY = cy - (midY * scale);

  // Second pass: project to screen coordinates
  for (int i = 0; i < g_N; i++) {
    float xv = g_xVals[i];
    float yv = g_yVals[i];

    float k = 5.0f * cosf(xv / 14.0f) * cosf(yv / 30.0f);
    float e = yv / 8.0f - 13.0f;
    float d = (k * k + e * e) / 59.0f + 4.0f;

    float q = 60.0f
      - 3.0f * sinf(atan2f(k, e) * e)
      + k * (3.0f + (4.0f / d) * sinf(d * d - g_t * 2.0f));

    float c = d / 2.0f + e / 99.0f - g_t / 18.0f;

    float mx = q * sinf(c);
    float my = (q + d * 9.0f) * cosf(c);

    float x = mx * scale + offX;
    float y = my * scale + offY;

    g_positions[i * 2 + 0] = x;
    g_positions[i * 2 + 1] = y;
  }
}
