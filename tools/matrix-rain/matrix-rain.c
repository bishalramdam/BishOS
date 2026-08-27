/* Matrix digital rain as a Wayland wallpaper, drawn by the GPU.
 *
 * A wallpaper on Wayland is a surface on the compositor's background layer --
 * the wlr-layer-shell protocol, the same one swaybg uses. There is no root
 * window to draw on the way X11 had, so this asks for a background surface per
 * output, anchors it to all four edges, and gives it an empty input region so
 * clicks fall through to whatever is behind.
 *
 * The rain itself is one fragment shader. Nothing is simulated on the CPU and
 * no per-cell state is stored: every pixel works out on its own which column it
 * is in, where that column's drop has fallen to by now, and how bright it
 * should therefore be. That makes the whole animation a pure function of time,
 * which is why this costs a few percent of an integrated GPU and nothing
 * measurable on the CPU.
 *
 *     matrix-rain                  # 30 fps, 16-pixel cells
 *     matrix-rain --fps 60
 *     matrix-rain --cell 12        # smaller glyphs, more columns
 *     matrix-rain --density 0.5    # fewer columns raining
 *     matrix-rain --speed 1.8      # faster fall
 */
#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <getopt.h>
#include <math.h>
#include <poll.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <wayland-client.h>
#include <wayland-egl.h>
#include <EGL/egl.h>
#include <GLES2/gl2.h>

#include "wlr-layer-shell-unstable-v1-client-protocol.h"
#include "glyphs.h"

struct output {
    struct wl_output *wl;
    uint32_t name;
    struct wl_surface *surface;
    struct zwlr_layer_surface_v1 *layer;
    struct wl_egl_window *egl_window;
    EGLSurface egl_surface;
    struct wl_callback *frame;
    int32_t width, height;
    bool ready;
    double next_due;
    struct output *next;
};

static struct wl_display *display;
static struct wl_compositor *compositor;
static struct zwlr_layer_shell_v1 *layer_shell;
static struct output *outputs;

static EGLDisplay egl_display = EGL_NO_DISPLAY;
static EGLContext egl_context = EGL_NO_CONTEXT;
static EGLConfig egl_config;

static GLuint program, atlas, vbo;
static GLint u_res, u_time, u_cell, u_glyphs, u_speed, u_spawn, u_drops, u_atlas;
static bool gl_ready;

static double start_time;
static double frame_interval = 1.0 / 30.0;
static float opt_cell = 16.0f;
static float opt_speed = 1.0f;
static float opt_density = 0.85f;
/* Matches the loop bound in the shader, which cannot be a uniform. */
#define MAX_DROPS 4.0f
static volatile sig_atomic_t running = 1;

static double now_sec(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}

static void on_signal(int sig)
{
    (void)sig;
    running = 0;
}

/* ------------------------------------------------------------------ shaders */

static const char *VERT =
    "attribute vec2 pos;\n"
    "void main() { gl_Position = vec4(pos, 0.0, 1.0); }\n";

static const char *FRAG =
    "precision highp float;\n"
    "uniform vec2 uRes;\n"
    "uniform float uTime;\n"
    "uniform vec2 uCell;\n"
    "uniform float uGlyphs;\n"
    "uniform float uSpeed;\n"
    "uniform float uSpawn;\n"
    "uniform float uDrops;\n"
    "uniform sampler2D uAtlas;\n"
    "\n"
    "float hash11(float n) { return fract(sin(n * 127.1) * 43758.5453123); }\n"
    "float hash21(vec2 p) {\n"
    "    return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453123);\n"
    "}\n"
    "\n"
    "void main() {\n"
    /* gl_FragCoord has y growing upward; the rain thinks in rows from the top. */
    "    vec2 p = vec2(gl_FragCoord.x, uRes.y - gl_FragCoord.y);\n"
    "    vec2 cell = floor(p / uCell);\n"
    "    vec2 f = fract(p / uCell);\n"
    "    float rows = ceil(uRes.y / uCell.y);\n"
    "\n"
    /* Everything about a column is derived from its index, so no state is kept. */
    "    float col = cell.x;\n"
    "    float bright = 0.0;\n"
    "    float headness = 0.0;\n"
    "\n"
    /* Past a full screen of columns the only way to get denser is more than
     * one drop falling in the same column at once, so density above 1.0 adds
     * drops. The loop bound has to be a constant -- GLSL ES 1.00 requires it
     * -- so unused drops are masked to zero rather than broken out of. */
    "    for (int i = 0; i < 4; i++) {\n"
    "        float fi = float(i);\n"
    "        float use = step(fi + 0.5, uDrops);\n"
    "        float seed = col * 2.71 + fi * 37.13;\n"
    "        float on = step(hash11(seed * 3.31 + 5.0), uSpawn);\n"
    "        float speed = (0.35 + 0.85 * hash11(seed)) * uSpeed;\n"
    "        float trail = 6.0 + 22.0 * hash11(seed * 1.77);\n"
    /* One cycle carries a head from above the screen to past the bottom. */
    "        float cycle = rows + trail + 8.0;\n"
    "        float head = mod(uTime * speed * 9.0 + hash11(seed * 7.77) * cycle,\n"
    "                         cycle) - trail;\n"
    /* Distance behind the head in rows: 0 is the head, trail is the tail. */
    "        float dist = head - cell.y;\n"
    "        float lit = step(0.0, dist) * step(dist, trail);\n"
    "        float b = lit * pow(1.0 - dist / trail, 1.7) * on * use;\n"
    /* Where drops overlap the brightest wins, and the colour follows it. */
    "        if (b > bright) {\n"
    "            bright = b;\n"
    "            headness = smoothstep(2.2, 0.0, dist);\n"
    "        }\n"
    "    }\n"
    "\n"
    /* Glyphs churn at a rate of their own so the trails shimmer unevenly. */
    "    float rate = 1.5 + 6.0 * hash11(col * 3.31 + cell.y * 0.017);\n"
    "    float step_t = floor(uTime * rate + hash21(cell) * 10.0);\n"
    "    float gi = floor(hash21(cell + vec2(step_t * 13.0, step_t * 7.0)) * uGlyphs);\n"
    "    gi = min(gi, uGlyphs - 1.0);\n"
    "\n"
    "    float ink = texture2D(uAtlas, vec2((gi + f.x) / uGlyphs, f.y)).r;\n"
    "\n"
    /* The leading cell is near-white; the trail settles into green. */
    "    vec3 trailCol = vec3(0.07, 0.85, 0.22);\n"
    "    vec3 headCol = vec3(0.80, 1.00, 0.85);\n"
    "    float flicker = 0.85 + 0.15 * hash21(cell + vec2(step_t, 0.0));\n"
    "    vec3 c = mix(trailCol, headCol, headness) * bright * ink * flicker;\n"
    "\n"
    "    gl_FragColor = vec4(c, 1.0);\n"
    "}\n";

static GLuint compile(GLenum type, const char *src)
{
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, NULL);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[2048];
        glGetShaderInfoLog(s, sizeof log, NULL, log);
        fprintf(stderr, "matrix-rain: shader failed to compile:\n%s\n", log);
        exit(1);
    }
    return s;
}

/* The atlas is one row of 8x8 glyphs. NEAREST keeps the pixel edges hard,
 * which is what makes it read as a bitmap font rather than a blur. */
static void build_atlas(void)
{
    int w = GLYPH_COUNT * GLYPH_W;
    unsigned char *px = calloc((size_t)w * GLYPH_H, 1);
    if (!px) {
        fprintf(stderr, "matrix-rain: out of memory building the atlas\n");
        exit(1);
    }
    for (int g = 0; g < GLYPH_COUNT; g++)
        for (int y = 0; y < GLYPH_H; y++) {
            const char *row = GLYPH_ROWS[g * GLYPH_H + y];
            for (int x = 0; x < GLYPH_W; x++)
                px[(size_t)y * w + g * GLYPH_W + x] = row[x] == '#' ? 255 : 0;
        }

    glGenTextures(1, &atlas);
    glBindTexture(GL_TEXTURE_2D, atlas);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE, w, GLYPH_H, 0,
                 GL_LUMINANCE, GL_UNSIGNED_BYTE, px);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    free(px);
}

static void gl_init(void)
{
    GLuint vs = compile(GL_VERTEX_SHADER, VERT);
    GLuint fs = compile(GL_FRAGMENT_SHADER, FRAG);
    program = glCreateProgram();
    glAttachShader(program, vs);
    glAttachShader(program, fs);
    glBindAttribLocation(program, 0, "pos");
    glLinkProgram(program);
    GLint ok = 0;
    glGetProgramiv(program, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[2048];
        glGetProgramInfoLog(program, sizeof log, NULL, log);
        fprintf(stderr, "matrix-rain: shader failed to link:\n%s\n", log);
        exit(1);
    }
    glDeleteShader(vs);
    glDeleteShader(fs);

    u_res = glGetUniformLocation(program, "uRes");
    u_time = glGetUniformLocation(program, "uTime");
    u_cell = glGetUniformLocation(program, "uCell");
    u_glyphs = glGetUniformLocation(program, "uGlyphs");
    u_speed = glGetUniformLocation(program, "uSpeed");
    u_spawn = glGetUniformLocation(program, "uSpawn");
    u_drops = glGetUniformLocation(program, "uDrops");
    u_atlas = glGetUniformLocation(program, "uAtlas");

    /* Two triangles covering clip space. The shader does the rest. */
    static const GLfloat quad[] = {
        -1.0f, -1.0f,  1.0f, -1.0f, -1.0f, 1.0f,
        -1.0f,  1.0f,  1.0f, -1.0f,  1.0f, 1.0f,
    };
    glGenBuffers(1, &vbo);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof quad, quad, GL_STATIC_DRAW);

    build_atlas();
    gl_ready = true;
}

/* ------------------------------------------------------------------ drawing */

static const struct wl_callback_listener frame_listener;

static void draw(struct output *o)
{
    if (!eglMakeCurrent(egl_display, o->egl_surface, o->egl_surface, egl_context)) {
        fprintf(stderr, "matrix-rain: eglMakeCurrent failed\n");
        running = 0;
        return;
    }
    if (!gl_ready)
        gl_init();

    glViewport(0, 0, o->width, o->height);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    glUseProgram(program);
    glUniform2f(u_res, (float)o->width, (float)o->height);
    glUniform1f(u_time, (float)(now_sec() - start_time));
    glUniform2f(u_cell, opt_cell, opt_cell);
    glUniform1f(u_glyphs, (float)GLYPH_COUNT);
    glUniform1f(u_speed, opt_speed);
    /* density 1.5 becomes two drops per column, each 75% likely to be
     * running -- the same split matrix_rain.py uses. */
    float drops = ceilf(opt_density);
    if (drops < 1.0f)
        drops = 1.0f;
    glUniform1f(u_drops, drops);
    glUniform1f(u_spawn, opt_density / drops);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, atlas);
    glUniform1i(u_atlas, 0);

    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, NULL);
    glDrawArrays(GL_TRIANGLES, 0, 6);

    /* Ask for the next frame before swapping: the callback is what tells us
     * the compositor actually painted, and it stops arriving when the
     * wallpaper is fully covered, which is free power saving. */
    o->frame = wl_surface_frame(o->surface);
    wl_callback_add_listener(o->frame, &frame_listener, o);
    eglSwapBuffers(egl_display, o->egl_surface);
}

static void frame_done(void *data, struct wl_callback *cb, uint32_t time)
{
    (void)time;
    struct output *o = data;
    wl_callback_destroy(cb);
    o->frame = NULL;
}

static const struct wl_callback_listener frame_listener = { .done = frame_done };

/* ------------------------------------------------------------- layer surface */

static void output_destroy(struct output *o);

static void layer_configure(void *data, struct zwlr_layer_surface_v1 *ls,
                            uint32_t serial, uint32_t w, uint32_t h)
{
    struct output *o = data;
    zwlr_layer_surface_v1_ack_configure(ls, serial);
    o->width = (int32_t)w;
    o->height = (int32_t)h;

    if (!o->egl_window) {
        o->egl_window = wl_egl_window_create(o->surface, o->width, o->height);
        o->egl_surface = eglCreateWindowSurface(egl_display, egl_config,
                                                (EGLNativeWindowType)o->egl_window,
                                                NULL);
        if (o->egl_surface == EGL_NO_SURFACE) {
            fprintf(stderr, "matrix-rain: could not create an EGL surface\n");
            running = 0;
            return;
        }
    } else {
        wl_egl_window_resize(o->egl_window, o->width, o->height, 0, 0);
    }

    o->ready = true;
    o->next_due = 0.0;
}

static void layer_closed(void *data, struct zwlr_layer_surface_v1 *ls)
{
    (void)ls;
    output_destroy(data);
}

static const struct zwlr_layer_surface_v1_listener layer_listener = {
    .configure = layer_configure,
    .closed = layer_closed,
};

static void output_init(struct output *o)
{
    o->surface = wl_compositor_create_surface(compositor);
    o->layer = zwlr_layer_shell_v1_get_layer_surface(
        layer_shell, o->surface, o->wl,
        ZWLR_LAYER_SHELL_V1_LAYER_BACKGROUND, "matrix-rain");

    zwlr_layer_surface_v1_set_anchor(o->layer,
                                     ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP |
                                     ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM |
                                     ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT |
                                     ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT);
    /* -1 means panels with an exclusive zone do not shrink us. */
    zwlr_layer_surface_v1_set_exclusive_zone(o->layer, -1);
    zwlr_layer_surface_v1_set_keyboard_interactivity(o->layer, 0);
    zwlr_layer_surface_v1_add_listener(o->layer, &layer_listener, o);

    /* An empty input region: the wallpaper never swallows a click. */
    struct wl_region *empty = wl_compositor_create_region(compositor);
    wl_surface_set_input_region(o->surface, empty);
    wl_region_destroy(empty);

    wl_surface_commit(o->surface);
}

static void output_destroy(struct output *o)
{
    for (struct output **p = &outputs; *p; p = &(*p)->next)
        if (*p == o) {
            *p = o->next;
            break;
        }
    if (o->frame)
        wl_callback_destroy(o->frame);
    if (o->egl_surface != EGL_NO_SURFACE)
        eglDestroySurface(egl_display, o->egl_surface);
    if (o->egl_window)
        wl_egl_window_destroy(o->egl_window);
    if (o->layer)
        zwlr_layer_surface_v1_destroy(o->layer);
    if (o->surface)
        wl_surface_destroy(o->surface);
    wl_output_destroy(o->wl);
    free(o);
}

/* ----------------------------------------------------------------- registry */

static void reg_global(void *data, struct wl_registry *reg, uint32_t name,
                       const char *iface, uint32_t version)
{
    (void)data;
    if (!strcmp(iface, wl_compositor_interface.name)) {
        compositor = wl_registry_bind(reg, name, &wl_compositor_interface,
                                      version < 4 ? version : 4);
    } else if (!strcmp(iface, zwlr_layer_shell_v1_interface.name)) {
        layer_shell = wl_registry_bind(reg, name, &zwlr_layer_shell_v1_interface,
                                       version < 4 ? version : 4);
    } else if (!strcmp(iface, wl_output_interface.name)) {
        struct output *o = calloc(1, sizeof *o);
        if (!o)
            return;
        o->wl = wl_registry_bind(reg, name, &wl_output_interface, 1);
        o->name = name;
        o->egl_surface = EGL_NO_SURFACE;
        o->next = outputs;
        outputs = o;
        /* Monitors plugged in later get a wallpaper too, but only once the
         * globals they need have arrived. */
        if (compositor && layer_shell)
            output_init(o);
    }
}

static void reg_remove(void *data, struct wl_registry *reg, uint32_t name)
{
    (void)data;
    (void)reg;
    for (struct output *o = outputs; o; o = o->next)
        if (o->name == name) {
            output_destroy(o);
            return;
        }
}

static const struct wl_registry_listener reg_listener = {
    .global = reg_global,
    .global_remove = reg_remove,
};

/* --------------------------------------------------------------------- main */

static void usage(void)
{
    puts("matrix-rain -- Matrix digital rain on the Wayland background layer\n"
         "\n"
         "  --fps N        frames per second (default 30)\n"
         "  --cell N       glyph cell size in pixels (default 16)\n"
         "  --speed N      fall speed multiplier (default 1.0)\n"
         "  --density N    up to 1.0 = share of columns raining; above that,\n"
         "                 simultaneous drops per column, max 4 (default 0.85)\n"
         "  --help");
}

int main(int argc, char **argv)
{
    double fps = 30.0;
    static const struct option opts[] = {
        { "fps", required_argument, NULL, 'f' },
        { "cell", required_argument, NULL, 'c' },
        { "speed", required_argument, NULL, 's' },
        { "density", required_argument, NULL, 'd' },
        { "help", no_argument, NULL, 'h' },
        { 0, 0, 0, 0 },
    };
    int c;
    while ((c = getopt_long(argc, argv, "f:c:s:d:h", opts, NULL)) != -1) {
        switch (c) {
        case 'f': fps = atof(optarg); break;
        case 'c': opt_cell = (float)atof(optarg); break;
        case 's': opt_speed = (float)atof(optarg); break;
        case 'd': opt_density = (float)atof(optarg); break;
        case 'h': usage(); return 0;
        default: usage(); return 1;
        }
    }
    if (fps < 1.0) fps = 1.0;
    if (opt_cell < 4.0f) opt_cell = 4.0f;
    if (opt_density < 0.0f) opt_density = 0.0f;
    if (opt_density > MAX_DROPS) opt_density = MAX_DROPS;
    frame_interval = 1.0 / fps;

    /* The glyph table is hand-edited text, so check its shape rather than
     * trusting it and reading past the end of a row. */
    if (GLYPH_ROW_COUNT % GLYPH_H != 0) {
        fprintf(stderr, "matrix-rain: glyph table is not a multiple of %d rows\n",
                GLYPH_H);
        return 1;
    }
    for (int i = 0; i < GLYPH_ROW_COUNT; i++)
        if ((int)strlen(GLYPH_ROWS[i]) != GLYPH_W) {
            fprintf(stderr, "matrix-rain: glyph row %d is %d wide, expected %d\n",
                    i, (int)strlen(GLYPH_ROWS[i]), GLYPH_W);
            return 1;
        }

    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);

    display = wl_display_connect(NULL);
    if (!display) {
        fprintf(stderr, "matrix-rain: no Wayland display "
                        "(is WAYLAND_DISPLAY set?)\n");
        return 1;
    }

    struct wl_registry *reg = wl_display_get_registry(display);
    wl_registry_add_listener(reg, &reg_listener, NULL);
    wl_display_roundtrip(display);

    if (!compositor || !layer_shell) {
        fprintf(stderr, "matrix-rain: the compositor does not offer "
                        "wlr-layer-shell, so there is no background to draw on\n");
        return 1;
    }
    if (!outputs) {
        fprintf(stderr, "matrix-rain: no outputs\n");
        return 1;
    }

    egl_display = eglGetDisplay((EGLNativeDisplayType)display);
    if (egl_display == EGL_NO_DISPLAY || !eglInitialize(egl_display, NULL, NULL)) {
        fprintf(stderr, "matrix-rain: could not initialise EGL\n");
        return 1;
    }
    eglBindAPI(EGL_OPENGL_ES_API);

    static const EGLint cfg_attrs[] = {
        EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
        EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8,
        EGL_NONE,
    };
    EGLint n = 0;
    if (!eglChooseConfig(egl_display, cfg_attrs, &egl_config, 1, &n) || n == 0) {
        fprintf(stderr, "matrix-rain: no suitable EGL config\n");
        return 1;
    }
    static const EGLint ctx_attrs[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE };
    egl_context = eglCreateContext(egl_display, egl_config, EGL_NO_CONTEXT, ctx_attrs);
    if (egl_context == EGL_NO_CONTEXT) {
        fprintf(stderr, "matrix-rain: could not create a GLES2 context\n");
        return 1;
    }

    for (struct output *o = outputs; o; o = o->next)
        output_init(o);

    start_time = now_sec();

    struct pollfd pfd = { .fd = wl_display_get_fd(display), .events = POLLIN };
    while (running) {
        /* Work out how long we can sleep: the soonest frame that is due on any
         * output, or indefinitely if every output is waiting on a callback. */
        double now = now_sec();
        int timeout = -1;
        for (struct output *o = outputs; o; o = o->next) {
            if (!o->ready || o->frame)
                continue;
            double d = o->next_due - now;
            if (d < 0.0)
                d = 0.0;
            int ms = (int)(d * 1000.0) + 1;
            if (timeout < 0 || ms < timeout)
                timeout = ms;
        }

        while (wl_display_prepare_read(display) != 0)
            wl_display_dispatch_pending(display);
        wl_display_flush(display);

        int ret = poll(&pfd, 1, timeout);
        if (ret > 0 && (pfd.revents & POLLIN))
            wl_display_read_events(display);
        else
            wl_display_cancel_read(display);

        if (ret < 0 && errno != EINTR) {
            fprintf(stderr, "matrix-rain: poll: %s\n", strerror(errno));
            break;
        }
        if (wl_display_dispatch_pending(display) < 0) {
            /* A protocol error kills the connection, and saying only that it
             * ended is useless -- name the interface that objected. */
            int err = wl_display_get_error(display);
            const struct wl_interface *iface = NULL;
            uint32_t code = 0, id = 0;
            if (err == EPROTO)
                code = wl_display_get_protocol_error(display, &iface, &id);
            if (err == EPROTO)
                fprintf(stderr, "matrix-rain: protocol error %u on %s (object %u)\n",
                        code, iface ? iface->name : "?", id);
            else
                fprintf(stderr, "matrix-rain: connection lost: %s\n", strerror(err));
            return 1;
        }

        now = now_sec();
        struct output *next;
        for (struct output *o = outputs; o; o = next) {
            next = o->next;
            if (!o->ready || o->frame || now + 0.0005 < o->next_due)
                continue;
            o->next_due = now + frame_interval;
            draw(o);
        }
    }

    while (outputs)
        output_destroy(outputs);
    if (egl_context != EGL_NO_CONTEXT) {
        eglMakeCurrent(egl_display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        eglDestroyContext(egl_display, egl_context);
    }
    eglTerminate(egl_display);
    wl_display_disconnect(display);
    return 0;
}
