/* test_allegro4_full.c
 * Comprehensive Allegro 4 test suite — 9 interactive pages.
 *
 * Navigate : LEFT / RIGHT arrows, or press 1-9 directly.
 * Quit     : ESC
 *
 * Pages
 *   1  Drawing primitives (lines, rects, circles, arcs, splines, polygons …)
 *   2  Bitmap ops        (blit, stretch, clipping, sub-bitmaps)
 *   3  Sprite transforms (rotate, flip, scale, pivot, lit, trans)
 *   4  Blending modes    (trans, add, burn, dodge, multiply, screen, XOR …)
 *   5  Text rendering    (alignment, colours, metrics)
 *   6  Keyboard input    (live key-state grid)
 *   7  Mouse input       (position, buttons, wheel, mickeys)
 *   8  Timer & FPS       (timing accuracy, retrace counter, busy-loop cost)
 *   9  Images            (bitmap loading, sprite draw, transforms)
 */

#include <allegro.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <signal.h>
#include <unistd.h>

/* ── dimensions ─────────────────────────────────────────────────────────── */
#define W        800
#define H        600
#define BAR_H     22          /* status bar at the bottom */
#define CH       (H - BAR_H) /* usable content height */
#define CX       (W  / 2)
#define CY       (CH / 2)

/* ── pages ───────────────────────────────────────────────────────────────── */
#define NUM_PAGES 10
#define NUM_FRAMES 10
#define ASSETS_DIR "bonhomme"

static const char *PAGE_NAMES[NUM_PAGES] = {
    "1: Primitives",
    "2: Bitmaps",
    "3: Sprites",
    "4: Blending",
    "5: Text",
    "6: Keyboard",
    "7: Mouse",
    "8: Timer",
    "9: Images",
    "0: Sound",
};

/* ── globals ─────────────────────────────────────────────────────────────── */
static BITMAP *buf;
static int     page = 0;
static int     tick = 0;

/* pre-allocated bitmaps — created once after set_gfx_mode, freed at exit */
static BITMAP *bmp_src;         /* page 2: static grid source     */
static BITMAP *bmp_msrc;        /* page 2: masked circle source   */
static BITMAP *bmp_full;        /* page 2: sub-bitmap parent      */
static BITMAP *bmp_sub;         /* page 2: sub-bitmap child       */
static BITMAP *bmp_clip;        /* page 2: clipping demo (animated) */
static BITMAP *bmp_spr;         /* page 3: arrow sprite           */
static BITMAP *bmp_blend[9];    /* page 4: blending sources (animated) */
static BITMAP *frames[NUM_FRAMES]; /* page 9: animation frames           */
static int     frames_loaded = 0;

/* ── page 10: sound state ───────────────────────────────────────────────── */
#define MP3_FILE "assets/1-Flo Rida My House Official Audio -TuxMwALL_S4-192k-1698461064.mp3"
static pid_t snd_pid     = -1;
static int   snd_playing = 0;
static int   snd_result  = -1;   /* return value of install_sound() */

#define DISABLE_ROTATION_DEMOS 0

static BITMAP *load_frame_bitmap(int index)
{
    static const char *dirs[] = { ASSETS_DIR, "assets" };

    for (int i = 0; i < (int)(sizeof(dirs) / sizeof(dirs[0])); i++) {
        char path[256];
        snprintf(path, sizeof(path), "%s/bonh_transp%d.bmp", dirs[i], index);
        BITMAP *frame = load_bitmap(path, NULL);
        if (frame)
            return frame;
    }

    return NULL;
}

static void draw_unavailable_demo(int x, int y, int w, int h, const char *msg)
{
    int border = makecol(120, 70, 70);
    int fg = makecol(220, 220, 220);
    int bg = makecol(45, 25, 25);

    rectfill(buf, x, y, x + w - 1, y + h - 1, bg);
    rect(buf, x, y, x + w - 1, y + h - 1, border);
    textout_centre_ex(buf, font, "Skipped on macOS arm64", x + w / 2, y + h / 2 - 8, fg, -1);
    textout_centre_ex(buf, font, msg, x + w / 2, y + h / 2 + 6, border, -1);
}

static volatile int fps_raw   = 0;
static volatile int fps_value = 0;

static void fps_frame_cb(void) { fps_raw++;                     }
END_OF_FUNCTION(fps_frame_cb)
static void fps_sec_cb(void)   { fps_value = fps_raw; fps_raw = 0; }
END_OF_FUNCTION(fps_sec_cb)

/* ── colour helpers ──────────────────────────────────────────────────────── */

/* hue: h 0-359  s/v 0-255 → makecol() */
static int hsv(int h, int s, int v)
{
    float Hf = (float)h / 60.0f;
    float S  = (float)s / 255.0f;
    float V  = (float)v / 255.0f;
    int   i  = (int)Hf % 6;
    float f  = Hf - (int)Hf;
    float p = V * (1.0f - S);
    float q = V * (1.0f - S * f);
    float t = V * (1.0f - S * (1.0f - f));
    float r, g, b;
    switch (i) {
        case 0: r=V; g=t; b=p; break;
        case 1: r=q; g=V; b=p; break;
        case 2: r=p; g=V; b=t; break;
        case 3: r=p; g=q; b=V; break;
        case 4: r=t; g=p; b=V; break;
        default:r=V; g=p; b=q; break;
    }
    return makecol((int)(r*255), (int)(g*255), (int)(b*255));
}

/* rainbow colour cycling for animations */
static int rainbow(int offset)
{
    return hsv((tick * 2 + offset) % 360, 220, 230);
}

/* ── status bar ──────────────────────────────────────────────────────────── */
static void draw_statusbar(void)
{
    int y   = CH;
    int sep = makecol(80, 80, 80);
    rectfill(buf, 0, y, W - 1, H - 1, makecol(20, 20, 20));
    line(buf, 0, y, W - 1, y, sep);

    /* page name */
    textprintf_ex(buf, font, 6, y + 4, makecol(200, 200, 200), -1,
                  "%s", PAGE_NAMES[page]);

    /* navigation hint */
    textprintf_ex(buf, font, CX - 100, y + 4, makecol(120, 120, 120), -1,
                  "< LEFT / RIGHT >  or  1-9, 0");

    /* FPS */
    textprintf_ex(buf, font, W - 70, y + 4, makecol(100, 200, 100), -1,
                  "FPS: %d", fps_value);
}

/* ════════════════════════════════════════════════════════════════════════════
 * PAGE 1: PRIMITIVES
 * ════════════════════════════════════════════════════════════════════════════ */
static void page_primitives(void)
{
    int col_dim = makecol(40,  40,  40);
    int white   = makecol(220, 220, 220);
    int grey    = makecol(120, 120, 120);

    /* ── section headers ── */
    textout_ex(buf, font, "line()",        6,   6, grey, -1);
    textout_ex(buf, font, "rect()",       206,  6, grey, -1);
    textout_ex(buf, font, "circle()",     406,  6, grey, -1);
    textout_ex(buf, font, "ellipse()",    606,  6, grey, -1);

    textout_ex(buf, font, "arc()",          6, 156, grey, -1);
    textout_ex(buf, font, "spline()",      206, 156, grey, -1);
    textout_ex(buf, font, "polygon()",     406, 156, grey, -1);
    textout_ex(buf, font, "floodfill()",   606, 156, grey, -1);

    textout_ex(buf, font, "triangle()",      6, 306, grey, -1);
    textout_ex(buf, font, "rectfill(grad)", 206, 306, grey, -1);
    textout_ex(buf, font, "circlefill()",   406, 306, grey, -1);
    textout_ex(buf, font, "ellipsefill()",  606, 306, grey, -1);

    /* vertical/horizontal dividers */
    for (int x = 200; x < W; x += 200) line(buf, x, 0, x, CH, col_dim);
    for (int y = 150; y < CH; y += 150) line(buf, 0, y, W, y, col_dim);

    /* ── row 0 ── */

    /* lines: animated radiating spokes */
    {
        int x0 = 100, y0 = 75;
        for (int i = 0; i < 24; i++) {
            float a = (float)(i * 15 + tick) * (float)M_PI / 180.0f;
            int x1 = x0 + (int)(60 * cosf(a));
            int y1 = y0 + (int)(60 * sinf(a));
            line(buf, x0, y0, x1, y1, hsv((i * 15 + tick * 3) % 360, 200, 220));
        }
        line(buf, 100 - 70, 75, 100 + 70, 75, white); /* horizontal */
        line(buf, 100, 75 - 60, 100, 75 + 60, white); /* vertical */
    }

    /* rect: animated nested rectangles */
    {
        int x0 = 300, y0 = 75;
        for (int i = 0; i < 5; i++) {
            int r = 10 + i * 12;
            int phase = (tick / 2 + i * 30) % 360;
            rect(buf, x0 - r, y0 - r, x0 + r, y0 + r, hsv(phase, 200, 220));
        }
    }

    /* circle: concentric, pulsing radius */
    {
        int x0 = 500, y0 = 75;
        for (int i = 0; i < 5; i++) {
            int r = 10 + i * 13 + (int)(6 * sinf((tick + i * 20) * (float)M_PI / 60.0f));
            circle(buf, x0, y0, r, rainbow(i * 40));
        }
    }

    /* ellipse: rotating-aspect ellipses */
    {
        int x0 = 700, y0 = 75;
        for (int i = 0; i < 4; i++) {
            float t = (tick + i * 40) * (float)M_PI / 120.0f;
            int rx = 20 + (int)(30 * fabsf(cosf(t)));
            int ry = 20 + (int)(30 * fabsf(sinf(t)));
            ellipse(buf, x0, y0, rx, ry, rainbow(i * 60));
        }
    }

    /* ── row 1 ── */

    /* arc: animated sweep */
    {
        int x0 = 100, y0 = 225;
        fixed ang1 = itofix(tick % 256);
        fixed ang2 = itofix((tick + 96) % 256);
        arc(buf, x0, y0, ang1, ang2, 55, rainbow(0));
        arc(buf, x0, y0, ang2, itofix((fixtoi(ang2) + 80) % 256), 40, rainbow(120));
        circle(buf, x0, y0, 3, white);
    }

    /* spline: animated control points */
    {
        int x0 = 300, y0 = 225;
        float t = tick * (float)M_PI / 80.0f;
        int pts[8] = {
            x0 - 60, y0 + 40,
            x0 - 20 + (int)(30 * cosf(t)),     y0 - 50,
            x0 + 20 + (int)(30 * cosf(t + 2)), y0 + 50,
            x0 + 60, y0 - 40,
        };
        spline(buf, pts, rainbow(0));
        /* show control points */
        for (int i = 0; i < 4; i++)
            circle(buf, pts[i*2], pts[i*2+1], 3, makecol(180,180,60));
    }

    /* polygon: spinning hexagon */
    {
        int x0 = 500, y0 = 225;
        int pts[12];
        for (int i = 0; i < 6; i++) {
            float a = (i * 60 + tick) * (float)M_PI / 180.0f;
            pts[i*2]   = x0 + (int)(55 * cosf(a));
            pts[i*2+1] = y0 + (int)(55 * sinf(a));
        }
        polygon(buf, 6, pts, rainbow(0));
    }

    /* floodfill: draw a shape and fill it (re-draw each frame) */
    {
        int x0 = 700, y0 = 225;
        /* thick outline */
        for (int r = 38; r <= 42; r++)
            circle(buf, x0, y0, r, makecol(10, 10, 10)); /* black hole */
        circle(buf, x0, y0, 40, rainbow(0));
        floodfill(buf, x0, y0, rainbow(180));
    }

    /* ── row 2 ── */

    /* triangle: spinning */
    {
        int x0 = 100, y0 = 400;
        float a = tick * (float)M_PI / 90.0f;
        int x1 = x0 + (int)(55 * cosf(a)),           y1 = y0 + (int)(55 * sinf(a));
        int x2 = x0 + (int)(55 * cosf(a + 2.094f)),  y2 = y0 + (int)(55 * sinf(a + 2.094f));
        int x3 = x0 + (int)(55 * cosf(a + 4.189f)),  y3 = y0 + (int)(55 * sinf(a + 4.189f));
        triangle(buf, x1, y1, x2, y2, x3, y3, rainbow(0));
    }

    /* rectfill gradient: horizontal rainbow bars */
    {
        int x0 = 200, y0 = 310;
        for (int i = 0; i < 90; i++) {
            int c = hsv((i * 4 + tick * 2) % 360, 220, 220);
            vline(buf, x0 + i, y0, y0 + 115, c);
            vline(buf, x0 + i + 100, y0, y0 + 115, c); /* duplicate for width */
        }
    }

    /* circlefill: pulsing rings */
    {
        int x0 = 500, y0 = 400;
        for (int i = 4; i >= 0; i--) {
            int r = 10 + i * 13 + (int)(8 * sinf((tick + i * 15) * (float)M_PI / 60.0f));
            circlefill(buf, x0, y0, r, hsv((i * 50 + tick * 2) % 360, 200, 220));
        }
    }

    /* ellipsefill: orbiting ellipse */
    {
        int x0 = 700, y0 = 400;
        float a = tick * (float)M_PI / 90.0f;
        int cx = x0 + (int)(20 * cosf(a));
        int cy = y0 + (int)(20 * sinf(a));
        ellipsefill(buf, cx, cy, 40, 25, rainbow(0));
        ellipsefill(buf, cx, cy, 20, 10, rainbow(180));
    }

#undef CC
}

/* ════════════════════════════════════════════════════════════════════════════
 * PAGE 2: BITMAPS
 * ════════════════════════════════════════════════════════════════════════════ */
static void page_bitmaps(void)
{
    int white = makecol(220, 220, 220);
    int grey  = makecol(100, 100, 100);

    /* ── blit: straight copy ── */
    textout_ex(buf, font, "blit()", 10, 20, grey, -1);
    blit(bmp_src, buf, 0, 0, 10, 35, bmp_src->w, bmp_src->h);

    /* ── masked_blit ── */
    textout_ex(buf, font, "masked_blit()", 110, 20, grey, -1);
    for (int y = 35; y < 95; y += 10)
        for (int x = 110; x < 200; x += 10)
            rectfill(buf, x, y, x+9, y+9,
                     ((x/10 + y/10) & 1) ? makecol(80,80,80) : makecol(40,40,40));
    masked_blit(bmp_msrc, buf, 0, 0, 110, 35, bmp_msrc->w, bmp_msrc->h);

    /* ── stretch_blit ── */
    textout_ex(buf, font, "stretch_blit()", 220, 20, grey, -1);
    int sw = 60 + (int)(20 * sinf(tick * (float)M_PI / 60.0f));
    int sh = 45 + (int)(15 * sinf(tick * (float)M_PI / 45.0f));
    stretch_blit(bmp_src, buf, 0, 0, bmp_src->w, bmp_src->h, 220, 35, sw, sh);
    textprintf_ex(buf, font, 220, 100, grey, -1, "%dx%d", sw, sh);

    /* ── masked_stretch_blit ── */
    textout_ex(buf, font, "masked_stretch_blit()", 340, 20, grey, -1);
    for (int y = 35; y < 105; y += 10)
        for (int x = 340; x < 500; x += 10)
            rectfill(buf, x, y, x+9, y+9,
                     ((x/10 + y/10) & 1) ? makecol(80,80,80) : makecol(40,40,40));
    masked_stretch_blit(bmp_msrc, buf, 0, 0, bmp_msrc->w, bmp_msrc->h, 340, 35, 120, 70);

    /* ── sub-bitmap ── */
    textout_ex(buf, font, "create_sub_bitmap()", 500, 20, grey, -1);
    blit(bmp_full, buf, 0, 0, 500, 35, bmp_full->w, bmp_full->h);
    rect(buf, 500+20, 35+15, 500+20+60, 35+15+40, makecol(255,200,0));
    blit(bmp_sub, buf, 0, 0, 640, 35, bmp_sub->w, bmp_sub->h);
    textout_ex(buf, font, "sub", 650, 80, makecol(255,200,0), -1);

    /* ── clipping (animated — refill each frame) ── */
    textout_ex(buf, font, "set_clip_rect()", 10, 140, grey, -1);
    clear_to_color(bmp_clip, makecol(30, 20, 50));
    set_clip_rect(bmp_clip, 20, 15, 160, 85);
    for (int i = 0; i < 360; i += 30)
        line(bmp_clip, 90, 50,
             90 + (int)(100 * cosf(i * (float)M_PI / 180.0f + tick * (float)M_PI / 90.0f)),
             50 + (int)(100 * sinf(i * (float)M_PI / 180.0f + tick * (float)M_PI / 90.0f)),
             rainbow(i));
    circlefill(bmp_clip, 90, 50, 20, rainbow(180));
    rect(bmp_clip, 20, 15, 160, 85, makecol(255, 100, 100));
    set_clip_rect(bmp_clip, 0, 0, bmp_clip->w - 1, bmp_clip->h - 1);
    blit(bmp_clip, buf, 0, 0, 10, 155, bmp_clip->w, bmp_clip->h);
    textout_ex(buf, font, "clipped to red rect", 10, 260, grey, -1);

    /* ── acquire/release ── */
    textout_ex(buf, font, "acquire_bitmap() / release_bitmap()", 220, 140, white, -1);
    textout_ex(buf, font, "Always acquire before direct pixel access,", 220, 160, grey, -1);
    textout_ex(buf, font, "release after. Many drivers require this.", 220, 175, grey, -1);
    acquire_bitmap(buf);
    for (int i = 0; i < 120; i++) {
        int c = hsv((i * 3 + tick * 2) % 360, 200, 220);
        putpixel(buf, 220 + i, 200, c);
        putpixel(buf, 220 + i, 201, c);
        putpixel(buf, 220 + i, 202, c);
    }
    release_bitmap(buf);
    textout_ex(buf, font, "<- putpixel() inside acquire/release", 350, 198, grey, -1);
}

/* ════════════════════════════════════════════════════════════════════════════
 * PAGE 3: SPRITES
 * ════════════════════════════════════════════════════════════════════════════ */

/* Build an asymmetric arrow sprite so flips/rotations are obvious */
static BITMAP *make_arrow_sprite(void)
{
    BITMAP *s = create_bitmap(48, 48);
    if (!s)
        return NULL;

    clear_to_color(s, makecol(255, 0, 255)); /* mask colour */
    /* arrow pointing right */
    int pts[8] = { 4,16,  32,16,  32,4,  44,24 };  /* upper half */
    /* draw filled arrow manually */
    triangle(s, 4, 16, 32, 16, 4, 32, makecol(100, 180, 255));
    triangle(s, 32, 4, 44, 24, 32, 44, makecol(100, 180, 255));
    triangle(s, 32, 4, 32, 44, 4, 16, makecol(100, 180, 255));
    triangle(s, 32, 4, 32, 44, 4, 32, makecol(100, 180, 255));
    /* highlight tip */
    circlefill(s, 44, 24, 4, makecol(255, 255, 100));
    /* suppress unused-variable warning */
    (void)pts;
    return s;
}

static void page_sprites(void)
{
    int grey = makecol(100, 100, 100);
    BITMAP *spr = bmp_spr;

    if (!spr) {
        textout_centre_ex(buf, font, "Sprite bitmap allocation failed.", CX, CY, makecol(220, 220, 220), -1);
        return;
    }

    /* 3x4 grid of 8 sprite functions + 4 transform combos */
#define CELL_W 200
#define CELL_H 130
#define DRAW_SPR(col, row, label, draw_call) \
    do { \
        int gx = (col)*CELL_W + CELL_W/2; \
        int gy = (row)*CELL_H + CELL_H/2; \
        (void)gx; \
        (void)gy; \
        textout_ex(buf, font, label, (col)*CELL_W + 4, (row)*CELL_H + 4, grey, -1); \
        draw_call; \
    } while(0)

#if !DISABLE_ROTATION_DEMOS
    fixed ang = itofix(tick % 256);
#endif

    /* row 0 */
    DRAW_SPR(0, 0, "draw_sprite()",
        draw_sprite(buf, spr, gx - 24, gy - 24));

    DRAW_SPR(1, 0, "draw_sprite_h_flip()",
        draw_sprite_h_flip(buf, spr, gx - 24, gy - 24));

    DRAW_SPR(2, 0, "draw_sprite_v_flip()",
        draw_sprite_v_flip(buf, spr, gx - 24, gy - 24));

    DRAW_SPR(3, 0, "draw_sprite_vh_flip()",
        draw_sprite_vh_flip(buf, spr, gx - 24, gy - 24));

    /* row 1 */
    DRAW_SPR(0, 1, "rotate_sprite()",
#if DISABLE_ROTATION_DEMOS
        draw_unavailable_demo((0) * CELL_W + 20, (1) * CELL_H + 28, CELL_W - 40, CELL_H - 40, "rotate path crashes"));
#else
        rotate_sprite(buf, spr, gx - 24, gy - 24, ang));
#endif

#if !DISABLE_ROTATION_DEMOS
    fixed scale = ftofix(1.0f + 0.6f * sinf(tick * (float)M_PI / 60.0f));
#endif
    DRAW_SPR(1, 1, "rotate_scaled_sprite()",
#if DISABLE_ROTATION_DEMOS
        draw_unavailable_demo((1) * CELL_W + 20, (1) * CELL_H + 28, CELL_W - 40, CELL_H - 40, "rotate path crashes"));
#else
        rotate_scaled_sprite(buf, spr, gx - 24, gy - 24, ang, scale));
#endif

    DRAW_SPR(2, 1, "rotate_sprite_v_flip()",
#if DISABLE_ROTATION_DEMOS
        draw_unavailable_demo((2) * CELL_W + 20, (1) * CELL_H + 28, CELL_W - 40, CELL_H - 40, "rotate path crashes"));
#else
        rotate_sprite_v_flip(buf, spr, gx - 24, gy - 24, ang));
#endif

    /* pivot: rotate around the arrow tip (44,24) */
    DRAW_SPR(3, 1, "pivot_sprite()",
#if DISABLE_ROTATION_DEMOS
        draw_unavailable_demo((3) * CELL_W + 20, (1) * CELL_H + 28, CELL_W - 40, CELL_H - 40, "rotate path crashes"));
#else
        pivot_sprite(buf, spr, gx, gy, 44, 24, ang));
#endif

    /* row 2 */
    DRAW_SPR(0, 2, "stretch_sprite()",
        stretch_sprite(buf, spr,
            gx - 24 - (int)(12 * fabsf(sinf(tick*(float)M_PI/60))),
            gy - 24,
            48 + (int)(24 * fabsf(sinf(tick*(float)M_PI/60))), 48));

    int lit = (int)(128 + 127 * sinf(tick * (float)M_PI / 60.0f));
    set_trans_blender(255, 255, 255, lit);
    DRAW_SPR(1, 2, "draw_lit_sprite()",
        draw_lit_sprite(buf, spr, gx - 24, gy - 24, lit));

    set_trans_blender(0, 0, 0, (int)(128 + 100 * sinf(tick * (float)M_PI / 90.0f)));
    DRAW_SPR(2, 2, "draw_trans_sprite()",
    {
        /* visible background for trans test */
        circlefill(buf, gx, gy, 30, makecol(200, 100, 50));
        draw_trans_sprite(buf, spr, gx - 24, gy - 24);
    });
    solid_mode();

#if !DISABLE_ROTATION_DEMOS
    fixed pscale = ftofix(1.2f + 0.5f * sinf(tick * (float)M_PI / 80.0f));
#endif
    DRAW_SPR(3, 2, "pivot_scaled_sprite()",
#if DISABLE_ROTATION_DEMOS
        draw_unavailable_demo((3) * CELL_W + 20, (2) * CELL_H + 28, CELL_W - 40, CELL_H - 40, "rotate path crashes"));
#else
        pivot_scaled_sprite(buf, spr, gx, gy, 44, 24, ang, pscale));
#endif

#undef CELL_W
#undef CELL_H
#undef DRAW_SPR
}

/* ════════════════════════════════════════════════════════════════════════════
 * PAGE 4: BLENDING MODES
 * ════════════════════════════════════════════════════════════════════════════ */
static void page_blending(void)
{
    int grey  = makecol(100, 100, 100);
    int white = makecol(220, 220, 220);

    /* background: gradient diagonal stripes */
    for (int y = 0; y < CH; y++)
        hline(buf, 0, y, W - 1, hsv((y + tick) % 360, 160, 80));

    /* blender configurations: name, setup call, draw call */
    struct {
        const char *name;
        int a;  /* alpha for blenders that take it */
    } modes[] = {
        { "set_trans_blender",       160 },
        { "set_add_blender",         160 },
        { "set_burn_blender",        200 },
        { "set_dodge_blender",       200 },
        { "set_multiply_blender",    200 },
        { "set_screen_blender",      200 },
        { "set_difference_blender",  200 },
        { "set_invert_blender",        0 },
        { "set_dissolve_blender",    160 },
    };

    int cols = 3, rows = 3;
    int cw = W / cols, rh = CH / rows;

    for (int i = 0; i < 9; i++) {
        int col = i % cols, row = i / cols;
        int x0 = col * cw + 4, y0 = row * rh + 16;
        int cx = col * cw + cw / 2, cy = row * rh + rh / 2;

        textout_ex(buf, font, modes[i].name, x0, row * rh + 2, grey, -1);

        /* fill animated content into pre-allocated bitmap */
        BITMAP *src = bmp_blend[i];
        clear_to_color(src, makecol(0, 0, 0)); /* mask colour */
        for (int j = 0; j < 12; j++) {
            float a = j * (float)M_PI / 6.0f + tick * (float)M_PI / 120.0f;
            int sx = src->w / 2 + (int)(30 * cosf(a)) - 18;
            int sy = src->h / 2 + (int)(30 * sinf(a)) - 18;
            rectfill(src, sx, sy, sx + 36, sy + 36,
                     hsv((j * 30 + tick * 2) % 360, 220, 220));
        }

        /* select blender */
        int a = modes[i].a;
        switch (i) {
            case 0: set_trans_blender(0,0,0,a);      break;
            case 1: set_add_blender(0,0,0,a);        break;
            case 2: set_burn_blender(0,0,0,a);       break;
            case 3: set_dodge_blender(0,0,0,a);      break;
            case 4: set_multiply_blender(0,0,0,a);   break;
            case 5: set_screen_blender(0,0,0,a);     break;
            case 6: set_difference_blender(0,0,0,a); break;
            case 7: set_invert_blender(0,0,0,0);     break;
            case 8: set_dissolve_blender(0,0,0,a);   break;
        }
        draw_trans_sprite(buf, src, x0, y0);
        solid_mode();

        /* also show XOR mode for the invert cell */
        if (i == 7) {
            xor_mode(TRUE);
            circle(buf, cx, cy, 30, white);
            rect(buf, cx - 20, cy - 20, cx + 20, cy + 20, makecol(255, 0, 0));
            xor_mode(FALSE);
        }
    }
}

/* ════════════════════════════════════════════════════════════════════════════
 * PAGE 5: TEXT
 * ════════════════════════════════════════════════════════════════════════════ */
static void page_text(void)
{
    int bg    = -1;   /* transparent background */
    int white = makecol(220, 220, 220);
    int grey  = makecol(100, 100, 100);
    int col   = rainbow(0);

    /* ── textout_ex ── */
    textout_ex(buf, font, "textout_ex()  — left-aligned", 10, 20, white, bg);

    /* ── textout_centre_ex ── */
    textout_centre_ex(buf, font, "textout_centre_ex()  — centred at x=400",
                      CX, 40, makecol(180, 220, 255), bg);

    /* ── textout_right_ex ── */
    textout_right_ex(buf, font, "textout_right_ex()  — right-aligned at x=790",
                     W - 10, 60, makecol(255, 180, 180), bg);

    /* ── textprintf_ex ── */
    textprintf_ex(buf, font, 10, 80, col, bg,
                  "textprintf_ex()  tick=%d  fps=%d", tick, fps_value);

    /* ── opaque background ── */
    textout_ex(buf, font, "textout_ex() with opaque background",
               10, 100, makecol(255, 255, 100), makecol(40, 40, 80));

    /* ── metrics ── */
    int tw = text_length(font, "METRICS");
    int th = text_height(font);
    textprintf_ex(buf, font, 10, 130, grey, bg,
                  "text_length(\"METRICS\") = %d px   text_height() = %d px",
                  tw, th);
    textout_ex(buf, font, "METRICS", 10, 148, white, bg);
    /* show width bracket */
    hline(buf, 10, 165, 10 + tw, makecol(255, 200, 0));
    vline(buf, 10,      162, 168, makecol(255, 200, 0));
    vline(buf, 10 + tw, 162, 168, makecol(255, 200, 0));

    /* ── justify ── */
    textout_justify_ex(buf, font,
                       "textout_justify_ex()  stretches  words  across  a  fixed  width",
                       10, W - 20, 185, 4, white, bg);

    /* ── colour cycling ── */
    textout_ex(buf, font, "Rainbow colour cycling:", 10, 210, grey, bg);
    const char *msg = "Allegro 4  |  arm64  |  macOS";
    int mw = text_length(font, msg);
    for (int c2 = 0; c2 < mw; c2++) {
        /* one-char-wide clip window — simple per-pixel colour trick */
        set_clip_rect(buf, 10 + c2, 228, 10 + c2 + 7, 228 + th - 1);
        textout_ex(buf, font, msg, 10, 228, hsv((c2 * 6 + tick * 3) % 360, 220, 220), bg);
    }
    set_clip_rect(buf, 0, 0, W - 1, CH - 1);

    /* ── ustring (UTF-8 characters) ── */
    textout_ex(buf, font, "ustrlen / ustrcpy / ustr* API also available", 10, 255, grey, bg);

    /* ── large text via stretch_blit trick ── */
    textout_ex(buf, font, "LARGE TEXT via stretch_blit", 10, 275, grey, bg);
    {
        const char *big = "BIG!";
        int bw = text_length(font, big), bh = text_height(font);
        BITMAP *tmp = create_bitmap(bw, bh);
        clear_to_color(tmp, makecol(0, 0, 0));
        textout_ex(tmp, font, big, 0, 0, rainbow(0), -1);
        stretch_blit(tmp, buf, 0, 0, bw, bh, 10, 290, bw * 4, bh * 4);
        destroy_bitmap(tmp);
    }

    /* ── alignment summary table ── */
    int ty = 380;
    textout_ex(buf, font, "Summary of alignment variants:", 10, ty, grey, bg);
    ty += 15;
    rect(buf, 9, ty - 1, W - 10, ty + 50, grey);
    textout_ex      (buf, font, "LEFT",    10, ty + 5, white, bg);
    textout_centre_ex(buf, font, "CENTRE", CX, ty + 5, white, bg);
    textout_right_ex (buf, font, "RIGHT",  W - 10, ty + 5, white, bg);
    /* second row with coloured bg */
    rectfill(buf, 10, ty + 22, W - 11, ty + 37, makecol(30, 30, 60));
    textout_ex      (buf, font, "left + bg",    10,    ty + 24, makecol(255,220,100), makecol(30,30,60));
    textout_centre_ex(buf, font, "centre + bg", CX,    ty + 24, makecol(100,255,180), makecol(30,30,60));
    textout_right_ex (buf, font, "right + bg",  W-10,  ty + 24, makecol(255,130,130), makecol(30,30,60));

    /* ── scancode_to_name demo ── */
    ty += 60;
    textprintf_ex(buf, font, 10, ty, grey, bg,
                  "scancode_to_name(KEY_SPACE) = \"%s\"  "
                  "scancode_to_ascii(KEY_A) = '%c'",
                  scancode_to_name(KEY_SPACE),
                  scancode_to_ascii(KEY_A));
}

/* ════════════════════════════════════════════════════════════════════════════
 * PAGE 6: KEYBOARD
 * ════════════════════════════════════════════════════════════════════════════ */
static void page_keyboard(void)
{
    int grey  = makecol(100, 100, 100);
    int white = makecol(220, 220, 220);

    textout_ex(buf, font, "Live key[] state — press keys to light them up",
               10, 6, grey, -1);

    /* QWERTY layout: row / key-name / scancode */
    struct { const char *label; int sc; } rows[4][14] = {
        {
            {"ESC",KEY_ESC}, {"F1",KEY_F1}, {"F2",KEY_F2}, {"F3",KEY_F3},
            {"F4",KEY_F4}, {"F5",KEY_F5}, {"F6",KEY_F6}, {"F7",KEY_F7},
            {"F8",KEY_F8}, {"F9",KEY_F9}, {"F10",KEY_F10}, {"F11",KEY_F11},
            {"F12",KEY_F12}, {0,0}
        },
        {
            {"`",KEY_TILDE}, {"1",KEY_1}, {"2",KEY_2}, {"3",KEY_3},
            {"4",KEY_4}, {"5",KEY_5}, {"6",KEY_6}, {"7",KEY_7},
            {"8",KEY_8}, {"9",KEY_9}, {"0",KEY_0}, {"-",KEY_MINUS},
            {"=",KEY_EQUALS}, {0,0}
        },
        {
            {"Q",KEY_Q}, {"W",KEY_W}, {"E",KEY_E}, {"R",KEY_R},
            {"T",KEY_T}, {"Y",KEY_Y}, {"U",KEY_U}, {"I",KEY_I},
            {"O",KEY_O}, {"P",KEY_P}, {"[",KEY_OPENBRACE},
            {"]",KEY_CLOSEBRACE}, {"\\",KEY_BACKSLASH}, {0,0}
        },
        {
            {"A",KEY_A}, {"S",KEY_S}, {"D",KEY_D}, {"F",KEY_F},
            {"G",KEY_G}, {"H",KEY_H}, {"J",KEY_J}, {"K",KEY_K},
            {"L",KEY_L}, {";",KEY_SEMICOLON}, {"'",KEY_QUOTE},
            {"RET",KEY_ENTER}, {0,0}, {0,0}
        },
    };

    int kw = 54, kh = 30, xoff = 10, yoff = 30;
    for (int r = 0; r < 4; r++) {
        for (int c = 0; c < 14; c++) {
            if (!rows[r][c].label) break;
            int x = xoff + c * (kw + 4) + r * 12;
            int y = yoff + r * (kh + 6);
            int pressed = key[rows[r][c].sc];
            int bg  = pressed ? makecol(80, 180, 80) : makecol(40, 40, 50);
            int fg  = pressed ? makecol(20, 20, 20)  : grey;
            rectfill(buf, x, y, x + kw - 1, y + kh - 1, bg);
            rect    (buf, x, y, x + kw - 1, y + kh - 1, makecol(80, 80, 100));
            textout_centre_ex(buf, font, rows[r][c].label,
                              x + kw / 2, y + (kh - text_height(font)) / 2, fg, -1);
        }
    }

    /* bottom row: SPACE, arrows, modifiers */
    struct { const char *label; int sc; int w; } extras[] = {
        {"SHIFT",KEY_LSHIFT,  70}, {"Z",KEY_Z, 54}, {"X",KEY_X, 54},
        {"C",KEY_C, 54}, {"V",KEY_V, 54}, {"B",KEY_B, 54},
        {"N",KEY_N, 54}, {"M",KEY_M, 54}, {",",KEY_COMMA, 54},
        {".",KEY_STOP, 54}, {"/",KEY_SLASH, 54}, {0,0,0}
    };
    int x = xoff + 24, y = yoff + 4 * (kh + 6);
    for (int i = 0; extras[i].label; i++) {
        int pressed = key[extras[i].sc];
        int bg = pressed ? makecol(80,180,80) : makecol(40,40,50);
        int fg = pressed ? makecol(20,20,20)  : grey;
        rectfill(buf, x, y, x + extras[i].w - 1, y + kh - 1, bg);
        rect    (buf, x, y, x + extras[i].w - 1, y + kh - 1, makecol(80,80,100));
        textout_centre_ex(buf, font, extras[i].label,
                          x + extras[i].w / 2, y + (kh - text_height(font)) / 2, fg, -1);
        x += extras[i].w + 4;
    }
    /* space bar */
    {
        int pressed = key[KEY_SPACE];
        int bg = pressed ? makecol(80,180,80) : makecol(40,40,50);
        rectfill(buf, xoff + 24 + 68, y, xoff + 24 + 68 + 220, y + kh - 1, bg);
        rect    (buf, xoff + 24 + 68, y, xoff + 24 + 68 + 220, y + kh - 1, makecol(80,80,100));
        textout_centre_ex(buf, font, "SPACE",
                          xoff + 24 + 68 + 110, y + (kh - text_height(font)) / 2, grey, -1);
    }

    /* key_shifts bitmask */
    int ky = yoff + 6 * (kh + 6) + 10;
    textprintf_ex(buf, font, 10, ky, grey, -1,
                  "key_shifts = 0x%04X", key_shifts);
    struct { const char *name; int bit; } shifts[] = {
        {"KB_SHIFT",  KB_SHIFT_FLAG},   {"KB_CTRL",  KB_CTRL_FLAG},
        {"KB_ALT",    KB_ALT_FLAG},     {"KB_LWIN",  KB_LWIN_FLAG},
        {"KB_RWIN",   KB_RWIN_FLAG},    {"KB_MENU",  KB_MENU_FLAG},
        {"KB_COMMAND",KB_COMMAND_FLAG}, {"KB_SCROLOCK", KB_SCROLOCK_FLAG},
        {"KB_NUMLOCK",KB_NUMLOCK_FLAG}, {"KB_CAPSLOCK", KB_CAPSLOCK_FLAG},
    };
    for (int i = 0; i < 10; i++) {
        int active = key_shifts & shifts[i].bit;
        textout_ex(buf, font, shifts[i].name,
                   10 + (i % 5) * 150, ky + 18 + (i / 5) * 16,
                   active ? makecol(100,255,100) : makecol(60,60,60), -1);
    }

    /* keypressed() */
    ky += 60;
    textprintf_ex(buf, font, 10, ky, white, -1,
                  "keypressed() = %s", keypressed() ? "TRUE" : "false");
    ky += 14;
    textout_ex(buf, font,
               "(keypressed() is non-zero when there is a key waiting in the buffer)",
               10, ky, grey, -1);
}

/* ════════════════════════════════════════════════════════════════════════════
 * PAGE 7: MOUSE
 * ════════════════════════════════════════════════════════════════════════════ */
#define TRAIL_LEN 64
static int trail_x[TRAIL_LEN], trail_y[TRAIL_LEN];
static int trail_head = 0;

static void page_mouse(void)
{
    int grey  = makecol(100, 100, 100);
    int white = makecol(220, 220, 220);

    if (mouse_needs_poll()) poll_mouse();

    /* update trail */
    trail_x[trail_head] = mouse_x;
    trail_y[trail_head] = mouse_y;
    trail_head = (trail_head + 1) % TRAIL_LEN;

    /* draw trail */
    for (int i = 0; i < TRAIL_LEN; i++) {
        int idx = (trail_head + i) % TRAIL_LEN;
        if (trail_x[idx] == 0 && trail_y[idx] == 0) continue;
        int a = i * 255 / TRAIL_LEN;
        int c = makecol(a, a / 2, 255 - a);
        putpixel(buf, trail_x[idx], trail_y[idx], c);
        if (i > 1) {
            int prev = (trail_head + i - 1) % TRAIL_LEN;
            line(buf, trail_x[prev], trail_y[prev], trail_x[idx], trail_y[idx], c);
        }
    }

    /* draw cursor crosshair */
    int mx = mouse_x, my = mouse_y;
    if (mx >= 0 && mx < W && my >= 0 && my < CH) {
        line(buf, mx - 12, my, mx - 4, my, white);
        line(buf, mx + 4,  my, mx + 12, my, white);
        line(buf, mx, my - 12, mx, my - 4, white);
        line(buf, mx, my + 4,  mx, my + 12, white);
        circle(buf, mx, my, 8, white);
    }

    /* info panel */
    int px = 10, py = 20;
    textprintf_ex(buf, font, px, py,      white, -1, "mouse_x     = %d", mouse_x);
    textprintf_ex(buf, font, px, py + 14, white, -1, "mouse_y     = %d", mouse_y);
    textprintf_ex(buf, font, px, py + 28, white, -1, "mouse_z     = %d  (scroll)", mouse_z);
    textprintf_ex(buf, font, px, py + 42, white, -1, "mouse_w     = %d  (hscroll)", mouse_w);
    textprintf_ex(buf, font, px, py + 56, white, -1, "mouse_b     = 0x%02X", mouse_b);

    /* button indicators */
    struct { const char *name; int bit; } btns[] = {
        {"LMB", 1}, {"RMB", 2}, {"MMB", 4}
    };
    for (int i = 0; i < 3; i++) {
        int active = mouse_b & btns[i].bit;
        int bx = px + i * 80;
        int by = py + 75;
        rectfill(buf, bx, by, bx + 60, by + 30,
                 active ? makecol(80, 200, 80) : makecol(40, 40, 50));
        rect(buf, bx, by, bx + 60, by + 30, grey);
        textout_centre_ex(buf, font, btns[i].name, bx + 30, by + 9,
                          active ? makecol(20,20,20) : grey, -1);
    }

    /* mickeys (relative delta) */
    int dmx, dmy;
    get_mouse_mickeys(&dmx, &dmy);
    textprintf_ex(buf, font, px, py + 120, grey, -1,
                  "get_mouse_mickeys() = (%+d, %+d)", dmx, dmy);

    /* mouse_on_screen */
    textprintf_ex(buf, font, px, py + 136, grey, -1,
                  "mouse_on_screen() = %s", mouse_on_screen() ? "YES" : "no");

    /* position */
    textprintf_ex(buf, font, px, py + 152, grey, -1,
                  "mouse_pos = 0x%08X  (packed x<<16|y)", mouse_pos);

    textout_ex(buf, font, "Move mouse, scroll wheel, click buttons.",
               10, CH - 30, grey, -1);
}

/* ════════════════════════════════════════════════════════════════════════════
 * PAGE 8: TIMER & FPS
 * ════════════════════════════════════════════════════════════════════════════ */
static volatile long retrace_snapshot = 0;

static void page_timer(void)
{
    int grey  = makecol(100, 100, 100);
    int white = makecol(220, 220, 220);
    int green = makecol(100, 220, 100);

    /* ── FPS ── */
    textout_ex(buf, font, "FPS & timing", 10, 10, grey, -1);
    textprintf_ex(buf, font, 10, 28, green, -1,
                  "fps_value (measured via install_int @1 Hz) = %d fps", fps_value);
    textprintf_ex(buf, font, 10, 46, grey, -1,
                  "tick counter (incremented each frame)       = %d", tick);

    /* ── retrace_count ── */
    retrace_snapshot = retrace_count;
    textprintf_ex(buf, font, 10, 72, grey, -1,
                  "retrace_count (Allegro vsync counter)       = %ld", retrace_snapshot);

    /* ── install_int resolution ── */
    textprintf_ex(buf, font, 10, 98, grey, -1,
                  "TIMERS_PER_SECOND = %ld", TIMERS_PER_SECOND);
    textprintf_ex(buf, font, 10, 114, grey, -1,
                  "install_int(cb, 1000) fires every ~1 second");
    textprintf_ex(buf, font, 10, 130, grey, -1,
                  "install_int_ex(cb, BPS_TO_TIMER(60)) fires ~60x/sec");

    /* ── animated FPS bar ── */
    {
        int bw = (int)((float)fps_value / 120.0f * (W - 20));
        if (bw > W - 20) bw = W - 20;
        int col = fps_value >= 55 ? makecol(60, 200, 60) :
                  fps_value >= 30 ? makecol(200, 200, 60) :
                                    makecol(200, 60, 60);
        textout_ex(buf, font, "FPS bar (target 60):", 10, 158, grey, -1);
        rectfill(buf, 10, 174, 10 + bw, 190, col);
        rect    (buf, 10, 174, W - 10,  190, grey);
        /* target line at 60fps */
        int tx = 10 + (int)(60.0f / 120.0f * (W - 20));
        vline(buf, tx, 170, 194, white);
        textout_ex(buf, font, "60", tx - 8, 196, white, -1);
    }

    /* ── busy-loop cost demo ── */
    textout_ex(buf, font, "Timing accuracy test:", 10, 215, grey, -1);
    {
        /* draw a sine wave of "frame time" variance using tick */
        int gx = 10, gy = 250, gw = W - 20, gh = 80;
        rect(buf, gx, gy, gx + gw, gy + gh, grey);
        textout_ex(buf, font, "Frame time variance (simulated sine)",
                   gx, gy - 12, grey, -1);
        for (int i = 1; i < gw; i++) {
            float v  = 0.5f + 0.4f * sinf((tick + i) * (float)M_PI / 30.0f);
            float v0 = 0.5f + 0.4f * sinf((tick + i - 1) * (float)M_PI / 30.0f);
            int y1 = gy + gh - (int)(v  * gh);
            int y0 = gy + gh - (int)(v0 * gh);
            line(buf, gx + i - 1, y0, gx + i, y1, green);
        }
    }

    /* ── vsync() note ── */
    textout_ex(buf, font,
               "vsync() called each frame — synchronises to vertical retrace.",
               10, 345, grey, -1);
    textout_ex(buf, font,
               "On this platform it blocks until the display refreshes.",
               10, 361, grey, -1);

    /* ── system/driver info ── */
    textprintf_ex(buf, font, 10, 395, grey, -1,
                  "system driver   : %s", system_driver->ascii_name);
    textprintf_ex(buf, font, 10, 411, grey, -1,
                  "timer  driver   : %s", timer_driver ? timer_driver->ascii_name : "none");
    textprintf_ex(buf, font, 10, 427, grey, -1,
                  "Allegro version : " ALLEGRO_VERSION_STR);
    textprintf_ex(buf, font, 10, 443, grey, -1,
                  "Color depth     : %d bpp", bitmap_color_depth(screen));
    textprintf_ex(buf, font, 10, 459, grey, -1,
                  "Screen size     : %d x %d", SCREEN_W, SCREEN_H);
}

/* ════════════════════════════════════════════════════════════════════════════
 * PAGE 9: IMAGES
 * ════════════════════════════════════════════════════════════════════════════ */
static void page_images(void)
{
    int grey  = makecol(100, 100, 100);
    int white = makecol(220, 220, 220);
    BITMAP *first = NULL;

    if (frames_loaded == 0) {
        textout_centre_ex(buf, font, "No images loaded.", CX, CY - 8, white, -1);
        textprintf_ex(buf, font, CX - 120, CY + 8, grey, -1,
                      "Put BMPs in " ASSETS_DIR "/");
        return;
    }

    first = frames[0];
    if (!first) {
        textout_centre_ex(buf, font, "Image metadata is inconsistent.", CX, CY, white, -1);
        return;
    }

    int fw = first->w;   /* 82 */
    int fh = first->h;   /* 82 */
    int cur = (tick / 6) % frames_loaded;   /* ~10fps animation */

    /* ── top strip: all frames ────────────────────────────────────────────── */
    int slot = W / frames_loaded;   /* 80px each */
    for (int i = 0; i < frames_loaded; i++) {
        if (!frames[i]) continue;
        int x = i * slot + (slot - fw) / 2;
        blit(frames[i], buf, 0, 0, x, 2, fw, fh);
        /* highlight current frame */
        if (i == cur)
            rect(buf, x - 1, 1, x + fw, fh + 2, makecol(255, 200, 0));
        textprintf_ex(buf, font, i * slot + slot/2 - 3, fh + 6,
                      i == cur ? makecol(255,200,0) : grey, -1, "%d", i);
    }

    /* divider */
    hline(buf, 0, fh + 18, W - 1, makecol(50, 50, 50));

    int y0 = fh + 26;   /* content starts below the strip */

    /* ── left column: blit vs draw_sprite ────────────────────────────────── */
    {
        int x = 10;
        textout_ex(buf, font, "blit()  (raw)", x, y0, grey, -1);
        /* checkerboard background to see if transparency is working */
        for (int cy = y0+14; cy < y0+14+fh; cy += 10)
            for (int cx = x; cx < x + fw; cx += 10)
                rectfill(buf, cx, cy, cx+9, cy+9,
                         ((cx/10+cy/10)&1) ? makecol(70,70,70) : makecol(40,40,40));
        blit(frames[cur], buf, 0, 0, x, y0 + 14, fw, fh);

        textout_ex(buf, font, "draw_sprite()  (masked)", x + fw + 10, y0, grey, -1);
        for (int cy = y0+14; cy < y0+14+fh; cy += 10)
            for (int cx = x + fw + 10; cx < x + fw*2 + 10; cx += 10)
                rectfill(buf, cx, cy, cx+9, cy+9,
                         ((cx/10+cy/10)&1) ? makecol(70,70,70) : makecol(40,40,40));
        draw_sprite(buf, frames[cur], x + fw + 10, y0 + 14);

        textprintf_ex(buf, font, x, y0 + 14 + fh + 4, grey, -1,
                      "mask colour: 0x%06X  (makecol 255,0,255 = 0x%06X)",
                      bitmap_mask_color(first),
                      makecol(255, 0, 255));
    }

    /* ── centre: large animated sprite ───────────────────────────────────── */
    {
        int scale = 3;
        int x = CX - (fw * scale) / 2;
        int y = y0 + 14;
        textout_centre_ex(buf, font, "stretch_blit() x3", CX, y0, grey, -1);
        for (int cy = y; cy < y + fh*scale; cy += 10)
            for (int cx = x; cx < x + fw*scale; cx += 10)
                rectfill(buf, cx, cy, cx+9, cy+9,
                         ((cx/10+cy/10)&1) ? makecol(70,70,70) : makecol(40,40,40));
        stretch_blit(frames[cur], buf, 0, 0, fw, fh,
                     x, y, fw * scale, fh * scale);
    }

    /* ── right column: transforms ────────────────────────────────────────── */
    {
        int x = W - fw - 20;
        int yy = y0;

        /* rotate_sprite */
        textout_ex(buf, font, "rotate_sprite()", x - 30, yy, grey, -1);
        yy += 14;
        circlefill(buf, x + fw/2, yy + fh/2, fw/2 + 4, makecol(30, 30, 50));
#if DISABLE_ROTATION_DEMOS
        draw_unavailable_demo(x - 8, yy - 4, fw + 16, fh + 8, "rotate path crashes");
#else
        rotate_sprite(buf, frames[cur], x, yy, itofix(tick % 256));
#endif
        yy += fh + 8;

        /* draw_lit_sprite */
        int lit = (int)(128 + 127 * sinf(tick * (float)M_PI / 60.0f));
        textprintf_ex(buf, font, x - 30, yy, grey, -1, "draw_lit_sprite() lit=%d", lit);
        yy += 14;
        set_trans_blender(255, 255, 255, lit);
        draw_lit_sprite(buf, frames[cur], x, yy, lit);
        yy += fh + 8;

        /* draw_trans_sprite */
        int alpha = (int)(128 + 100 * sinf(tick * (float)M_PI / 90.0f));
        textprintf_ex(buf, font, x - 30, yy, grey, -1, "draw_trans_sprite() a=%d", alpha);
        yy += 14;
        /* coloured background so transparency is visible */
        rectfill(buf, x, yy, x + fw - 1, yy + fh - 1, makecol(200, 100, 50));
        set_trans_blender(0, 0, 0, alpha);
        draw_trans_sprite(buf, frames[cur], x, yy);
        solid_mode();
    }

    /* ── bottom: image info ───────────────────────────────────────────────── */
    int yinfo = CH - 32;
    textprintf_ex(buf, font, 10, yinfo, grey, -1,
                  "Loaded: %d/%d frames   Size: %dx%d   Depth: %d bpp   "
                  "Frame: %d   Source: " ASSETS_DIR "/",
                  frames_loaded, NUM_FRAMES, fw, fh,
                  bitmap_color_depth(first), cur);
}

/* ════════════════════════════════════════════════════════════════════════════
 * BITMAP LIFECYCLE — call init after set_gfx_mode, free before exit
 * ════════════════════════════════════════════════════════════════════════════ */
static void init_bitmaps(void)
{
    int cw = W / 3, rh = CH / 3;

    /* page 2: static-content bitmaps drawn once */
    bmp_src = create_bitmap(80, 60);
    clear_to_color(bmp_src, makecol(0, 0, 80));
    for (int y = 0; y < 60; y += 10) hline(bmp_src, 0, y, 79, makecol(0, 0, 180));
    for (int x = 0; x < 80; x += 10) vline(bmp_src, x, 0, 59, makecol(0, 0, 180));
    textout_ex(bmp_src, font, "SRC", 28, 24, makecol(255, 255, 100), -1);

    bmp_msrc = create_bitmap(80, 60);
    clear_to_color(bmp_msrc, makecol(0, 0, 0));
    circlefill(bmp_msrc, 40, 30, 25, makecol(200, 80, 80));
    textout_ex(bmp_msrc, font, "MSK", 24, 24, makecol(255, 255, 100), -1);

    bmp_full = create_bitmap(120, 80);
    clear_to_color(bmp_full, makecol(20, 60, 20));
    for (int i = 0; i < 120; i += 8) line(bmp_full, i, 0, i, 79, makecol(40, 120, 40));
    for (int i = 0; i < 80;  i += 8) line(bmp_full, 0, i, 119, i, makecol(40, 120, 40));
    /* sub must be freed before full — never swap this order */
    bmp_sub = create_sub_bitmap(bmp_full, 20, 15, 60, 40);

    /* page 2: animated bitmaps — pre-allocate, fill each frame */
    bmp_clip = create_bitmap(180, 100);

    /* page 3: arrow sprite */
    bmp_spr = make_arrow_sprite();

    /* page 4: blending sources — pre-allocate, fill each frame */
    for (int i = 0; i < 9; i++)
        bmp_blend[i] = create_bitmap(cw - 8, rh - 20);

    /* page 9: load animation frames */
    frames_loaded = 0;
    for (int i = 0; i < NUM_FRAMES; i++)
        frames[i] = NULL;

    for (int i = 0; i < NUM_FRAMES; i++) {
        BITMAP *frame = load_frame_bitmap(i);
        if (frame)
            frames[frames_loaded++] = frame;
    }
}

static void free_bitmaps(void)
{
    /* sub-bitmap must be freed before its parent */
    destroy_bitmap(bmp_sub);
    destroy_bitmap(bmp_full);
    destroy_bitmap(bmp_src);
    destroy_bitmap(bmp_msrc);
    destroy_bitmap(bmp_clip);
    destroy_bitmap(bmp_spr);
    for (int i = 0; i < 9; i++) destroy_bitmap(bmp_blend[i]);
    for (int i = 0; i < NUM_FRAMES; i++)
        if (frames[i]) destroy_bitmap(frames[i]);
}

/* ════════════════════════════════════════════════════════════════════════════
 * PAGE 10: SOUND
 * ════════════════════════════════════════════════════════════════════════════ */
static void page_sound(void)
{
    static int prev_space = 0;

    int white  = makecol(220, 220, 220);
    int grey   = makecol(120, 120, 120);
    int dim    = makecol( 35,  35,  35);
    int green  = makecol( 80, 220,  80);
    int red    = makecol(220,  70,  70);
    int yellow = makecol(220, 200,  60);

    /* ── SPACE: toggle play/stop ─────────────────────────────────────────── */
    int cur_space = key[KEY_SPACE];
    if (cur_space && !prev_space) {
        if (snd_playing) {
            kill(snd_pid, SIGTERM);
            waitpid(snd_pid, NULL, WNOHANG);
            snd_pid = -1;
            snd_playing = 0;
        } else {
            snd_pid = fork();
            if (snd_pid == 0) {
                /* child: exec afplay and never return */
                execlp("afplay", "afplay", MP3_FILE, NULL);
                _exit(1);
            }
            if (snd_pid > 0)
                snd_playing = 1;
        }
    }
    prev_space = cur_space;

    /* check if afplay finished on its own */
    if (snd_playing && snd_pid > 0 &&
        waitpid(snd_pid, NULL, WNOHANG) == snd_pid) {
        snd_pid = -1;
        snd_playing = 0;
    }

    /* ── allegro audio driver status ─────────────────────────────────────── */
    textout_ex(buf, font, "Allegro audio driver:", 10, 10, grey, -1);
    textout_ex(buf, font, snd_result == 0 ? "OK" : "unavailable",
               190, 10, snd_result == 0 ? green : red, -1);

    /* ── track info ──────────────────────────────────────────────────────── */
    textout_ex(buf, font, "Track:", 10, 28, grey, -1);
    textout_ex(buf, font, "Flo Rida - My House", 60, 28, white, -1);
    textout_ex(buf, font, "(played via afplay)", 60, 44, grey, -1);

    /* ── playback status ─────────────────────────────────────────────────── */
    int status_col = snd_playing ? green : grey;
    textout_centre_ex(buf, font, snd_playing ? ">> PLAYING" : "[] STOPPED",
                      CX, 70, status_col, -1);

    /* ── fake visualizer ─────────────────────────────────────────────────── */
    #define NUM_BARS 40
    int bar_w     = 14;
    int gap       = 5;
    int total_w   = NUM_BARS * (bar_w + gap) - gap;
    int bar_x0    = (W - total_w) / 2;
    int bar_max_h = 200;
    int bar_bot   = 340;

    for (int i = 0; i < NUM_BARS; i++) {
        double t = tick * 0.04;
        double h;
        if (snd_playing) {
            h = 0.45 + 0.30 * sin(t * 2.1 + i * 0.38)
                     + 0.25 * sin(t * 3.7 + i * 0.61)
                     + 0.15 * sin(t * 1.3 + i * 0.95)
                     + 0.10 * sin(t * 5.0 - i * 0.20);
            h = h / 1.25;
            if (h < 0.04) h = 0.04;
            if (h > 1.00) h = 1.00;
        } else {
            h = 0.015;
        }

        int bar_h = (int)(h * bar_max_h);
        if (bar_h < 2) bar_h = 2;

        int x = bar_x0 + i * (bar_w + gap);

        /* colour: green → yellow → red with height */
        int r = h < 0.5 ? (int)(h * 2 * 200) : 200;
        int g = h < 0.5 ? 200 : (int)((1.0 - h) * 2 * 200);
        if (r < 0) r = 0; if (r > 255) r = 255;
        if (g < 0) g = 0; if (g > 255) g = 255;
        int bar_col = makecol(r, g, 40);

        rectfill(buf, x, bar_bot - bar_max_h, x + bar_w - 1,
                 bar_bot - bar_h - 1, dim);
        rectfill(buf, x, bar_bot - bar_h, x + bar_w - 1, bar_bot, bar_col);
    }
    #undef NUM_BARS

    /* ── instructions ────────────────────────────────────────────────────── */
    textout_centre_ex(buf, font, "SPACE  —  play / stop",
                      CX, bar_bot + 16, yellow, -1);
}

/* ════════════════════════════════════════════════════════════════════════════
 * MAIN
 * ════════════════════════════════════════════════════════════════════════════ */
int main(void)
{
    allegro_init();
    install_timer();
    install_keyboard();
    install_mouse();
    snd_result = install_sound(DIGI_AUTODETECT, MIDI_NONE, NULL);

    LOCK_VARIABLE(fps_raw);
    LOCK_VARIABLE(fps_value);
    LOCK_FUNCTION(fps_frame_cb);
    LOCK_FUNCTION(fps_sec_cb);
    install_int(fps_frame_cb, 16);    /* ~60 times/second */
    install_int(fps_sec_cb,  1000);   /* once per second  */

    set_color_depth(32);
    if (set_gfx_mode(GFX_AUTODETECT_WINDOWED, W, H, 0, 0) != 0) {
        allegro_message("set_gfx_mode failed: %s", allegro_error);
        return 1;
    }
    set_window_title("Allegro 4 Full Test Suite — ESC to quit");

    buf = create_bitmap(W, H);
    if (!buf) {
        allegro_message("create_bitmap failed");
        return 1;
    }

    init_bitmaps();

    /* initialise mouse trail */
    memset(trail_x, 0, sizeof(trail_x));
    memset(trail_y, 0, sizeof(trail_y));

    /* previous-frame key states for edge detection */
    int prev_left  = 0;
    int prev_right = 0;
    int prev_num[NUM_PAGES] = {0};

    while (!key[KEY_ESC]) {
        /* ── navigation: trigger only on key-down edge, not while held ── */
        int cur_left  = key[KEY_LEFT];
        int cur_right = key[KEY_RIGHT];

        if (cur_right && !prev_right) page = (page + 1) % NUM_PAGES;
        if (cur_left  && !prev_left)  page = (page + NUM_PAGES - 1) % NUM_PAGES;

        for (int k = 0; k < NUM_PAGES - 1; k++) {
            int cur = key[KEY_1 + k];
            if (cur && !prev_num[k]) page = k;
            prev_num[k] = cur;
        }
        /* KEY_0 → last page (sound) */
        {
            int cur0 = key[KEY_0];
            if (cur0 && !prev_num[NUM_PAGES - 1]) page = NUM_PAGES - 1;
            prev_num[NUM_PAGES - 1] = cur0;
        }

        prev_right = cur_right;
        prev_left  = cur_left;

        /* ── render ── */
        clear_to_color(buf, makecol(18, 18, 22));

        switch (page) {
            case 0: page_primitives(); break;
            case 1: page_bitmaps();    break;
            case 2: page_sprites();    break;
            case 3: page_blending();   break;
            case 4: page_text();       break;
            case 5: page_keyboard();   break;
            case 6: page_mouse();      break;
            case 7: page_timer();      break;
            case 8: page_images();     break;
            case 9: page_sound();      break;
        }

        draw_statusbar();
        blit(buf, screen, 0, 0, 0, 0, W, H);
        vsync();
        tick++;
    }

    if (snd_playing && snd_pid > 0) {
        kill(snd_pid, SIGTERM);
        waitpid(snd_pid, NULL, 0);
    }
    remove_sound();
    free_bitmaps();
    destroy_bitmap(buf);
    allegro_exit();
    return 0;
}

END_OF_MAIN()