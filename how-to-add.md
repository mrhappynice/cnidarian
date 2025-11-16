

1. **Concept: how animations plug in**
2. **C side: how to write a new animation file**
3. **Build side: how to compile it**
4. **UI / JS side: how to hook it into the selector**
5. **Checklist you can literally follow next time**

References are based on your current project layout. 

---

## 1. Concept: how animations plug in

Each animation is:

* One **C file** → compiled by Emscripten into its own `pkg/<name>.js` + `.wasm`.
* It exposes the **same C API** (the `lb_*` functions).
* The JS (`main.js`) doesn’t care *which* module it’s talking to, as long as those functions exist.
* The UI has a selector that picks which module is “active.”

So: **new animation = new C file with the same `lb_*` exports** + a couple small build & JS changes.

---

## 2. C side: how to write a new animation

### 2.1. Required API (you can think of this as an interface)

Every animation C file must define these functions:

```c
EMSCRIPTEN_KEEPALIVE void  lb_set_canvas(int width, int height);
EMSCRIPTEN_KEEPALIVE void  lb_set_density(float density);
EMSCRIPTEN_KEEPALIVE void  lb_set_speed(float speed);
EMSCRIPTEN_KEEPALIVE void  lb_set_zoom(float zoom);
EMSCRIPTEN_KEEPALIVE void  lb_set_zoom_auto(int on);
EMSCRIPTEN_KEEPALIVE void  lb_reset(void);
EMSCRIPTEN_KEEPALIVE void  lb_step(float dt);
EMSCRIPTEN_KEEPALIVE int   lb_get_point_count(void);
EMSCRIPTEN_KEEPALIVE float lb_get_x(int i);
EMSCRIPTEN_KEEPALIVE float lb_get_y(int i);
```

* JS calls these via `cwrap` and uses them for:

  * **Canvas size** (so you know the screen size),
  * **Controls** (speed, density, zoom),
  * **Simulation step** (`lb_step(dt)`),
  * **Point cloud** (positions for drawing).

You’re free to **ignore** any inputs you don’t care about (e.g. zoom) as long as the function exists and doesn’t crash.

---

### 2.2. Basic structure of a new animation file

Here’s a template you can copy to make, say, `src/swirls.c`:

```c
// src/swirls.c
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
static float g_baseDensity = 0.005f;   // points per pixel
static float g_speed       = 1.0f;
static float g_zoom        = 1.0f;
static int   g_zoomAuto    = 0;
static float g_t           = 0.0f;

static const int   G_MIN_POINTS = 6000;
static const int   G_MAX_POINTS = 120000;

static int    g_N         = 0;
static float* g_positions = NULL;  // [x0, y0, x1, y1, ...]

// ===== Helpers =====
static void free_field(void) {
  free(g_positions);
  g_positions = NULL;
  g_N = 0;
}

static void rebuild_field(void) {
  if (g_width <= 0 || g_height <= 0) return;

  double target_d = (double)g_baseDensity * (double)g_width * (double)g_height;
  int target = (int)target_d;
  if (target < G_MIN_POINTS)  target = G_MIN_POINTS;
  if (target > G_MAX_POINTS)  target = G_MAX_POINTS;

  if (target == g_N && g_positions) return;

  free_field();

  g_N = target;
  g_positions = (float*)malloc(sizeof(float) * g_N * 2);

  if (!g_positions) {
    free_field();
    return;
  }
}

// ===== API impl =====

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
  g_t = 0.0f;
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

// ===== Core animation step =====

EMSCRIPTEN_KEEPALIVE
void lb_step(float dt) {
  if (!g_positions || g_N <= 0) return;
  if (g_width <= 0 || g_height <= 0) return;

  if (dt < 0.0001f) dt = 0.0001f;
  if (dt > 0.1f)    dt = 0.1f;

  g_t += dt * g_speed;

  float cx = (float)g_width  * 0.5f;
  float cy = (float)g_height * 0.5f;
  float radiusBase = (float)(g_height < g_width ? g_height : g_width) * 0.35f * g_zoom;

  for (int i = 0; i < g_N; i++) {
    float u = (float)i / (float)g_N;    // 0..1
    float angle = u * 20.0f + g_t * 0.7f;
    float r = radiusBase * (0.3f + 0.7f * u);

    float x = cx + cosf(angle) * r;
    float y = cy + sinf(angle * 1.3f) * r * 0.5f;

    g_positions[i * 2 + 0] = x;
    g_positions[i * 2 + 1] = y;
  }
}
```

**Where to put your creativity:**

* Everything inside `lb_step` is your playground:

  * Use `i` / `u` for indexing,
  * Use `g_t` as time,
  * Use `g_width` / `g_height` to scale to canvas,
  * Combine `sin`, `cos`, noise, etc. to create shapes.
* As long as you fill `g_positions` with `[x, y]` pairs inside the canvas, it will draw fine.

If you need per-point state (like particles), copy the pattern from your `fire.c` (particle structs + `respawn` logic) and just make sure you write positions every frame.

---

## 3. Build side: adding it to Dockerfile.build

Your `Dockerfile.build` currently compiles `livebg.c` (and now `fire.c`). 

To add a new animation, e.g. `swirls.c`, add another `emcc` line in the same RUN:

```dockerfile
RUN mkdir -p pkg \
 && source /emsdk/emsdk_env.sh \
 && emcc src/livebg.c -O3 \
      -sENVIRONMENT=web \
      -sEXPORT_ES6=1 -sMODULARIZE=1 \
      -sALLOW_MEMORY_GROWTH=1 \
      -sEXPORTED_RUNTIME_METHODS=['ccall','cwrap'] \
      -o pkg/livebg.js \
 && emcc src/fire.c -O3 \
      -sENVIRONMENT=web \
      -sEXPORT_ES6=1 -sMODULARIZE=1 \
      -sALLOW_MEMORY_GROWTH=1 \
      -sEXPORTED_RUNTIME_METHODS=['ccall','cwrap'] \
      -o pkg/fire.js \
 && emcc src/swirls.c -O3 \
      -sENVIRONMENT=web \
      -sEXPORT_ES6=1 -sMODULARIZE=1 \
      -sALLOW_MEMORY_GROWTH=1 \
      -sEXPORTED_RUNTIME_METHODS=['ccall','cwrap'] \
      -o pkg/swirls.js
```

You don’t have to change `pack-dist.sh` because it already copies all `.js`/`.wasm` from `pkg/` into `dist/pkg/`. 

---

## 4. UI & JS side: wiring it into the selector

You already have:

* A `<select id="effectSelect">` in the controls drawer,
* A `main.js` with an `effects` map, `loadEffect(name)`, and an `activeEffect` switcher.

### 4.1. Add an option in HTML

Inside your existing `<select id="effectSelect">`:

```html
<select id="effectSelect">
  <option value="livebg">Live BG</option>
  <option value="fire">Fire</option>
  <option value="swirls">Swirls</option> <!-- NEW -->
</select>
```

### 4.2. Import the new module in `main.js`

At the top of `web/main.js`, where you currently have:

```js
import createLiveBg from "../pkg/livebg.js";
import createFire   from "../pkg/fire.js";
```

Add:

```js
import createSwirls from "../pkg/swirls.js";
```

### 4.3. Update the `loadEffect` function

Inside `loadEffect(name)` you already have a `switch`-ish logic:

```js
let createFn;
if (name === "livebg")      createFn = createLiveBg;
else if (name === "fire")   createFn = createFire;
else throw new Error("Unknown effect: " + name);
```

Extend it:

```js
let createFn;
if (name === "livebg")      createFn = createLiveBg;
else if (name === "fire")   createFn = createFire;
else if (name === "swirls") createFn = createSwirls;
else throw new Error("Unknown effect: " + name);
```

That’s it. The rest of the system (wrappers like `lb_step`, `lb_get_x`, `lb_get_y`, and the render loop) will automatically use the new animation when you pick it from the dropdown.

---

## 5. Step-by-step checklist for future you

When “future you” wants to add a new effect (say `nebula`), here’s the literal checklist:

1. **Create the C file**

   * Copy an existing one (e.g. `src/fire.c` or `src/swirls.c`) to `src/nebula.c`.
   * Rename the internal structs/logic but keep the **same `lb_*` functions**.
   * Implement your animation logic inside `lb_step(float dt)`.

2. **Update the build** (`Dockerfile.build`)

   * Add:

     ```dockerfile
     emcc src/nebula.c -O3 \
       -sENVIRONMENT=web \
       -sEXPORT_ES6=1 -sMODULARIZE=1 \
       -sALLOW_MEMORY_GROWTH=1 \
       -sEXPORTED_RUNTIME_METHODS=['ccall','cwrap'] \
       -o pkg/nebula.js
     ```

   * Rebuild the “build” docker image and run the build script to regenerate `pkg/*.js` & `.wasm`.

3. **Import it in `main.js`**

   * Add:

     ```js
     import createNebula from "../pkg/nebula.js";
     ```

4. **Teach `loadEffect` about it**

   * Extend the `if/else` inside `loadEffect`:

     ```js
     else if (name === "nebula") createFn = createNebula;
     ```

5. **Add it to the selector UI**

   * In `index.html`, inside `<select id="effectSelect">`, add:

     ```html
     <option value="nebula">Nebula</option>
     ```

6. **Test**

   * Rebuild, run the container, open the page.
   * Pick “Nebula” in the dropdown.
   * If it doesn’t show anything, check:

     * Did `lb_get_point_count()` return > 0?
     * Are your `x, y` coordinates in the [0, width] / [0, height] range?

---

If you’d like, I can also give you:

* A **“template.c”** file you keep in `src/` as a starting point for all new effects.
* Or a “terminal-fire” style C template (char grid) if you decide to go that route later.
