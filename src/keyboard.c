#include "keyboard.h"
#include <stdio.h>
#include <string.h>
#include <math.h>

/* ---- self-contained dark palette (NextUI-flavoured) ----------------- */
static const SDL_Color KC_BG    = { 17,  18,  22, 255};
static const SDL_Color KC_WHITE = {236, 238, 240, 255};
static const SDL_Color KC_DARK  = { 24,  26,  30, 255};
static const SDL_Color KC_GRAY  = {150, 154, 164, 255};
static const SDL_Color KC_CHIP  = { 40,  42,  50, 255};

/* ---- per-run config (modal + single-threaded -> file scope is fine) - */
static TTF_Font *F_TITLE, *F_KEY, *F_HINT;
static SDL_Color ACCENT;
static int MAXC;          /* max code points, <= 0 = unlimited */
static int VW, VH;        /* canvas size, read from the renderer */

static void osk_begin(SDL_Renderer *ren, const OskConfig *cfg)
{
    F_TITLE = cfg->font_title;
    F_KEY   = cfg->font_key;
    F_HINT  = cfg->font_hint;
    ACCENT  = cfg->accent;
    MAXC    = cfg->max_chars;
    SDL_GetRendererOutputSize(ren, &VW, &VH);
    if (VW <= 0 || VH <= 0) { VW = 1024; VH = 768; }
}

/* ---- drawing primitives -------------------------------------------- */

static void set_color(SDL_Renderer *r, SDL_Color c)
{
    SDL_SetRenderDrawColor(r, c.r, c.g, c.b, c.a);
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
    int th = TTF_FontHeight(F_HINT);
    int chipH = th + 8;
    int tw = text_w(F_HINT, token);
    int chipW = tw + 16; if (chipW < chipH) chipW = chipH;
    fill_round(r, x, y, chipW, chipH, chipH / 2, KC_CHIP);
    text(r, F_HINT, token, x + chipW / 2, y + 4, KC_WHITE, 1);
    int lx = x + chipW + 8;
    text(r, F_HINT, label, lx, y + 4, KC_GRAY, 0);
    return lx + text_w(F_HINT, label) + 24;
}

/* ---- UTF-8 helpers -------------------------------------------------- */

static int utf8_count(const char *s)
{ int n = 0; for (; *s; s++) if (((unsigned char)*s & 0xC0) != 0x80) n++; return n; }

static void utf8_pop(char *buf, int *len)   /* drop the last code point */
{
    if (*len <= 0) return;
    int i = *len - 1;
    while (i > 0 && ((unsigned char)buf[i] & 0xC0) == 0x80) i--;
    buf[i] = 0; *len = i;
}

static int over_limit(const char *buf)   /* would adding one more char overflow? */
{ return MAXC > 0 && utf8_count(buf) >= MAXC; }

/* ---- key layout (iPhone-style English: ABC / 123 / #+=) ------------- */

/* Single/short strings are character keys; the named keys
 * Shift/Del/Space/Done/123/ABC/#+= are commands. */
static const char *const KB[3][4][12] = {
    { /* 0: ABC */
        {"q", "w", "e", "r", "t", "y", "u", "i", "o", "p", NULL},
        {"a", "s", "d", "f", "g", "h", "j", "k", "l", NULL},
        {"Shift", "z", "x", "c", "v", "b", "n", "m", "Del", NULL},
        {"123", "Space", "Done", NULL},
    },
    { /* 1: 123  (US layout: $ on this page, currencies on #+=) */
        {"1", "2", "3", "4", "5", "6", "7", "8", "9", "0", NULL},
        {"-", "/", ":", ";", "(", ")", "$", "&", "@", "\"", NULL},
        {"#+=", ".", ",", "?", "!", "'", "Del", NULL},
        {"ABC", "Space", "Done", NULL},
    },
    { /* 2: #+= */
        {"[", "]", "{", "}", "#", "%", "^", "*", "+", "=", NULL},
        {"_", "\\", "|", "~", "<", ">", "\xe2\x82\xac", "\xc2\xa3", "\xc2\xa5", "\xe2\x80\xa2", NULL},
        {"123", ".", ",", "?", "!", "'", "Del", NULL},
        {"ABC", "Space", "Done", NULL},
    },
};
#define KB_NROW 4
#define KB_G    10   /* gap between keys (must match kb_render) */

/* named keys are commands; everything else is a character to insert */
static int kb_is_cmd(const char *k)
{
    return !strcmp(k, "Shift") || !strcmp(k, "Del") || !strcmp(k, "Space") ||
           !strcmp(k, "Done")  || !strcmp(k, "123") || !strcmp(k, "ABC")  ||
           !strcmp(k, "#+=");
}

static int kb_rowlen(int page, int r) { int n = 0; while (KB[page][r][n]) n++; return n; }

static int kb_keyw(const char *k)
{
    if (!kb_is_cmd(k)) return 64;                 /* any character (incl. multibyte) */
    if (!strcmp(k, "Space")) return 380;
    return text_w(F_KEY, k) + 36;
}

/* on-screen centre x of a key, matching kb_render's centred row layout */
static int kb_key_cx(int page, int r, int c)
{
    int n = kb_rowlen(page, r), kw[12], roww = 0;
    for (int i = 0; i < n; i++) { kw[i] = kb_keyw(KB[page][r][i]); roww += kw[i] + (i ? KB_G : 0); }
    int kx = (VW - roww) / 2;
    for (int i = 0; i < c; i++) kx += kw[i] + KB_G;
    return kx + kw[c] / 2;
}

/* move the selection (nav: 0 up, 1 down, 2 left, 3 right) */
static void kb_move(int page, int nav, int *row, int *col)
{
    if (nav == 2 || nav == 3) {                   /* left / right within the row */
        int n = kb_rowlen(page, *row);
        *col = (nav == 2) ? (*col + n - 1) % n : (*col + 1) % n;
        return;
    }
    /* up / down: jump to the key in the next row whose horizontal *span*
     * (not just its centre) is nearest the current key's centre, so a wide key
     * like Space captures any key sitting directly above it. */
    int cx = kb_key_cx(page, *row, *col);
    int nr = (nav == 0) ? (*row + KB_NROW - 1) % KB_NROW : (*row + 1) % KB_NROW;
    int n = kb_rowlen(page, nr), best = 0, bestd = 1 << 30;
    for (int c = 0; c < n; c++) {
        int half = kb_keyw(KB[page][nr][c]) / 2;
        int d = kb_key_cx(page, nr, c) - cx; if (d < 0) d = -d;
        d = (d <= half) ? 0 : d - half;   /* 0 when cx is within the key's span */
        if (d < bestd) { bestd = d; best = c; }
    }
    *row = nr; *col = best;
}

/* shift states: off, one-shot (next char), caps lock (double-tap) */
enum { SH_OFF, SH_ONCE, SH_LOCK };
#define KB_DBLTAP 350   /* ms window for shift double-tap -> caps lock */

/* Apply a key. Returns: 0 none/shift, 1 char/space/del (repeatable on hold),
 * 2 done, 3 page ABC, 4 page 123, 5 page #+=. Updates buf/len/shift. */
static int kb_apply(const char *k, char *buf, int *len, int bufsz,
                    int *shift, Uint32 *shift_t)
{
    if (!kb_is_cmd(k)) {                          /* character key (maybe multibyte) */
        char tmp[8]; int kl = (int)strlen(k);
        if (kl == 1) {
            char c = k[0];
            if (*shift != SH_OFF && c >= 'a' && c <= 'z') c -= 32;
            tmp[0] = c; tmp[1] = 0;
        } else { memcpy(tmp, k, kl); tmp[kl] = 0; }
        if (!over_limit(buf) && *len + kl < bufsz) {
            memcpy(buf + *len, tmp, kl); *len += kl; buf[*len] = 0;
            if (*shift == SH_ONCE) *shift = SH_OFF;   /* one-shot consumed */
        }
        return 1;
    }
    if (!strcmp(k, "Shift")) {
        Uint32 now = SDL_GetTicks();
        if (*shift == SH_LOCK) *shift = SH_OFF;
        else if (*shift == SH_ONCE) *shift = (now - *shift_t < KB_DBLTAP) ? SH_LOCK : SH_OFF;
        else *shift = SH_ONCE;
        *shift_t = now;
        return 0;
    }
    if (!strcmp(k, "Space")) {
        if (!over_limit(buf) && *len + 1 < bufsz) { buf[(*len)++] = ' '; buf[*len] = 0; }
        return 1;
    }
    if (!strcmp(k, "Del"))  { utf8_pop(buf, len); return 1; }
    if (!strcmp(k, "Done")) return 2;
    if (!strcmp(k, "ABC"))  return 3;
    if (!strcmp(k, "123"))  return 4;
    if (!strcmp(k, "#+="))  return 5;
    return 0;
}

/* repeat a held action (1 select current key, 2 del, 3 space); 1 if repeatable */
static int kb_hold(int kind, int page, int row, int col, char *buf, int *len, int bufsz,
                   int *shift, Uint32 *shift_t)
{
    const char *k = (kind == 2) ? "Del" : (kind == 3) ? "Space" : KB[page][row][col];
    return kb_apply(k, buf, len, bufsz, shift, shift_t) == 1;
}

static void kb_render(SDL_Renderer *ren, const char *title, const char *buf,
                      int page, int row, int col, int shift)
{
    set_color(ren, KC_BG);
    SDL_RenderClear(ren);

    /* --- vertical layout -------------------------------------------------
     * The keyboard is pinned to the bottom (just above the hint row) and its
     * height is derived from the key metrics, so adding/removing key rows
     * never needs manual y-tuning. The input field sits centred in the gap
     * between the title and the keyboard, free to grow downward (e.g. a future
     * multi-line field) without shifting the keyboard. */
    const int ch = 56, g = KB_G;                       /* key cell height / gap */
    const int title_y = 56;
    const int hint_y  = VH - 56;   /* match the main screen's hint bar */
    const int kb_gap  = 64;                            /* breathing room below the keyboard */
    const int kb_h    = KB_NROW * ch + (KB_NROW - 1) * g;
    const int ky      = hint_y - kb_gap - kb_h;        /* keyboard top, bottom-anchored */

    const int lh = TTF_FontHeight(F_TITLE);
    const int title_bot = title_y + lh;
    const int fw = 760, fx = (VW - fw) / 2;
    const int fh = 56;
    const int fy = title_bot + ((ky - title_bot) - fh) / 2;  /* centred in the gap */

    text(ren, F_TITLE, title, VW / 2, title_y, KC_WHITE, 1);
    fill_round(ren, fx, fy, fw, fh, 12, KC_CHIP);
    char shown[600];
    snprintf(shown, sizeof shown, "%s_", buf);
    text(ren, F_TITLE, shown, fx + 20, fy + fh / 2 - lh / 2, KC_WHITE, 0);
    if (MAXC > 0) {
        char cnt[16];
        snprintf(cnt, sizeof cnt, "%d/%d", utf8_count(buf), MAXC);
        text(ren, F_HINT, cnt, fx + fw - 16, fy + fh / 2 - TTF_FontHeight(F_HINT) / 2,
             KC_GRAY, 2);
    }

    for (int r = 0; r < KB_NROW; r++) {
        int n = kb_rowlen(page, r), roww = 0, kw[12];
        for (int c = 0; c < n; c++) { kw[c] = kb_keyw(KB[page][r][c]); roww += kw[c] + (c ? g : 0); }
        int kx = (VW - roww) / 2, yy = ky + r * (ch + g);
        for (int c = 0; c < n; c++) {
            const char *k = KB[page][r][c];
            int selk = (r == row && c == col);
            int is_shift = !strcmp(k, "Shift");
            int caps = is_shift && shift == SH_LOCK;   /* locked: solid white  */
            int once = is_shift && shift == SH_ONCE;   /* armed: white outline */
            /* the cursor takes the accent colour; Shift states use white */
            SDL_Color bg = selk ? ACCENT : caps ? KC_WHITE : KC_CHIP;
            fill_round(ren, kx, yy, kw[c], ch, 10, bg);
            if (once && !selk) {                       /* draw a white ring */
                fill_round(ren, kx, yy, kw[c], ch, 10, KC_WHITE);
                fill_round(ren, kx + 3, yy + 3, kw[c] - 6, ch - 6, 8, KC_CHIP);
            }
            char lab[2] = {0, 0};
            const char *s = k;
            if (!k[1]) { lab[0] = (shift != SH_OFF && k[0] >= 'a' && k[0] <= 'z') ? (char)(k[0] - 32) : k[0]; s = lab; }
            text(ren, F_KEY, s, kx + kw[c] / 2, yy + ch / 2 - TTF_FontHeight(F_KEY) / 2,
                 (caps && !selk) ? KC_DARK : KC_WHITE, 1);
            kx += kw[c] + g;
        }
    }

    int hx = 40, hy = VH - 56;
    hx = draw_hint(ren, hx, hy, "D-PAD", "MOVE");
    hx = draw_hint(ren, hx, hy, "A", "SELECT");
    hx = draw_hint(ren, hx, hy, "Y", "DEL");
    hx = draw_hint(ren, hx, hy, "X", "SPACE");
    hx = draw_hint(ren, hx, hy, "START", "DONE");
    hx = draw_hint(ren, hx, hy, "B", "CANCEL");
    (void)hx;
}

/* ---- public API ----------------------------------------------------- */

void osk_demo(SDL_Renderer *ren, const OskConfig *cfg)
{
    osk_begin(ren, cfg);
    kb_render(ren, "New preset", "Rainy night", 0, 2, 8, SH_OFF);   /* Del highlighted */
}

int osk_run(SDL_Renderer *ren, const OskConfig *cfg,
            const char *title, char *buf, int bufsz)
{
    osk_begin(ren, cfg);

    int cap = bufsz - 1;
    int len = (int)strlen(buf); if (len > cap) { len = cap; buf[len] = 0; }
    int page = 0, row = 0, col = 0, done = 0, ok = 0;
    int shift = SH_OFF; Uint32 shift_t = 0;
    int dheld = -1; Uint32 dsince = 0, dlast = 0;   /* d-pad move repeat        */
    int hkind = 0;  Uint32 hsince = 0, hlast = 0;   /* held action: 1 sel 2 del 3 spc */
    const Uint32 DELAY = 300, RATE = 60;
    SDL_StartTextInput();
    SDL_PumpEvents();                        /* drop the press that opened us */
    SDL_FlushEvent(SDL_KEYDOWN);
    SDL_FlushEvent(SDL_CONTROLLERBUTTONDOWN);
    SDL_FlushEvent(SDL_TEXTINPUT);
    while (!done) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) { done = 1; break; }
            if (e.type == SDL_TEXTINPUT) {
                for (const char *p = e.text.text; *p; p++)
                    if (!over_limit(buf) && len < cap &&
                        (unsigned char)*p >= 0x20 && (unsigned char)*p < 0x7f)
                        buf[len++] = *p;
                buf[len] = 0;
                continue;
            }
            int move = -1;       /* 0-3 d-pad direction pressed         */
            int moverel = 0;     /* a d-pad direction released          */
            int act = 0;         /* action pressed: 1 sel 2 del 3 space */
            int relkind = 0;     /* action released: 1 sel 2 del 3 space*/
            if (e.type == SDL_KEYDOWN && e.key.repeat == 0) {
                switch (e.key.keysym.sym) {
                case SDLK_UP: move = 0; break;
                case SDLK_DOWN: move = 1; break;
                case SDLK_LEFT: move = 2; break;
                case SDLK_RIGHT: move = 3; break;
                case SDLK_RETURN: ok = 1; done = 1; break;
                case SDLK_ESCAPE: done = 1; break;
                case SDLK_BACKSPACE: act = 2; break;
                }
            } else if (e.type == SDL_KEYUP) {
                switch (e.key.keysym.sym) {
                case SDLK_UP: case SDLK_DOWN: case SDLK_LEFT: case SDLK_RIGHT:
                    moverel = 1; break;
                case SDLK_BACKSPACE: relkind = 2; break;
                }
            } else if (e.type == SDL_CONTROLLERBUTTONDOWN) {
                switch (e.cbutton.button) {
                case SDL_CONTROLLER_BUTTON_DPAD_UP: move = 0; break;
                case SDL_CONTROLLER_BUTTON_DPAD_DOWN: move = 1; break;
                case SDL_CONTROLLER_BUTTON_DPAD_LEFT: move = 2; break;
                case SDL_CONTROLLER_BUTTON_DPAD_RIGHT: move = 3; break;
                case SDL_CONTROLLER_BUTTON_B: act = 1; break;   /* physical A: select */
                case SDL_CONTROLLER_BUTTON_A: done = 1; break;  /* physical B: cancel */
                case SDL_CONTROLLER_BUTTON_X: act = 2; break;   /* physical Y: backspace */
                case SDL_CONTROLLER_BUTTON_Y: act = 3; break;   /* physical X: space */
                case SDL_CONTROLLER_BUTTON_START: ok = 1; done = 1; break;
                }
            } else if (e.type == SDL_CONTROLLERBUTTONUP) {
                switch (e.cbutton.button) {
                case SDL_CONTROLLER_BUTTON_DPAD_UP:
                case SDL_CONTROLLER_BUTTON_DPAD_DOWN:
                case SDL_CONTROLLER_BUTTON_DPAD_LEFT:
                case SDL_CONTROLLER_BUTTON_DPAD_RIGHT: moverel = 1; break;
                case SDL_CONTROLLER_BUTTON_B: relkind = 1; break;
                case SDL_CONTROLLER_BUTTON_X: relkind = 2; break;
                case SDL_CONTROLLER_BUTTON_Y: relkind = 3; break;
                }
            }
            Uint32 t = SDL_GetTicks();
            if (moverel) dheld = -1;
            if (relkind && relkind == hkind) hkind = 0;
            if (move >= 0) {
                kb_move(page, move, &row, &col);
                dheld = move; dsince = dlast = t;
            }
            if (act == 1) {                  /* select the highlighted key */
                int r = kb_apply(KB[page][row][col], buf, &len, bufsz, &shift, &shift_t);
                if (r == 2) { ok = 1; done = 1; }
                else if (r == 1) { hkind = 1; hsince = hlast = t; }    /* repeatable */
                else if (r >= 3) {                                     /* page switch */
                    page = r - 3; shift = SH_OFF; hkind = 0;
                    int n = kb_rowlen(page, row);      /* keep cursor on the switch key */
                    if (col >= n) col = n - 1;
                }
            } else if (act == 2 || act == 3) {                        /* del / space */
                kb_apply(act == 2 ? "Del" : "Space", buf, &len, bufsz, &shift, &shift_t);
                hkind = act; hsince = hlast = t;
            }
        }

        Uint32 now = SDL_GetTicks();
        if (dheld >= 0 && now - dsince >= DELAY && now - dlast >= RATE) {
            kb_move(page, dheld, &row, &col);
            dlast = now;
        }
        if (hkind && now - hsince >= DELAY && now - hlast >= RATE) {
            if (kb_hold(hkind, page, row, col, buf, &len, bufsz, &shift, &shift_t)) hlast = now;
            else hkind = 0;
        }
        kb_render(ren, title, buf, page, row, col, shift);
        SDL_RenderPresent(ren);
        SDL_Delay(16);
    }
    SDL_StopTextInput();
    return ok;
}
