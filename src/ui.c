#include "ui.h"
#include "system.h"      /* g_accent, sys_battery */
#include "keyboard.h"    /* osk_run / osk_demo (portable keyboard module) */
#include <SDL2/SDL_ttf.h>
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

/* palette (NextUI-flavoured) */
static const SDL_Color C_BG    = { 17,  18,  22, 255};
static const SDL_Color C_WHITE = {236, 238, 240, 255};
static const SDL_Color C_DARK  = { 24,  26,  30, 255};
static const SDL_Color C_GRAY  = {150, 154, 164, 255};
static const SDL_Color C_TRACK = { 46,  48,  56, 255};
static const SDL_Color C_CHIP  = { 40,  42,  50, 255};
static const SDL_Color C_MUTE  = { 92,  96, 106, 255};
#define C_ARROW C_TRACK   /* scroll arrows match the volume-bar track gray */

static TTF_Font *g_font_l, *g_font_m, *g_font_s;
static SDL_Texture *g_arrow_up, *g_arrow_down;
static int g_arrow_w, g_arrow_h;

/* main-screen vertical layout (kept together so the pieces stay in sync) */
#define TAB_Y        108            /* preset tab strip top (below sleep line) */
#define TAB_H         48            /* tab height                              */
#define LIST_TOP     246            /* first channel row top                   */
#define ROW_H         56            /* channel selection-pill height           */
#define ROW_PITCH     80            /* channel row spacing                     */
#define HINT_Y    (UI_H - 56)       /* bottom hint bar                         */

/* ---- text label helpers -------------------------------------------- */

/* One UTF-8 char: byte length + display width (ASCII = 1, multibyte = 2). */
static int utf8_step(const char *p, int *units)
{
    unsigned char b = (unsigned char)*p;
    if (b < 0x80)        { *units = 1; return 1; }
    if ((b >> 5) == 0x6) { *units = 2; return 2; }
    if ((b >> 4) == 0xE) { *units = 2; return 3; }
    if ((b >> 3) == 0x1E){ *units = 2; return 4; }
    *units = 1; return 1;
}

void ui_make_disp(const char *src, char *out, int outsz)
{
    int total = 0, u;
    for (const char *q = src; *q; ) q += utf8_step(q, &u), total += u;
    if (total <= 16) {
        strncpy(out, src, outsz - 1);
        out[outsz - 1] = 0;
        return;
    }
    const char *p = src, *lastStart = src;
    int used = 0;
    while (*p) {
        int adv = utf8_step(p, &u);
        if (used + u > 16) break;
        lastStart = p; used += u; p += adv;
    }
    int bytes = (int)(lastStart - src);   /* drop the prefix's last char */
    if (bytes > outsz - 4) bytes = outsz - 4;
    memcpy(out, src, bytes);
    out[bytes] = 0;
    strncat(out, "...", outsz - 1 - bytes);
}

/* ---- drawing primitives -------------------------------------------- */

static void set_color(SDL_Renderer *r, SDL_Color c)
{
    SDL_SetRenderDrawColor(r, c.r, c.g, c.b, c.a);
}

static SDL_Color col_mix(SDL_Color a, SDL_Color b, float t)
{
    return (SDL_Color){
        (Uint8)(a.r + (b.r - a.r) * t),
        (Uint8)(a.g + (b.g - a.g) * t),
        (Uint8)(a.b + (b.b - a.b) * t), 255};
}

static void fill_circle(SDL_Renderer *r, int cx, int cy, int rad)
{
    for (int dy = -rad; dy <= rad; dy++) {
        int hw = (int)(sqrtf((float)(rad * rad - dy * dy)) + 0.5f);
        SDL_RenderDrawLine(r, cx - hw, cy + dy, cx + hw, cy + dy);
    }
}

static void fill_round(SDL_Renderer *r, int x, int y, int w, int h, int rad,
                       SDL_Color c)
{
    if (rad * 2 > h) rad = h / 2;
    if (rad * 2 > w) rad = w / 2;
    set_color(r, c);
    SDL_RenderFillRect(r, &(SDL_Rect){x + rad, y, w - 2 * rad, h});
    SDL_RenderFillRect(r, &(SDL_Rect){x, y + rad, rad, h - 2 * rad});
    SDL_RenderFillRect(r, &(SDL_Rect){x + w - rad, y + rad, rad, h - 2 * rad});
    fill_circle(r, x + rad,         y + rad,         rad);
    fill_circle(r, x + w - rad - 1, y + rad,         rad);
    fill_circle(r, x + rad,         y + h - rad - 1, rad);
    fill_circle(r, x + w - rad - 1, y + h - rad - 1, rad);
}

/* flat fallback chevron, used only if the arrow texture failed to build */
static void tri(SDL_Renderer *r, int cx, int y, int hw, int h, int up, SDL_Color c)
{
    set_color(r, c);
    for (int dy = 0; dy <= h; dy++) {
        int t = up ? dy : (h - dy);
        int half = hw * t / h;
        SDL_RenderDrawLine(r, cx - half, y + dy, cx + half, y + dy);
    }
}

static int text_w(TTF_Font *f, const char *s)
{
    int w = 0, h = 0;
    if (f && s) TTF_SizeUTF8(f, s, &w, &h);
    return w;
}

/* anchor: 0 = left, 1 = center, 2 = right (x is the anchor point) */
static void text(SDL_Renderer *ren, TTF_Font *f, const char *s, int x, int y,
                 SDL_Color c, int anchor)
{
    if (!f || !s || !*s) return;
    SDL_Surface *surf = TTF_RenderUTF8_Blended(f, s, c);
    if (!surf) return;
    SDL_Texture *t = SDL_CreateTextureFromSurface(ren, surf);
    int w = surf->w, h = surf->h;
    SDL_FreeSurface(surf);
    int dx = anchor == 2 ? x - w : anchor == 1 ? x - w / 2 : x;
    SDL_RenderCopy(ren, t, NULL, &(SDL_Rect){dx, y, w, h});
    SDL_DestroyTexture(t);
}

static int draw_hint(SDL_Renderer *r, int x, int y, const char *token,
                     const char *label)
{
    int th = TTF_FontHeight(g_font_s);
    int chipH = th + 8;
    int tw = text_w(g_font_s, token);
    int chipW = tw + 16; if (chipW < chipH) chipW = chipH;
    fill_round(r, x, y, chipW, chipH, chipH / 2, C_CHIP);
    text(r, g_font_s, token, x + chipW / 2, y + 4, C_WHITE, 1);
    int lx = x + chipW + 8;
    text(r, g_font_s, label, lx, y + 4, C_GRAY, 0);
    return lx + text_w(g_font_s, label) + 24;
}

/* ---- scroll arrow (procedural, rounded, anti-aliased) -------------- */

/* which side of line (x1,y1)-(x2,y2) the point (px,py) is on (signed area) */
static float side(float px, float py, float x1, float y1, float x2, float y2)
{
    return (x2-x1)*(py-y1) - (y2-y1)*(px-x1);
}

/* One rounded corner at vertex V (neighbours P, Q), radius r: returns the arc
 * centre O and the two tangent points (the chord that caps the corner). */
static void corner(float vx, float vy, float px, float py, float qx, float qy,
                   float r, float *ox, float *oy,
                   float *t1x, float *t1y, float *t2x, float *t2y)
{
    float u1x = px-vx, u1y = py-vy, l1 = sqrtf(u1x*u1x+u1y*u1y);
    float u2x = qx-vx, u2y = qy-vy, l2 = sqrtf(u2x*u2x+u2y*u2y);
    u1x /= l1; u1y /= l1; u2x /= l2; u2y /= l2;
    float ca = u1x*u2x + u1y*u2y;
    if (ca > 1) ca = 1; if (ca < -1) ca = -1;
    float half = acosf(ca) / 2.0f;
    float s = sinf(half), t = tanf(half);
    float bxx = u1x+u2x, byy = u1y+u2y, bl = sqrtf(bxx*bxx+byy*byy);
    bxx /= bl; byy /= bl;                       /* inward bisector */
    *ox = vx + bxx*(r/s); *oy = vy + byy*(r/s);
    *t1x = vx + u1x*(r/t); *t1y = vy + u1y*(r/t);
    *t2x = vx + u2x*(r/t); *t2y = vy + u2y*(r/t);
}

/* Procedurally render NextUI's rounded scroll chevron. The apex corner uses a
 * larger radius than the two base corners. Anti-aliased via supersampling. */
static SDL_Texture *gen_arrow(SDL_Renderer *ren, int up)
{
    const int W = 80, H = 22, SS = 4;
    const float rApex = 6.0f, rBase = 3.0f;
    float vx[3], vy[3], r[3] = {rApex, rBase, rBase};  /* [0]=apex [1],[2]=base */
    if (up) { vx[0]=W/2.0f; vy[0]=0; vx[1]=0; vy[1]=H; vx[2]=W; vy[2]=H; }
    else    { vx[0]=W/2.0f; vy[0]=H; vx[1]=0; vy[1]=0; vx[2]=W; vy[2]=0; }
    float ox[3], oy[3], t1x[3], t1y[3], t2x[3], t2y[3];
    corner(vx[0],vy[0], vx[1],vy[1], vx[2],vy[2], r[0], &ox[0],&oy[0],&t1x[0],&t1y[0],&t2x[0],&t2y[0]);
    corner(vx[1],vy[1], vx[0],vy[0], vx[2],vy[2], r[1], &ox[1],&oy[1],&t1x[1],&t1y[1],&t2x[1],&t2y[1]);
    corner(vx[2],vy[2], vx[0],vy[0], vx[1],vy[1], r[2], &ox[2],&oy[2],&t1x[2],&t1y[2],&t2x[2],&t2y[2]);

    unsigned char *buf = malloc(W * H * 4);
    if (!buf) return NULL;
    for (int py = 0; py < H; py++)
        for (int px = 0; px < W; px++) {
            int hit = 0;
            for (int sy = 0; sy < SS; sy++)
                for (int sx = 0; sx < SS; sx++) {
                    float fx = px + (sx+0.5f)/SS, fy = py + (sy+0.5f)/SS;
                    int in =
                        side(fx,fy, vx[0],vy[0], vx[1],vy[1]) * side(vx[2],vy[2], vx[0],vy[0], vx[1],vy[1]) >= 0 &&
                        side(fx,fy, vx[1],vy[1], vx[2],vy[2]) * side(vx[0],vy[0], vx[1],vy[1], vx[2],vy[2]) >= 0 &&
                        side(fx,fy, vx[2],vy[2], vx[0],vy[0]) * side(vx[1],vy[1], vx[2],vy[2], vx[0],vy[0]) >= 0;
                    if (!in) continue;
                    int cut = 0;
                    for (int k = 0; k < 3; k++) {       /* trim corner tips to the arc */
                        float sv = side(fx,fy, t1x[k],t1y[k], t2x[k],t2y[k]);
                        float vv = side(vx[k],vy[k], t1x[k],t1y[k], t2x[k],t2y[k]);
                        if (sv*vv > 0) {                /* on the vertex side of chord */
                            float dx = fx-ox[k], dy = fy-oy[k];
                            if (dx*dx+dy*dy > r[k]*r[k]) { cut = 1; break; }
                        }
                    }
                    if (!cut) hit++;
                }
            unsigned char a = (unsigned char)((float)hit / (SS*SS) * 255.0f + 0.5f);
            int i = (py*W + px) * 4;
            buf[i] = 255; buf[i+1] = 255; buf[i+2] = 255; buf[i+3] = a;
        }
    SDL_Texture *t = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ABGR8888,
                                       SDL_TEXTUREACCESS_STATIC, W, H);
    if (t) {
        SDL_UpdateTexture(t, NULL, buf, W * 4);
        SDL_SetTextureBlendMode(t, SDL_BLENDMODE_BLEND);
    }
    free(buf);
    return t;
}

void ui_load_arrows(SDL_Renderer *r)
{
    g_arrow_up = gen_arrow(r, 1);
    g_arrow_down = gen_arrow(r, 0);
    g_arrow_w = 80; g_arrow_h = 22;
    if (g_arrow_up)   SDL_SetTextureColorMod(g_arrow_up, C_ARROW.r, C_ARROW.g, C_ARROW.b);
    if (g_arrow_down) SDL_SetTextureColorMod(g_arrow_down, C_ARROW.r, C_ARROW.g, C_ARROW.b);
}

/* ---- fonts --------------------------------------------------------- */

int ui_load_fonts(void)
{
    if (!TTF_WasInit()) TTF_Init();
    const char *paths[] = {"res/font.ttf", "font.ttf", NULL};
    const char *p = NULL;
    for (int i = 0; paths[i]; i++) {
        FILE *f = fopen(paths[i], "rb");
        if (f) { fclose(f); p = paths[i]; break; }
    }
    if (!p) { SDL_Log("font not found"); return -1; }
    g_font_l = TTF_OpenFont(p, 30);
    g_font_m = TTF_OpenFont(p, 22);
    g_font_s = TTF_OpenFont(p, 20);
    return (g_font_l && g_font_m && g_font_s) ? 0 : -1;
}

void ui_close_fonts(void)
{
    TTF_CloseFont(g_font_l); g_font_l = NULL;
    TTF_CloseFont(g_font_m); g_font_m = NULL;
    TTF_CloseFont(g_font_s); g_font_s = NULL;
}

/* ---- screens ------------------------------------------------------- */

static void render_header(SDL_Renderer *ren, App *a)
{
    text(ren, g_font_l, "Ambience", 40, 26, C_WHITE, 0);
    char buf[24];
    time_t t = time(NULL);
    struct tm *lt = localtime(&t);
    snprintf(buf, sizeof buf, "%02d:%02d", lt->tm_hour, lt->tm_min);
    int clockW = text_w(g_font_m, buf);
    text(ren, g_font_m, buf, UI_W - 40, 30, C_GRAY, 2);

    int rightX = UI_W - 40 - clockW - 20;        /* next slot, right-anchored */

    int bpct, bchg;
    sys_battery(&bpct, &bchg);
    if (bpct >= 0) {
        char bb[16];
        snprintf(bb, sizeof bb, "%d%%%s", bpct, bchg ? "+" : "");
        SDL_Color bc = bpct <= 10 ? (SDL_Color){235, 70, 70, 255}    /* critical: red    */
                     : bpct <= 20 ? (SDL_Color){240, 160, 40, 255}   /* low: orange      */
                     : bchg       ? g_accent                         /* charging         */
                                  : C_GRAY;
        text(ren, g_font_m, bb, rightX, 30, bc, 2);
        rightX -= text_w(g_font_m, bb) + 20;
    }

    int vol = sys_volume();
    if (vol >= 0) {
        char vb[16];
        snprintf(vb, sizeof vb, "VOL %d%%", vol);
        text(ren, g_font_m, vb, rightX, 30, C_GRAY, 2);
    }
    if (a->paused)
        text(ren, g_font_m, "PAUSED", UI_W / 2, 30, g_accent, 1);
    if (a->sleep_left >= 0) {
        snprintf(buf, sizeof buf, "SLEEP %d:%02d",
                 (int)a->sleep_left / 60, (int)a->sleep_left % 60);
        text(ren, g_font_s, buf, UI_W - 40, 66, g_accent, 2);
    }
}

static void render_tabs(SDL_Renderer *ren, App *a)
{
    const int ty = TAB_Y, h = TAB_H, margin = 32, gap = 8;
    const int view_w = UI_W - 2 * margin;     /* visible tab strip width */

    /* measure each tab and its content-space x (relative to the left margin) */
    char lbl[MAXPRESET][20];
    int w[MAXPRESET], xs[MAXPRESET], total = 0;
    for (int i = 0; i < a->npreset; i++) {
        ui_make_disp(a->presets[i].name, lbl[i], sizeof lbl[i]);
        w[i] = text_w(g_font_m, lbl[i]) + 28;
        xs[i] = total;
        total += w[i] + gap;
    }
    if (a->npreset > 0) total -= gap;

    /* target scroll: keep the active tab fully in view */
    static float scroll = 0; static Uint32 last = 0;
    float target = scroll;
    if (total > view_w) {
        int ax = xs[a->active], aw = w[a->active];
        if (ax - target < 0) target = ax;                              /* off the left  */
        else if (ax + aw - target > view_w) target = ax + aw - view_w; /* off the right */
        float maxs = (float)(total - view_w);
        if (target < 0) target = 0;
        if (target > maxs) target = maxs;
    } else target = 0;

    /* time-based easing toward the target offset (snap on the first frame) */
    Uint32 now = SDL_GetTicks();
    if (!last) { scroll = target; last = now; }
    else {
        float dt = (now - last) / 1000.0f; last = now;
        if (dt > 0.1f) dt = 0.1f;
        scroll += (target - scroll) * (1.0f - expf(-dt * 14.0f));
        if (fabsf(target - scroll) < 0.5f) scroll = target;
    }

    SDL_Rect clip = { 0, ty, UI_W, h };       /* keep tabs on their own row */
    SDL_RenderSetClipRect(ren, &clip);
    int tyt = ty + h / 2 - TTF_FontHeight(g_font_m) / 2;
    for (int i = 0; i < a->npreset; i++) {
        int tx = margin + xs[i] - (int)(scroll + 0.5f);
        if (tx + w[i] < 0 || tx > UI_W) continue;   /* fully scrolled out */
        int act = (i == a->active);
        fill_round(ren, tx, ty, w[i], h, h / 2, act ? g_accent : C_CHIP);
        text(ren, g_font_m, lbl[i], tx + w[i] / 2, tyt, act ? C_WHITE : C_GRAY, 1);
    }
    SDL_RenderSetClipRect(ren, NULL);
}

void ui_render(SDL_Renderer *ren, App *a)
{
    set_color(ren, C_BG);
    SDL_RenderClear(ren);
    render_header(ren, a);
    render_tabs(ren, a);

    if (a->nch == 0) {
        text(ren, g_font_l, "No sounds found", UI_W / 2, UI_H / 2 - 50, C_WHITE, 1);
        text(ren, g_font_m, "Put .ogg, .wav or .mp3 files in res/sounds",
             UI_W / 2, UI_H / 2 + 6, C_GRAY, 1);
        draw_hint(ren, 40, HINT_Y, "B", "EXIT");
        return;
    }

    const int PH = ROW_H, pitch = ROW_PITCH, top = LIST_TOP;   /* 5 rows; roomy arrow gaps */
    const int px = 32, pw = UI_W - 64;
    const int lh = TTF_FontHeight(g_font_l);
    const int mh = TTF_FontHeight(g_font_m);
    const int padX = 24;

    int vis = (UI_H - 96 - top) / pitch;
    if (vis < 1) vis = 1;
    if (a->sel < a->scroll) a->scroll = a->sel;
    if (a->sel >= a->scroll + vis) a->scroll = a->sel - vis + 1;
    if (a->scroll < 0) a->scroll = 0;

    int maxLabelW = 0;
    for (int i = 0; i < a->nch; i++) {
        int w = text_w(g_font_l, a->ch[i].disp);
        if (w > maxLabelW) maxLabelW = w;
    }
    int barX = px + maxLabelW + padX * 2 + 20;
    int pctW = text_w(g_font_m, "100%");
    int pctR = px + pw - 8;
    int barEnd = pctR - pctW - 24;
    int barW = barEnd - barX; if (barW < 60) barW = 60;
    int barH = 16;

    int last = a->scroll + vis; if (last > a->nch) last = a->nch;
    for (int i = a->scroll; i < last; i++) {
        Channel *c = &a->ch[i];
        int sel = (i == a->sel);
        int y = top + (i - a->scroll) * pitch;
        int midy = y + PH / 2;

        if (sel) {
            int nw = text_w(g_font_l, c->disp);
            fill_round(ren, px, y, nw + padX * 2, PH, PH / 2, C_WHITE);
        }
        SDL_Color name_c = sel ? C_DARK : (c->muted ? C_MUTE : C_WHITE);
        text(ren, g_font_l, c->disp, px + padX, midy - lh / 2, name_c, 0);

        int barY = midy - barH / 2;
        fill_round(ren, barX, barY, barW, barH, barH / 2, C_TRACK);
        float lvl = c->muted ? c->saved : c->target;   /* show level even when muted */
        if (lvl > 0.0f) {
            int fw = (int)(barW * lvl);
            if (fw < barH) fw = barH;
            SDL_Color fillc = c->muted ? col_mix(g_accent, C_BG, 0.45f) : g_accent;
            fill_round(ren, barX, barY, fw, barH, barH / 2, fillc);
        }
        char buf[12];
        if (c->muted) snprintf(buf, sizeof buf, "MUTED");
        else          snprintf(buf, sizeof buf, "%d%%", (int)(c->target * 100 + 0.5f));
        text(ren, g_font_m, buf, pctR, midy - mh / 2, c->muted ? C_MUTE : C_GRAY, 2);
    }

    /* scroll indicators: tucked close to the list (not mid-gap) so they read as
     * part of it, while still clearing the tabs above and hint bar below */
    const int list_bottom = top + (vis - 1) * pitch + PH;
    const int arrow_gap = 18;                     /* distance from the list edge */
    if (a->scroll > 0) {
        int ay = top - arrow_gap - g_arrow_h;
        if (g_arrow_up)
            SDL_RenderCopy(ren, g_arrow_up, NULL,
                &(SDL_Rect){UI_W / 2 - g_arrow_w / 2, ay, g_arrow_w, g_arrow_h});
        else tri(ren, UI_W / 2, ay, 24, 12, 1, C_GRAY);
    }
    if (last < a->nch) {
        int ay = list_bottom + arrow_gap;
        if (g_arrow_down)
            SDL_RenderCopy(ren, g_arrow_down, NULL,
                &(SDL_Rect){UI_W / 2 - g_arrow_w / 2, ay, g_arrow_w, g_arrow_h});
        else tri(ren, UI_W / 2, ay, 24, 12, 0, C_GRAY);
    }

    int hx = 40, hy = HINT_Y;
    hx = draw_hint(ren, hx, hy, "D-PAD", "ADJUST");
    hx = draw_hint(ren, hx, hy, "A", "MUTE");
    hx = draw_hint(ren, hx, hy, "L R", "PRESET");
    hx = draw_hint(ren, hx, hy, "Y", "MENU");
    hx = draw_hint(ren, hx, hy, "X", "PAUSE");
    hx = draw_hint(ren, hx, hy, "B", "EXIT");
    (void)hx;
}

void ui_render_confirm(SDL_Renderer *ren)
{
    SDL_SetRenderDrawColor(ren, 0, 0, 0, 160);
    SDL_RenderFillRect(ren, &(SDL_Rect){0, 0, UI_W, UI_H});
    int w = 560, h = 208;
    int x = (UI_W - w) / 2, y = (UI_H - h) / 2;
    fill_round(ren, x, y, w, h, 28, (SDL_Color){30, 32, 40, 255});
    text(ren, g_font_l, "Quit Ambience?", UI_W / 2, y + 56, C_WHITE, 1);
    int th = TTF_FontHeight(g_font_s), chipH = th + 8;
    int aw = text_w(g_font_s, "A") + 16; if (aw < chipH) aw = chipH;
    int bw = text_w(g_font_s, "B") + 16; if (bw < chipH) bw = chipH;
    int a_adv = aw + 8 + text_w(g_font_s, "QUIT") + 24;
    int b_adv = bw + 8 + text_w(g_font_s, "CANCEL") + 24;
    int hx = UI_W / 2 - (a_adv + b_adv - 24) / 2;
    int hy = y + h - 72;
    hx = draw_hint(ren, hx, hy, "A", "QUIT");
    draw_hint(ren, hx, hy, "B", "CANCEL");
}

/* ---- modal: preset menu -------------------------------------------- */

int ui_preset_menu(SDL_Renderer *ren, App *a)
{
    const char *items[] = {"New preset", "Rename preset", "Delete preset"};
    int nit = (a->npreset > 1) ? 3 : 2;     /* delete only when >1 preset */
    int sel = 0, done = 0, ret = UI_PM_CANCEL;
    SDL_PumpEvents();                        /* drop the press that opened us */
    SDL_FlushEvent(SDL_KEYDOWN);
    SDL_FlushEvent(SDL_CONTROLLERBUTTONDOWN);
    while (!done) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            int act = 0;
            if (e.type == SDL_QUIT) { done = 1; }
            else if (e.type == SDL_KEYDOWN && e.key.repeat == 0) {
                switch (e.key.keysym.sym) {
                case SDLK_UP: sel = (sel + nit - 1) % nit; break;
                case SDLK_DOWN: sel = (sel + 1) % nit; break;
                case SDLK_RETURN: act = 1; break;
                case SDLK_ESCAPE: done = 1; break;
                }
            } else if (e.type == SDL_CONTROLLERBUTTONDOWN) {
                switch (e.cbutton.button) {
                case SDL_CONTROLLER_BUTTON_DPAD_UP: sel = (sel + nit - 1) % nit; break;
                case SDL_CONTROLLER_BUTTON_DPAD_DOWN: sel = (sel + 1) % nit; break;
                case SDL_CONTROLLER_BUTTON_B: act = 1; break;      /* physical A */
                case SDL_CONTROLLER_BUTTON_A: done = 1; break;     /* physical B */
                }
            }
            if (act) { ret = sel; done = 1; }       /* sel maps to UI_PM_* */
        }
        ui_render(ren, a);
        SDL_SetRenderDrawColor(ren, 0, 0, 0, 160);
        SDL_RenderFillRect(ren, &(SDL_Rect){0, 0, UI_W, UI_H});
        int w = 460, rowh = 64, h = 96 + nit * rowh;
        int x = (UI_W - w) / 2, y = (UI_H - h) / 2;
        fill_round(ren, x, y, w, h, 24, (SDL_Color){30, 32, 40, 255});
        text(ren, g_font_l, "Menu", UI_W / 2, y + 28, C_WHITE, 1);
        int lh = TTF_FontHeight(g_font_l);
        for (int i = 0; i < nit; i++) {
            int ry = y + 84 + i * rowh;
            if (i == sel) fill_round(ren, x + 24, ry, w - 48, rowh - 12, (rowh - 12) / 2, C_WHITE);
            text(ren, g_font_l, items[i], UI_W / 2, ry + (rowh - 12) / 2 - lh / 2,
                 i == sel ? C_DARK : C_WHITE, 1);
        }
        SDL_RenderPresent(ren);
        SDL_Delay(16);
    }
    return ret;
}


/* ---- on-screen keyboard ---------------------------------------------
 * The keyboard is a standalone, reusable module (keyboard.c / keyboard.h).
 * These thin wrappers feed it this app's fonts and accent colour. */
static OskConfig ui_osk_cfg(void)
{
    OskConfig c;
    c.font_title = g_font_l;
    c.font_key   = g_font_m;
    c.font_hint  = g_font_s;
    c.accent     = g_accent;
    c.max_chars  = 16;          /* preset-name limit */
    return c;
}

int ui_keyboard(SDL_Renderer *ren, const char *title, char *buf, int bufsz)
{
    OskConfig c = ui_osk_cfg();
    return osk_run(ren, &c, title, buf, bufsz);
}

void ui_keyboard_demo(SDL_Renderer *ren)
{
    OskConfig c = ui_osk_cfg();
    osk_demo(ren, &c);
}
