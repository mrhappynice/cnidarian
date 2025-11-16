// src/fire.c
// Drop-in replacement for livebg.c, using a simple "fire" particle system.
// Exports the same lb_* API so your existing main.js can talk to it unchanged.

#include <emscripten/emscripten.h>
#include <math.h>
#include <stdlib.h>
#include <stdint.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// ===== Global state =====
static int   g_width       = 0;
static int   g_height      = 0;
static float g_baseDensity = 0.005f;   // points per screen pixel
static float g_speed       = 0.8f;     // fire rise speed multiplier
static float g_zoom        = 1.0f;
static int   g_zoomAuto    = 0;        // unused, but kept for API compat
static float g_zoomPhase   = 0.0f;     // unused
static float g_t           = 0.0f;

static const int   G_MIN_POINTS = 6000;
static const int   G_MAX_POINTS = 120000;

// Particle representation: a single glowing ember rising upward
typedef struct {
  float x;
  float y;
  float vy;
  float life;     // 0..1, decremented over time
  float maxLife;  // starting life
} Particle;

static int       g_N          = 0;
static Particle* g_particles  = NULL;
static float*    g_positions  = NULL;  // [x0, y0, x1, y1, ...], length = 2 * g_N

// ----- Utilities -----
static float rand01(void) {
  return (float)rand() / (float)RAND_MAX;
}

// Respawn a single particle near the bottom of the canvas
static void respawn_particle(int i) {
  if (!g_particles || g_width <= 0 || g_height <= 0) return;

  Particle* p = &g_particles[i];
  // Spawn along the bottom with slight horizontal jitter
  p->x = rand01() * (float)g_width;
  p->y = (float)g_height - rand01() * 10.0f;

  // Vertical speed: pixels per second (negative = upwards)
  // Slight variation per particle so the fire looks more organic
  float baseVy = - (50.0f + 150.0f * rand01()); // -50 .. -200
  p->vy = baseVy * g_speed;

  // Lifetime in seconds, slightly randomized
  p->maxLife = 0.6f + 0.6f * rand01(); // 0.6 .. 1.2
  p->life    = p->maxLife;
}

static void free_field(void) {
  free(g_particles);  g_particles = NULL;
  free(g_positions);  g_positions = NULL;
  g_N = 0;
}

// Allocate/rebuild the particle field based on current width/height + density
static void rebuild_field(void) {
  if (g_width <= 0 || g_height <= 0) return;

  double target_d = (double)g_baseDensity * (double)g_width * (double)g_height;
  int target = (int)target_d;
  if (target < G_MIN_POINTS)  target = G_MIN_POINTS;
  if (target > G_MAX_POINTS)  target = G_MAX_POINTS;

  if (target == g_N && g_particles && g_positions) return;

  free_field();

  g_N = target;
  g_particles = (Particle*)malloc(sizeof(Particle) * g_N);
  g_positions = (float*)malloc(sizeof(float) * g_N * 2);

  if (!g_particles || !g_positions) {
    free_field();
    return;
  }

  // Initialize particles
  for (int i = 0; i < g_N; i++) {
    respawn_particle(i);
    g_positions[i * 2 + 0] = g_particles[i].x;
    g_positions[i * 2 + 1] = g_particles[i].y;
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
  // Keep roughly same UI range as original
  if (speed < 0.05f) speed = 0.05f;
  if (speed > 5.0f)  speed = 5.0f;
  g_speed = speed;
}

EMSCRIPTEN_KEEPALIVE
void lb_set_zoom(float zoom) {
  // In this fire version, zoom is mostly a no-op.
  if (zoom < 0.5f) zoom = 0.5f;
  if (zoom > 4.0f) zoom = 4.0f;
  g_zoom = zoom;
}

EMSCRIPTEN_KEEPALIVE
void lb_set_zoom_auto(int on) {
  // No-op for now, but kept for compatibility
  g_zoomAuto = on ? 1 : 0;
}

EMSCRIPTEN_KEEPALIVE
void lb_reset(void) {
  g_t         = 0.0f;
  g_zoomPhase = 0.0f;

  if (!g_particles) return;
  for (int i = 0; i < g_N; i++) {
    respawn_particle(i);
  }
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

// Core step: update time & particle positions
EMSCRIPTEN_KEEPALIVE
void lb_step(float dt) {
  if (!g_particles || !g_positions || g_N <= 0) return;
  if (g_width <= 0 || g_height <= 0) return;

  if (dt < 0.0001f) dt = 0.0001f;
  if (dt > 0.1f)    dt = 0.1f;     // clamp big frame jumps

  g_t += dt;

  // Update each particle
  for (int i = 0; i < g_N; i++) {
    Particle* p = &g_particles[i];

    // Integrate position
    p->y += p->vy * dt;

    // Simple upward acceleration (like rising hot air)
    float accel = -40.0f * g_speed;
    p->vy += accel * dt;

    // Age the particle
    p->life -= dt;

    // If it went off-screen or "died", respawn it near the bottom
    if (p->y < -20.0f || p->life <= 0.0f) {
      respawn_particle(i);
    }

    // Apply a very gentle horizontal jitter to make the column less uniform
    float jitter = (rand01() - 0.5f) * 10.0f * dt;
    p->x += jitter;
    if (p->x < -10.0f)              p->x = -10.0f;
    if (p->x > (float)g_width + 10) p->x = (float)g_width + 10.0f;

    // Write to positions array. We could apply zoom here, but for now we
    // just pass through, so zoom is effectively ignored.
    g_positions[i * 2 + 0] = p->x;
    g_positions[i * 2 + 1] = p->y;
  }
}

