/*
 * page-trees: screensaver of the page trees, from "Anathem".
 *
 * A grove through the year: the leaves bud, broaden, turn and are picked, dried on
 * lines, pressed under stones for a hundred years, and cut into pages.
 *
 *   page-trees            full screen on every display; any key or mouse movement quits
 *   page-trees --window   in a window; Esc or Q quits
 *   PageTrees.scr /s      Windows screensaver conventions
 */
#include <SDL.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* The pictures (drawn with Gemini, prepared by tools/make_sprites.py) are built
 * into the program as PNG and JPEG files and decoded at start-up. */
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wunused-but-set-variable"
#pragma GCC diagnostic ignored "-Wmisleading-indentation"
#endif
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#define STBI_NO_STDIO
#define STBI_NO_THREAD_LOCALS /* else Windows builds need libwinpthread-1.dll */
#include "stb_image.h"
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

#ifdef _WIN32
#define ART_SECTION ".section .rdata,\"dr\"\n"
#else
#define ART_SECTION ".section .rodata\n"
#endif
#define ART(name, file) \
    __asm__(ART_SECTION ".global art_" #name "\nart_" #name ":\n.incbin \"assets/art/" file "\"\n" \
            ".global art_" #name "_end\nart_" #name "_end:\n.text\n"); \
    extern const unsigned char art_##name[], art_##name##_end[];
ART(bg, "bg.jpg")
ART(banner, "banner.png")
ART(tree1, "tree1.png")
ART(tree2, "tree2.png")
ART(leaf_g, "leaf_g.png")
ART(leaf_y, "leaf_y.png")
ART(leaf_d, "leaf_d.png")
ART(stone, "stone.png")
ART(frame, "frame.png")
ART(f_reach, "f_reach.png")
ART(f_carry, "f_carry.png")
ART(f_write, "f_write.png")
ART(f_stand, "f_stand.png")

enum { SP_BG, SP_BANNER, SP_TREE1, SP_TREE2, SP_LEAF_G, SP_LEAF_Y, SP_LEAF_D, SP_STONE, SP_FRAME, SP_F_REACH, SP_F_CARRY, SP_F_WRITE, SP_F_STAND, NSPRITE };
static struct { const unsigned char *file, *end; int w, h; unsigned char *px; } sprite[NSPRITE] = {
    {art_bg, art_bg_end},
    {art_banner, art_banner_end},
    {art_tree1, art_tree1_end},
    {art_tree2, art_tree2_end},
    {art_leaf_g, art_leaf_g_end},
    {art_leaf_y, art_leaf_y_end},
    {art_leaf_d, art_leaf_d_end},
    {art_stone, art_stone_end},
    {art_frame, art_frame_end},
    {art_f_reach, art_f_reach_end},
    {art_f_carry, art_f_carry_end},
    {art_f_write, art_f_write_end},
    {art_f_stand, art_f_stand_end},
};
static SDL_Texture **cur_tex; /* the pictures, for the renderer being drawn to */

static void load_sprites(void)
{
    for (int i = 0; i < NSPRITE; i++) {
        int n;
        sprite[i].px = stbi_load_from_memory(sprite[i].file, (int)(sprite[i].end - sprite[i].file), &sprite[i].w, &sprite[i].h, &n, 4);
        if (!sprite[i].px) { sprite[i].w = sprite[i].h = 1; }
    }
}
static void make_textures(SDL_Renderer *r, SDL_Texture **tex)
{
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "linear");
    for (int i = 0; i < NSPRITE; i++) {
        tex[i] = NULL;
        if (!sprite[i].px) continue;
        tex[i] = SDL_CreateTexture(r, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, sprite[i].w, sprite[i].h);
        if (!tex[i]) continue;
        SDL_UpdateTexture(tex[i], NULL, sprite[i].px, sprite[i].w * 4);
        SDL_SetTextureBlendMode(tex[i], SDL_BLENDMODE_BLEND);
    }
}

/* ---- options ---- */
static int opt_window = 0;
static double opt_speed = 1.0;
static uint64_t opt_seed = 0;
static const char *opt_snapshot = NULL;
static double opt_time = 30;
static int opt_w = 1920, opt_h = 1200;

/* ---- colours ---- */
typedef struct { Uint8 r, g, b; } Rgb;
static const Rgb BG = {8, 8, 20}, INK = {46, 32, 22}, CHORD = {150, 44, 30}, PAPER = {253, 246, 226};
static const Rgb WOOD = {120, 84, 52}, WALL = {66, 56, 48}, DRIED = {205, 183, 142}, DRIED2 = {188, 165, 124}, PAGE = {246, 236, 208};

/* ---- geometry buffer ---- */
static SDL_Vertex *vb;
static int *ib;
static int nv, ni, vcap, icap;

static void vb_need(int v, int i)
{
    if (nv + v > vcap) { vcap = (nv + v) * 2; vb = realloc(vb, sizeof *vb * vcap); }
    if (ni + i > icap) { icap = (ni + i) * 2; ib = realloc(ib, sizeof *ib * icap); }
}
static inline SDL_Color col(Rgb c, double a)
{
    SDL_Color o = {c.r, c.g, c.b, (Uint8)(a < 0 ? 0 : a > 1 ? 255 : a * 255)};
    return o;
}
static inline void vert(double x, double y, SDL_Color c)
{
    vb[nv].position.x = (float)x; vb[nv].position.y = (float)y;
    vb[nv].color = c; vb[nv].tex_coord.x = vb[nv].tex_coord.y = 0;
    nv++;
}
static void quad4(double x0, double y0, double x1, double y1, double x2, double y2, double x3, double y3,
                  SDL_Color c0, SDL_Color c1, SDL_Color c2, SDL_Color c3)
{
    vb_need(4, 6);
    int b = nv;
    vert(x0, y0, c0); vert(x1, y1, c1); vert(x2, y2, c2); vert(x3, y3, c3);
    ib[ni++] = b; ib[ni++] = b + 1; ib[ni++] = b + 2; ib[ni++] = b; ib[ni++] = b + 2; ib[ni++] = b + 3;
}
static void quad(double x0, double y0, double x1, double y1, double x2, double y2, double x3, double y3, SDL_Color c)
{
    quad4(x0, y0, x1, y1, x2, y2, x3, y3, c, c, c, c);
}
static void rect(double x, double y, double w, double h, SDL_Color c) { quad(x, y, x + w, y, x + w, y + h, x, y + h, c); }
static void disc(double cx, double cy, double r, SDL_Color centre, SDL_Color rim)
{
    int n = r < 4 ? 10 : r < 30 ? 28 : 56;
    vb_need(n + 1, 3 * n);
    int b = nv;
    vert(cx, cy, centre);
    for (int i = 0; i < n; i++) vert(cx + r * cos(i * 2 * M_PI / n), cy + r * sin(i * 2 * M_PI / n), rim);
    for (int i = 0; i < n; i++) { ib[ni++] = b; ib[ni++] = b + 1 + i; ib[ni++] = b + 1 + (i + 1) % n; }
}
/* a stroke with round ends */
static void line(double x0, double y0, double x1, double y1, double w, SDL_Color c)
{
    double dx = x1 - x0, dy = y1 - y0, l = hypot(dx, dy);
    if (l > 1e-9) {
        double nx = -dy / l * w / 2, ny = dx / l * w / 2;
        quad(x0 + nx, y0 + ny, x1 + nx, y1 + ny, x1 - nx, y1 - ny, x0 - nx, y0 - ny, c);
    }
    if (w > 2.5) { disc(x0, y0, w / 2, c, c); disc(x1, y1, w / 2, c, c); }
}
__attribute__((unused)) static void ring(double cx, double cy, double r, double w, SDL_Color c)
{
    for (int i = 0; i < 64; i++) {
        double a = i * M_PI / 32, b = (i + 1) * M_PI / 32;
        line(cx + cos(a) * r, cy + sin(a) * r, cx + cos(b) * r, cy + sin(b) * r, w, c);
    }
}
static void flush(SDL_Renderer *r)
{
    if (ni) SDL_RenderGeometry(r, NULL, vb, nv, ib, ni);
    nv = ni = 0;
}

static SDL_Renderer *ren; /* where the current frame is being drawn */

/* a picture stretched over a rectangle; sx0..sx1 picks a slice of it across (0..1) */
static void pic_part(int which, double x, double y, double w, double h, double sx0, double sx1, double tilt, int flip)
{
    flush(ren);
    if (!cur_tex || !cur_tex[which]) return;
    SDL_Rect src = {(int)(sx0 * sprite[which].w), 0, (int)((sx1 - sx0) * sprite[which].w), sprite[which].h};
    SDL_FRect dst = {(float)x, (float)y, (float)w, (float)h};
    SDL_FPoint pivot = {(float)(w / 2), (float)h};
    SDL_RenderCopyExF(ren, cur_tex[which], &src, &dst, tilt, &pivot, flip ? SDL_FLIP_HORIZONTAL : SDL_FLIP_NONE);
}
/* a picture `tall` high, standing with the point (ax, ay) of it (0..1 across and down) at (x, y) */
__attribute__((unused)) static void pic(int which, double x, double y, double tall, double ax, double ay)
{
    double w = tall * sprite[which].w / sprite[which].h;
    pic_part(which, x - w * ax, y - tall * ay, w, tall, 0, 1, 0, 0);
}

/* ---- lettering: pen strokes on a 4 x 6 grid, drawn with a round nib ---- */
static const char *GLYPH[] = {
    "A06 02 20 42 46|04 44", "B00 06 36 45 44 33 03|33 42 41 30 00", "C41 30 10 01 05 16 36 45",
    "D00 06 26 44 42 20 00", "E40 00 06 46|03 33", "F40 00 06|03 33", "G41 30 10 01 05 16 36 45 43 23",
    "H00 06|40 46|03 43", "I20 26|10 30|16 36", "J40 45 36 16 05", "K00 06|40 03 46", "L00 06 46",
    "M06 00 23 40 46", "N06 00 46 40", "O10 30 41 45 36 16 05 01 10", "P06 00 30 41 42 33 03",
    "Q10 30 41 45 36 16 05 01 10|24 46", "R06 00 30 41 42 33 03|23 46", "S41 30 10 01 02 13 33 44 45 36 16 05",
    "T00 40|20 26", "U00 05 16 36 45 40", "V00 26 40", "W00 16 23 36 40", "X00 46|40 06", "Y00 23 40|23 26",
    "Z00 40 06 46", "010 30 41 45 36 16 05 01 10", "111 20 26|16 36", "201 10 30 41 42 06 46",
    "301 10 30 41 42 33 23|33 44 45 36 16 05", "436 30 04 44", "540 00 03 33 44 45 36 16 05",
    "630 10 01 05 16 36 45 44 33 03", "700 40 16", "813 02 01 10 30 41 42 33 13 04 05 16 36 45 44 33",
    "943 13 02 01 10 30 41 45 36 16", ",25 26 17", ".26 26", ":22 22|25 25", "'20 21", "?01 10 30 41 42 24|26 26",
    "-13 33", "!20 24|26 26", "+03 43|21 25", "=02 42|04 44",
};
static const char *glyph(char ch)
{
    for (unsigned i = 0; i < sizeof GLYPH / sizeof *GLYPH; i++)
        if (GLYPH[i][0] == ch) return GLYPH[i] + 1;
    return NULL;
}
static double text_width(const char *str, double s) { return strlen(str) * 6.2 * s - 2.2 * s; }
/* text centred on cx, top at y; s is one grid step, so letters are 6s tall */
static void text(const char *str, double cx, double y, double s, double weight, SDL_Color c)
{
    double x = cx - text_width(str, s) / 2;
    for (; *str; str++, x += 6.2 * s) {
        const char *g = glyph(*str);
        if (!g) continue;
        int have = 0; double lx = 0, ly = 0;
        for (; *g; g++) {
            if (*g == '|') { have = 0; continue; }
            if (*g == ' ') continue;
            double gx = x + (g[0] - '0') * s, gy = y + (g[1] - '0') * s;
            g++;
            if (have) line(lx, ly, gx, gy, s * weight, c);
            disc(gx, gy, s * weight * (have && lx == gx && ly == gy ? 0.62 : 0.5), c, c); /* the nib is round */
            lx = gx; ly = gy; have = 1;
        }
    }
}
/* ---- the grove ----
 *
 * A grove of page trees through one year, and then the hundred years after. The
 * leaves bud in spring, broaden through the summer, and turn in autumn, when they
 * are picked by hand and strung on lines to dry. Then they are stacked indoors under
 * stones for a century, and cut square in a frame; about one in ten makes a page.
 *
 * A year takes YEAR seconds. It starts at the season it really is (taking the year
 * to begin with spring on the first of March, as in the north). */
#define YEAR 150.0
#define NTREE 5
#define NL 34                 /* leaves on a tree */
#define NLEAF (NTREE * NL)
#define SLOTS 11              /* places on the line between one tree and the next */
#define NFLAKE 110
typedef struct { double ax, ay, tilt, size, sway; int tree, order, slot; } Leaf;
static Leaf leaf[NLEAF];
static const double TX[NTREE] = {0.1, 0.29, 0.5, 0.71, 0.9};      /* where the trees stand, across */
static const double TY[NTREE] = {0.8, 0.86, 0.79, 0.87, 0.81};    /* their feet, down the picture */
static const double TH[NTREE] = {0.56, 0.66, 0.54, 0.68, 0.58};   /* how tall */
static struct { double x, y, v, sway; } flake[NFLAKE];
static double T, anim_t;
static uint64_t seed_state;
static int opt_day = -1;      /* --date: begin at this day of the year instead of today */

/* the year, as fractions of it */
#define P_BUD 0.03
#define P_FULL 0.22
#define P_TURN 0.40
#define P_PICK 0.50
#define P_PICKED 0.68
#define P_DRY 0.80
#define P_IN 0.84
#define P_CUT 0.925

static uint64_t next_rand(void)
{
    seed_state ^= seed_state << 13; seed_state ^= seed_state >> 7; seed_state ^= seed_state << 17;
    return seed_state;
}
static double frand(void) { return (next_rand() >> 11) * (1.0 / 9007199254740992.0); }
static double ramp(double t, double a, double b) { return t <= a ? 0 : t >= b ? 1 : (t - a) / (b - a); }
static double ease(double x) { return x * x * (3 - 2 * x); }

static void new_round(void)
{
    int n = 0;
    for (int t = 0; t < NTREE; t++) {
        int perm[NL];
        for (int i = 0; i < NL; i++) perm[i] = i;
        for (int i = NL - 1; i > 0; i--) { int j = (int)(frand() * (i + 1)) % (i + 1), k = perm[i]; perm[i] = perm[j]; perm[j] = k; }
        for (int i = 0; i < NL; i++, n++) {
            /* in the crown: a wide oval over the top two-thirds of the tree */
            double a = frand() * 2 * M_PI, r = sqrt(frand());
            leaf[n] = (Leaf){cos(a) * r * 0.46, 0.36 + sin(a) * r * 0.27, (frand() - 0.5) * 50, 0.8 + 0.4 * frand(), frand() * 6.28, t, t * NL + perm[i], -1};
        }
    }
    /* every third leaf picked is one we see on the lines; the rest go straight indoors */
    for (int i = 0; i < NLEAF; i++) if (leaf[i].order % 3 == 0 && leaf[i].tree < NTREE - 1) {
        int k = (leaf[i].order - leaf[i].tree * NL) / 3;
        if (k < SLOTS) leaf[i].slot = leaf[i].tree * SLOTS + k;
    }
    for (int i = 0; i < NFLAKE; i++) { flake[i].x = frand(); flake[i].y = frand(); flake[i].v = 0.03 + 0.04 * frand(); flake[i].sway = frand() * 6.28; }
}

static void start_at_today(void)
{
    time_t tt = time(NULL);
    struct tm *tm = localtime(&tt);
    int day = opt_day >= 0 ? opt_day : tm ? tm->tm_yday : 0;
    T = fmod((day - 59 + 365) % 365 / 365.0, 1.0) * YEAR;
}

static void update(double dt)
{
    anim_t += dt;
    T += dt * opt_speed;
    if (T >= YEAR) T -= YEAR;
    for (int i = 0; i < NFLAKE; i++) { flake[i].y += flake[i].v * dt; if (flake[i].y > 1) { flake[i].y -= 1; flake[i].x = frand(); } }
}

/* ---- drawing ---- */
static double unit;
static int sw, sh;

/* a picture `tall` high hanging from the point (x, y) by the middle of its top edge */
static void hang(int which, double x, double y, double tall, double deg, double alpha)
{
    if (alpha <= 0.01 || !cur_tex || !cur_tex[which]) return;
    double w = tall * sprite[which].w / sprite[which].h;
    SDL_FRect dst = {(float)(x - w / 2), (float)y, (float)w, (float)tall};
    SDL_FPoint pivot = {(float)(w / 2), 0};
    SDL_SetTextureAlphaMod(cur_tex[which], (Uint8)(fmin(alpha, 1) * 255));
    SDL_RenderCopyExF(ren, cur_tex[which], NULL, &dst, deg, &pivot, SDL_FLIP_NONE);
    SDL_SetTextureAlphaMod(cur_tex[which], 255);
}
/* a leaf, part-way between two of its colours */
static void a_leaf(double x, double y, double tall, double deg, int from, int to, double mix, double alpha)
{
    hang(from, x, y, tall, deg, alpha);
    if (mix > 0.01) hang(to, x, y, tall, deg, alpha * mix);
}
/* where a place on the lines is: the line sags between one tree and the next */
static void slot_at(int slot, double *x, double *y)
{
    int k = slot / SLOTS;
    double f = (slot % SLOTS + 1.0) / (SLOTS + 1);
    double x0 = TX[k] * sw, x1 = TX[k + 1] * sw, y0 = (TY[k] - TH[k] * 0.36) * sh, y1 = (TY[k + 1] - TH[k + 1] * 0.36) * sh;
    *x = x0 + (x1 - x0) * f; *y = y0 + (y1 - y0) * f + sin(f * M_PI) * sh * 0.035;
}

static void draw_caption(int w, int h, const char *l1, const char *l2)
{
    double s1 = unit * 0.46, s2 = unit * 0.3;
    double widest = fmax(text_width(l1, s1), text_width(l2, s2)), room = w * 0.66;
    if (widest > room) { s1 *= room / widest; s2 *= room / widest; }
    double bw = fmax(text_width(l1, s1), text_width(l2, s2)) + unit * 13, bh = unit * 11.5;
    double bx = w / 2.0 - bw / 2, by = unit * 1.2;
    double cap = 0.135, cw = bh * 1.15 * sprite[SP_BANNER].w * cap / sprite[SP_BANNER].h;
    pic_part(SP_BANNER, bx + cw - 1, by, bw - 2 * cw + 2, bh, cap, 1 - cap, 0, 0);
    pic_part(SP_BANNER, bx, by, cw, bh, 0, cap, 0, 0);
    pic_part(SP_BANNER, bx + bw - cw, by, cw, bh, 1 - cap, 1, 0, 0);
    text(l1, w / 2.0, by + bh * 0.25, s1, 0.95, col(INK, 1));
    text(l2, w / 2.0, by + bh * 0.25 + s1 * 6 + unit * 1.2, s2, 0.9, col(CHORD, 1));
}

/* Indoors: the stack under its stones while a hundred years go by, then a leaf in
 * the cutting frame, and the page that comes of it. */
static void indoors(double p, int w, int h)
{
    double in = ease(ramp(p, P_IN, P_IN + 0.012)) * (1 - ease(ramp(p, 0.988, 0.999)));
    if (in <= 0.01) return;
    double pw = h * 0.62, ph = h * 0.5, x0 = w * 0.5 - pw / 2, y0 = h * 0.3 + (1 - in) * h * 0.08, b = unit * 0.6;
    SDL_Color wood = col(WOOD, in), wall = col(WALL, in);
    rect(x0 - b, y0 - b, pw + 2 * b, ph + 2 * b, col(INK, in));
    rect(x0, y0, pw, ph, wall);
    rect(x0, y0 + ph * 0.8, pw, ph * 0.2, wood); /* a shelf */
    flush(ren);
    double cut = ramp(p, P_CUT, P_CUT + 0.012), base = y0 + ph * 0.8, cx = x0 + pw * 0.5;
    if (cut < 1) {
        /* the stack, seen from the side: leaves lying flat, pressed thinner as the years pass */
        double years = ease(ramp(p, P_IN + 0.015, P_CUT - 0.008)), thick = ph * (0.022 - 0.008 * years), sw_ = pw * 0.5;
        for (int k = 0; k < 16; k++) {
            double y = base - (k + 1) * thick, off = sin(k * 2.4) * pw * 0.012;
            rect(cx - sw_ / 2 + off, y, sw_, thick * 0.9, col(k % 2 ? DRIED : DRIED2, in * (1 - cut)));
        }
        flush(ren);
        double top = base - 16 * thick, stone = pw * 0.2;
        for (int k = 0; k < 3; k++) {
            double sx_ = cx + (k - 1) * stone * 0.92, hh = stone * sprite[SP_STONE].h / sprite[SP_STONE].w;
            SDL_FRect d = {(float)(sx_ - stone / 2), (float)(top - hh * (k == 1 ? 1.5 : 0.92)), (float)stone, (float)hh};
            if (cur_tex && cur_tex[SP_STONE]) { SDL_SetTextureAlphaMod(cur_tex[SP_STONE], (Uint8)(in * (1 - cut) * 255)); SDL_RenderCopyF(ren, cur_tex[SP_STONE], NULL, &d); SDL_SetTextureAlphaMod(cur_tex[SP_STONE], 255); }
        }
        char buf[40];
        int yr = (int)(years * 100 + 0.5);
        snprintf(buf, sizeof buf, yr >= 100 ? "A HUNDRED YEARS" : "YEAR %d", yr < 1 ? 1 : yr);
        text(buf, cx, y0 + ph * 0.1, unit * 0.36, 1, col(PAPER, in * (1 - cut)));
    } else {
        /* a dried leaf laid flat, the frame set on it, and the square of it that is kept */
        double q = ramp(p, P_CUT + 0.012, 0.985), lh = ph * 0.6, lx = x0 + pw * 0.3, ly = y0 + ph * 0.1;
        double framed = ease(ramp(q, 0.1, 0.3)), kept = ease(ramp(q, 0.36, 0.5)), written = ramp(q, 0.55, 0.95);
        double fw = lh * 0.6, fh = lh * 0.78, fx = lx - fw / 2, fy = ly + lh * 0.12;
        hang(SP_LEAF_D, lx, ly, lh, 0, in * (1 - kept));
        rect(fx, fy, fw, fh, col(PAGE, in * kept));
        for (int k = 0; k < 9; k++) { /* and it is written on */
            double f = ramp(written, k / 9.0, (k + 1) / 9.0), yy = fy + fh * (0.14 + 0.085 * k);
            if (f > 0) line(fx + fw * 0.12, yy, fx + fw * (0.12 + 0.76 * f * (k == 8 ? 0.5 : 1)), yy, unit * 0.14, col(INK, in * 0.8));
        }
        flush(ren);
        double fr = fh * 1.16, slide = (1 - framed) * pw * 0.5;
        if (kept < 1 && cur_tex && cur_tex[SP_FRAME]) {
            double fww = fr * sprite[SP_FRAME].w / sprite[SP_FRAME].h;
            SDL_FRect d = {(float)(lx - fww / 2 + slide), (float)(fy - fh * 0.08), (float)fww, (float)fr};
            SDL_SetTextureAlphaMod(cur_tex[SP_FRAME], (Uint8)(in * (1 - kept) * 255)); SDL_RenderCopyF(ren, cur_tex[SP_FRAME], NULL, &d); SDL_SetTextureAlphaMod(cur_tex[SP_FRAME], 255);
        }
        double tall = ph * 0.62;
        if (cur_tex && cur_tex[SP_F_WRITE]) SDL_SetTextureAlphaMod(cur_tex[SP_F_WRITE], (Uint8)(in * 255));
        pic(SP_F_WRITE, x0 + pw * 0.75, base + ph * 0.06, tall, 0.5, 1);
        flush(ren);
        if (cur_tex && cur_tex[SP_F_WRITE]) SDL_SetTextureAlphaMod(cur_tex[SP_F_WRITE], 255);
    }
    flush(ren);
}

static void draw_scene(SDL_Renderer *r, int w, int h)
{
    ren = r; sw = w; sh = h;
    unit = (w < h ? w : h) * 0.0125;
    SDL_SetRenderDrawColor(r, BG.r, BG.g, BG.b, 255);
    SDL_RenderClear(r);
    pic_part(SP_BG, 0, 0, w, h, 0, 1, 0, 0);
    double p = T / YEAR;
    /* the light of the season */
    double autumn = ramp(p, P_TURN, P_PICK) * (1 - ramp(p, P_DRY, P_IN)), winter = ramp(p, P_DRY, P_IN) * (1 - ramp(p, 0.985, 1.0)) + (1 - ramp(p, 0, P_BUD));
    if (winter > 1) winter = 1;
    if (autumn > 0) rect(0, 0, w, h, (SDL_Color){230, 150, 60, (Uint8)(34 * autumn)});
    if (winter > 0) rect(0, 0, w, h, (SDL_Color){196, 214, 236, (Uint8)(110 * winter)});
    flush(r);

    double wind = sin(anim_t * 0.31) * 0.6 + sin(anim_t * 0.13 + 1) * 0.4;
    double picking = ramp(p, P_PICK, P_PICKED) * NLEAF;                 /* how many leaves have been picked */
    int order_tree[NTREE] = {0, 2, 4, 1, 3};                           /* far trees first, near ones over them */
    for (int oi = 0; oi < NTREE; oi++) {
        int t = order_tree[oi];
        double tall = TH[t] * h, tw = tall * sprite[SP_TREE1 + t % 2].w / sprite[SP_TREE1 + t % 2].h, x = TX[t] * w, y = TY[t] * h;
        disc(x, y, tw * 0.3, col(INK, 0.25), col(INK, 0));
        pic(SP_TREE1 + t % 2, x, y, tall, 0.5, 1);
        flush(r);
        for (int i = t * NL; i < (t + 1) * NL; i++) {
            const Leaf *l = &leaf[i];
            double born = P_BUD + (P_FULL - P_BUD) * 0.5 * (l->order % NL) / NL, grown = ease(ramp(p, born, born + (P_FULL - P_BUD) * 0.5));
            double since = picking - l->order;                           /* > 0 once it has been picked */
            if (grown <= 0 || since > 1.5) continue;
            double lx = x + l->ax * tw, ly = y - tall + l->ay * tall, lt = tall * 0.15 * l->size * (0.25 + 0.75 * grown);
            double deg = l->tilt + (sin(anim_t * 0.9 + l->sway) * 5 + wind * 6), fall = since > 0 ? since / 1.5 : 0;
            a_leaf(lx, ly + fall * fall * tall * 0.2, lt, deg + fall * 40, SP_LEAF_G, SP_LEAF_Y, ramp(p, P_TURN + 0.06 * l->sway / 6.28, P_PICK), 1 - fall);
        }
    }
    /* the lines, and the leaves drying on them */
    double lines = ramp(p, P_PICK - 0.02, P_PICK) * (1 - ramp(p, P_IN - 0.005, P_IN + 0.01));
    if (lines > 0) {
        for (int k = 0; k < NTREE - 1; k++) for (int s = 0; s <= SLOTS; s++) {
            double x0, y0, x1, y1;
            if (s == 0) { x0 = TX[k] * w; y0 = (TY[k] - TH[k] * 0.36) * h; } else slot_at(k * SLOTS + s - 1, &x0, &y0);
            if (s == SLOTS) { x1 = TX[k + 1] * w; y1 = (TY[k + 1] - TH[k + 1] * 0.36) * h; } else slot_at(k * SLOTS + s, &x1, &y1);
            line(x0, y0, x1, y1, unit * 0.16, col(INK, 0.75 * lines));
        }
        flush(r);
        for (int i = 0; i < NLEAF; i++) if (leaf[i].slot >= 0) {
            double hung = ramp(picking - leaf[i].order, 6, 9), taken = ramp(p, P_DRY + 0.03 * leaf[i].slot / (double)(SLOTS * (NTREE - 1)), P_DRY + 0.035 + 0.03 * leaf[i].slot / (double)(SLOTS * (NTREE - 1)));
            double x, y;
            if (hung <= 0 || taken >= 1) continue;
            slot_at(leaf[i].slot, &x, &y);
            a_leaf(x, y, h * 0.085 * leaf[i].size, sin(anim_t * 1.1 + leaf[i].sway) * 7 + wind * 10, SP_LEAF_Y, SP_LEAF_D, ramp(p, P_PICKED - 0.04, P_DRY - 0.02), hung * (1 - taken));
        }
    }
    flush(r);
    /* the fraa: admiring them in spring and summer, and picking in autumn */
    if (p > P_BUD + 0.02 && p < P_PICK - 0.01) pic(SP_F_STAND, w * 0.4, h * 0.93, h * 0.3, 0.5, 1);
    else if (p >= P_PICK - 0.01 && p < P_PICKED + 0.01) {
        double f = ramp(p, P_PICK, P_PICKED) * NTREE;
        int t = (int)f; if (t >= NTREE) t = NTREE - 1;
        double part = f - t, x = TX[t] * w + h * 0.1, bob = fabs(sin(anim_t * 3)) * unit * 0.3;
        if (part > 0.82 && t < NTREE - 1) { x += (TX[t + 1] * w - TX[t] * w) * ease((part - 0.82) / 0.18); pic(SP_F_CARRY, x, h * 0.95 - bob, h * 0.3, 0.5, 1); }
        else pic(SP_F_REACH, x, h * 0.95 - (sin(anim_t * 2.2) > 0.6 ? unit * 0.6 : 0), h * 0.3, 0.5, 1);
    }
    flush(r);
    if (winter > 0) for (int i = 0; i < NFLAKE; i++) /* snow */
        disc(w * (flake[i].x + 0.012 * sin(anim_t * 0.9 + flake[i].sway)), h * flake[i].y, unit * (0.3 + flake[i].v * 6), col(PAPER, 0.95 * winter), col(PAPER, 0.6 * winter));
    flush(r);
    indoors(p, w, h);

    const char *l1 = "THE PAGE TREES IN WINTER", *l2 = "BARE, AND WAITING";
    if (p >= P_BUD) { l1 = "THE PAGE TREES"; l2 = "BRED LONG AGO SO THAT THEIR LEAVES COULD BE WRITTEN ON"; }
    if (p >= P_FULL) { l1 = "SUMMER"; l2 = "THE BEST LEAVES GROW ON THE MIDDLE BRANCHES, NOT FAR FROM THE TRUNK"; }
    if (p >= P_TURN) { l1 = "THE LEAVES TURN"; l2 = "A WARM WET SUMMER MAKES MORE GOOD ONES; A COLD DRY ONE, FEWER"; }
    if (p >= P_PICK) { l1 = "IN THE AUTUMN THEY ARE PICKED BY HAND"; l2 = "AND STRUNG ON LINES TO DRY FOR SEVERAL WEEKS"; }
    if (p >= P_DRY) { l1 = "THEN THEY ARE TAKEN INDOORS"; l2 = "AND STACKED UNDER STONES TO PRESS THEM FLAT"; }
    if (p >= P_IN + 0.015) { l1 = "THERE THEY STAY"; l2 = "FOR A HUNDRED YEARS"; }
    if (p >= P_CUT) { l1 = "CUT SQUARE IN A FRAME"; l2 = "ABOUT ONE LEAF IN TEN IS GOOD ENOUGH TO MAKE A PAGE"; }
    if (p >= 0.955) { l1 = "A PAGE"; l2 = "WRITTEN ON A LEAF THAT WAS PICKED A CENTURY AGO"; }
    draw_caption(w, h, l1, l2);
    flush(r);
}

typedef struct { SDL_Window *win; SDL_Renderer *ren; SDL_Texture *scene, *tex[NSPRITE]; int w, h; } View;

static void size_view(View *v)
{
    int ow, oh;
    SDL_RendererInfo info;
    SDL_GetRendererOutputSize(v->ren, &ow, &oh);
    if (ow < 1 || oh < 1) return;
    int ss = 2; /* drawn at twice the size and scaled down, for smooth edges */
    if (SDL_GetRendererInfo(v->ren, &info) == 0) {
        if (!(info.flags & SDL_RENDERER_TARGETTEXTURE) || (info.flags & SDL_RENDERER_SOFTWARE)) ss = 0;
        else if (info.max_texture_width && (ow * 2 > info.max_texture_width || oh * 2 > info.max_texture_height)) ss = 0;
    }
    if (v->w == ow * (ss ? ss : 1) && v->h == oh * (ss ? ss : 1) && (v->scene || !ss)) return;
    if (v->scene) SDL_DestroyTexture(v->scene);
    v->scene = NULL; v->w = ow; v->h = oh;
    if (!ss) return;
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "linear");
    v->scene = SDL_CreateTexture(v->ren, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET, ow * ss, oh * ss);
    if (v->scene) { v->w = ow * ss; v->h = oh * ss; }
}
static void render(View *v)
{
    if (v->scene) SDL_SetRenderTarget(v->ren, v->scene);
    cur_tex = v->tex;
    draw_scene(v->ren, v->w, v->h);
    if (v->scene) { SDL_SetRenderTarget(v->ren, NULL); SDL_RenderCopy(v->ren, v->scene, NULL, NULL); }
    SDL_RenderPresent(v->ren);
}
static int make_view(View *v, int display)
{
    Uint32 flags = SDL_WINDOW_ALLOW_HIGHDPI;
    int x = SDL_WINDOWPOS_CENTERED_DISPLAY(display);
    if (opt_window) flags |= SDL_WINDOW_RESIZABLE;
    else flags |= SDL_WINDOW_FULLSCREEN_DESKTOP | SDL_WINDOW_BORDERLESS;
    memset(v, 0, sizeof *v);
    v->win = SDL_CreateWindow("The Page Trees", x, x, 1200, 750, flags);
    if (!v->win) return 0;
    v->ren = SDL_CreateRenderer(v->win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC | SDL_RENDERER_TARGETTEXTURE);
    if (!v->ren) v->ren = SDL_CreateRenderer(v->win, -1, SDL_RENDERER_SOFTWARE);
    if (!v->ren) return 0;
    SDL_SetRenderDrawBlendMode(v->ren, SDL_BLENDMODE_BLEND);
    make_textures(v->ren, v->tex);
    return 1;
}

static void start_show(void)
{
    seed_state = opt_seed ? opt_seed * 0x9E3779B97F4A7C15ULL : ((uint64_t)time(NULL) << 20) ^ SDL_GetPerformanceCounter();
    if (!seed_state) seed_state = 1;
    for (int i = 0; i < 6; i++) next_rand();
    anim_t = 0;
    new_round();
    start_at_today();
}

static int snapshot(void)
{
    SDL_Surface *surf = SDL_CreateRGBSurfaceWithFormat(0, opt_w, opt_h, 32, SDL_PIXELFORMAT_ARGB8888);
    SDL_Renderer *r = surf ? SDL_CreateSoftwareRenderer(surf) : NULL;
    if (!r) { fprintf(stderr, "page-trees: %s\n", SDL_GetError()); return 1; }
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    SDL_Texture *tex[NSPRITE];
    make_textures(r, tex);
    cur_tex = tex;
    start_show();
    for (double t = 0; t < opt_time; t += 1.0 / 60) update(1.0 / 60);
    draw_scene(r, opt_w, opt_h);
    SDL_RenderPresent(r);
    if (SDL_SaveBMP(surf, opt_snapshot) != 0) { fprintf(stderr, "page-trees: %s\n", SDL_GetError()); return 1; }
    return 0;
}

static void usage(void)
{
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_INFORMATION, "The Page Trees",
        "The Page Trees screensaver\n\n"
        "  --window        run in a window (Esc or Q quits)\n"
        "  --speed X       animation speed, 1 = normal\n"
        "  --seed N        repeatable sequence\n"
        "  --date MM-DD    begin at this day of the year instead of today\n"
        "  --snapshot FILE.bmp [--time SECONDS] [--size WxH]\n"
        "                  write a picture of the screensaver after SECONDS and exit\n", NULL);
}

int main(int argc, char **argv)
{
    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        if ((a[0] == '/' || a[0] == '-') && (a[1] == 's' || a[1] == 'S') && !a[2]) continue;
        else if ((a[0] == '/' || a[0] == '-') && (a[1] == 'p' || a[1] == 'P') && (!a[2] || a[2] == ':')) return 0;
        else if ((a[0] == '/' || a[0] == '-') && (a[1] == 'c' || a[1] == 'C') && (!a[2] || a[2] == ':')) {
            SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_INFORMATION, "The Page Trees", "The Page Trees screensaver has no settings.", NULL);
            return 0;
        }
        else if (!strcmp(a, "--window")) opt_window = 1;
        else if (!strcmp(a, "--speed") && i + 1 < argc) opt_speed = SDL_atof(argv[++i]);
        else if (!strcmp(a, "--seed") && i + 1 < argc) opt_seed = SDL_strtoull(argv[++i], NULL, 10);
        else if (!strcmp(a, "--snapshot") && i + 1 < argc) opt_snapshot = argv[++i];
        else if (!strcmp(a, "--date") && i + 1 < argc) { /* MM-DD */
            static const int BEFORE[12] = {0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334};
            const char *dash = SDL_strchr(argv[++i], '-');
            int mo = SDL_atoi(argv[i]), dy = dash ? SDL_atoi(dash + 1) : 0;
            if (mo < 1 || mo > 12 || dy < 1 || dy > 31) { usage(); return 1; }
            opt_day = BEFORE[mo - 1] + dy - 1;
        }
        else if (!strcmp(a, "--time") && i + 1 < argc) opt_time = SDL_atof(argv[++i]);
        else if (!strcmp(a, "--size") && i + 1 < argc) {
            const char *x = SDL_strchr(argv[++i], 'x');
            opt_w = SDL_atoi(argv[i]); opt_h = x ? SDL_atoi(x + 1) : 0;
            if (opt_w < 64 || opt_h < 64) { usage(); return 1; }
        }
        else { usage(); return 1; }
    }
    if (opt_speed < 0.05) opt_speed = 0.05;
    load_sprites();

    if (opt_snapshot) return snapshot();
    SDL_SetHint(SDL_HINT_VIDEO_ALLOW_SCREENSAVER, "1");
    if (SDL_Init(SDL_INIT_VIDEO) != 0) { fprintf(stderr, "page-trees: %s\n", SDL_GetError()); return 1; }

    View views[8];
    int nviews = 0, ndisp = opt_window ? 1 : SDL_GetNumVideoDisplays();
    if (ndisp < 1) ndisp = 1;
    for (int d = 0; d < ndisp && nviews < 8; d++)
        if (make_view(&views[nviews], d)) nviews++;
    if (!nviews) { fprintf(stderr, "page-trees: %s\n", SDL_GetError()); return 1; }
    if (!opt_window) SDL_ShowCursor(SDL_DISABLE);
    start_show();

    Uint64 freqc = SDL_GetPerformanceFrequency(), last = SDL_GetPerformanceCounter(), start = last;
    double moved = 0;
    int running = 1;
    while (running) {
        SDL_Event e;
        double since = (double)(SDL_GetPerformanceCounter() - start) / freqc;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) running = 0;
            else if (e.type == SDL_WINDOWEVENT && e.window.event == SDL_WINDOWEVENT_CLOSE) running = 0;
            else if (e.type == SDL_KEYDOWN) {
                if (!opt_window || e.key.keysym.sym == SDLK_ESCAPE || e.key.keysym.sym == SDLK_q) running = 0;
            } else if (!opt_window && since > 1.0) {
                if (e.type == SDL_MOUSEBUTTONDOWN || e.type == SDL_MOUSEWHEEL || e.type == SDL_FINGERDOWN) running = 0;
                else if (e.type == SDL_MOUSEMOTION) {
                    moved += abs(e.motion.xrel) + abs(e.motion.yrel);
                    if (moved > 24) running = 0;
                }
            }
        }
        Uint64 now = SDL_GetPerformanceCounter();
        double dt = (double)(now - last) / freqc;
        last = now;
        if (dt > 0.1) dt = 0.1;
        update(dt);
        for (int i = 0; i < nviews; i++) { size_view(&views[i]); render(&views[i]); }
        SDL_Delay(views[0].scene ? 1 : 12);
    }
    SDL_Quit();
    return 0;
}






