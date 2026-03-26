#include "ltwm.h"
#include <X11/Xft/Xft.h>
#include <time.h>

static XftFont  *bar_font_xft = NULL;
static XftDraw  *bar_draw_xft = NULL;
static XftColor  bar_col_cache[64];
static int       bar_col_n = 0;

/* ── parse hex → XftColor, cached ──────────────────────── */
static XftColor *xft_color(WM *wm, const char *hex) {
    char name[16];
    snprintf(name, sizeof(name), "%s%s", hex[0]=='#'?"":"#", hex[0]=='#'?hex+1:hex);
    /* check cache */
    for (int i = 0; i < bar_col_n; i++)
        if (!memcmp(bar_col_cache[i].color.red ? name : "", name, 1)) {}
    /* just allocate fresh — small bar, not perf critical */
    if (bar_col_n >= 64) bar_col_n = 0;
    XftColorAllocName(wm->dpy,
                      DefaultVisual(wm->dpy, wm->screen),
                      DefaultColormap(wm->dpy, wm->screen),
                      name, &bar_col_cache[bar_col_n]);
    return &bar_col_cache[bar_col_n++];
}

/* ── text width via Xft ─────────────────────────────────── */
static int xft_tw(const char *s) {
    if (!bar_font_xft || !s || !*s) return strlen(s) * 8;
    XGlyphInfo ext;
    XftTextExtentsUtf8(NULL, bar_font_xft, (FcChar8*)s, strlen(s), &ext);
    return ext.xOff;
}

/* hacky but necessary — store display for xft_tw */
static Display *g_dpy = NULL;
static int xft_tw2(Display *dpy, const char *s) {
    if (!bar_font_xft || !s || !*s) return strlen(s)*8;
    XGlyphInfo ext;
    XftTextExtentsUtf8(dpy, bar_font_xft, (FcChar8*)s, strlen(s), &ext);
    return ext.xOff;
}

/* ── create bar ─────────────────────────────────────────── */
void bar_create(WM *wm) {
    if (!wm->cfg.bar_enabled) return;
    g_dpy = wm->dpy;
    int bh = wm->cfg.bar_height;

    XSetWindowAttributes swa;
    swa.background_pixel  = wm->cfg.bar_bg;
    swa.override_redirect = True;
    swa.event_mask        = ExposureMask | ButtonPressMask;

    wm->bar_win = XCreateWindow(
        wm->dpy, wm->root, 0, 0, wm->sw, bh, 0,
        DefaultDepth(wm->dpy, wm->screen), InputOutput,
        DefaultVisual(wm->dpy, wm->screen),
        CWBackPixel | CWOverrideRedirect | CWEventMask, &swa);

    /* load Xft font — supports "IosevkaNerdFont:size=12" format */
    if (bar_font_xft) { XftFontClose(wm->dpy, bar_font_xft); bar_font_xft=NULL; }
    bar_font_xft = XftFontOpenName(wm->dpy, wm->screen, wm->cfg.bar_font);
    if (!bar_font_xft)
        bar_font_xft = XftFontOpenName(wm->dpy, wm->screen, "monospace:size=10");

    /* Xft draw on bar window */
    if (bar_draw_xft) { XftDrawDestroy(bar_draw_xft); bar_draw_xft=NULL; }
    bar_draw_xft = XftDrawCreate(wm->dpy, wm->bar_win,
                                 DefaultVisual(wm->dpy, wm->screen),
                                 DefaultColormap(wm->dpy, wm->screen));

    /* GC for fill rects */
    wm->bar_gc = XCreateGC(wm->dpy, wm->bar_win, 0, NULL);

    XMapRaised(wm->dpy, wm->bar_win);
    wm->bar_visible = true;
    memset(wm->bar_cmd_cache, 0, sizeof(wm->bar_cmd_cache));
    memset(wm->bar_cmd_last,  0, sizeof(wm->bar_cmd_last));
}

void bar_destroy(WM *wm) {
    if (!wm->bar_win) return;
    if (bar_draw_xft)   { XftDrawDestroy(bar_draw_xft);    bar_draw_xft=NULL; }
    if (bar_font_xft)   { XftFontClose(wm->dpy,bar_font_xft); bar_font_xft=NULL; }
    XFreeGC(wm->dpy, wm->bar_gc);
    XDestroyWindow(wm->dpy, wm->bar_win);
    wm->bar_win = None;
    wm->bar_visible = false;
}

/* ── fill rect ──────────────────────────────────────────── */
static void bfill(WM *wm, int x, int y, int w, int h, unsigned long col) {
    XSetForeground(wm->dpy, wm->bar_gc, col);
    XFillRectangle(wm->dpy, wm->bar_win, wm->bar_gc,
                   x, y, (unsigned)w, (unsigned)h);
}

/* ── draw text with Xft, return right edge ──────────────── */
static int btext(WM *wm, int x, const char *s,
                 const char *col_fg, const char *col_bg, int padx) {
    if (!s || !*s) return x;
    int bh  = wm->cfg.bar_height;
    int w   = xft_tw2(wm->dpy, s) + 2*padx;
    int asc = bar_font_xft ? bar_font_xft->ascent  : 12;
    int dsc = bar_font_xft ? bar_font_xft->descent : 2;
    int ty  = (bh - asc - dsc)/2 + asc;

    if (col_bg) {
        unsigned long bg = parse_color(wm->dpy, wm->screen, col_bg);
        bfill(wm, x, 0, w, bh, bg);
    }
    if (bar_draw_xft && bar_font_xft && col_fg) {
        XftColor *c = xft_color(wm, col_fg);
        XftDrawStringUtf8(bar_draw_xft, c, bar_font_xft,
                          x + padx, ty, (FcChar8*)s, strlen(s));
    }
    return x + w;
}

/* ── update command cache ────────────────────────────────── */
void bar_update_cmds(WM *wm) {
    time_t now = time(NULL);
    for (int i = 0; i < wm->cfg.bar_n_cmds; i++) {
        if (now - wm->bar_cmd_last[i] < 1) continue;
        FILE *f = popen(wm->cfg.bar_cmds[i].cmd, "r");
        if (!f) { wm->bar_cmd_cache[i][0]='\0'; continue; }
        if (!fgets(wm->bar_cmd_cache[i], 255, f))
            wm->bar_cmd_cache[i][0]='\0';
        else {
            char *nl = strchr(wm->bar_cmd_cache[i],'\n');
            if (nl) *nl='\0';
        }
        pclose(f);
        wm->bar_cmd_last[i] = now;
    }
}

/* ── handle button click on bar ─────────────────────────── */
void bar_handle_click(WM *wm, int x) {
    /* workspace label click */
    int cx = wm->cfg.bar_padding_x;
    if (wm->cfg.bar_show_workspaces) {
        for (int i = 0; i < MAX_WORKSPACES; i++) {
            Workspace *ws = &wm->workspaces[i];
            bool active   = (i == wm->cur_ws);
            bool occupied = (ws->clients != NULL);
            if (!active && !occupied) continue;
            int w = xft_tw2(wm->dpy, ws->name) + 14 + 1;
            if (x >= cx && x < cx + w) { ws_switch(wm, i); return; }
            cx += w;
        }
    }
    /* button label click */
    if (wm->cfg.bar_show_btns) {
        int rx = wm->sw - wm->cfg.bar_padding_x;
        for (int i = wm->cfg.bar_n_btns-1; i >= 0; i--) {
            int w = xft_tw2(wm->dpy, wm->cfg.bar_btns[i].icon) + 18;
            rx -= w;
            if (x >= rx && x < rx + w) { spawn(wm->cfg.bar_btns[i].cmd); return; }
        }
    }
}

/* ── main draw ──────────────────────────────────────────── */
void bar_draw(WM *wm) {
    if (!wm->bar_visible || !wm->bar_win || !bar_draw_xft) return;
    int bh = wm->cfg.bar_height;
    int sw = wm->sw;
    int px = wm->cfg.bar_padding_x;

    bar_update_cmds(wm);

    /* background */
    bfill(wm, 0, 0, sw, bh, wm->cfg.bar_bg);

    /* fg/bg as hex strings */
    char sfg[16], sbg[16], sa_bg[16], sa_fg[16], so_bg[16], so_fg[16];
    snprintf(sfg,   sizeof(sfg),   "#%06lx", wm->cfg.bar_fg   & 0xFFFFFF);
    snprintf(sbg,   sizeof(sbg),   "#%06lx", wm->cfg.bar_bg   & 0xFFFFFF);
    snprintf(sa_bg, sizeof(sa_bg), "#%06lx", wm->cfg.bar_ws_active_bg   & 0xFFFFFF);
    snprintf(sa_fg, sizeof(sa_fg), "#%06lx", wm->cfg.bar_ws_active_fg   & 0xFFFFFF);
    snprintf(so_bg, sizeof(so_bg), "#%06lx", wm->cfg.bar_ws_occupied_bg & 0xFFFFFF);
    snprintf(so_fg, sizeof(so_fg), "#%06lx", wm->cfg.bar_ws_occupied_fg & 0xFFFFFF);

    int lx = px;
    int rx = sw - px;

    /* ── LEFT: workspaces ─────────────────────────────── */
    if (wm->cfg.bar_show_workspaces) {
        for (int i = 0; i < MAX_WORKSPACES; i++) {
            Workspace *ws = &wm->workspaces[i];
            bool active   = (i == wm->cur_ws);
            bool occupied = (ws->clients != NULL);
            if (!active && !occupied) continue;
            char *bg = active ? sa_bg : so_bg;
            char *fg = active ? sa_fg : so_fg;
            lx = btext(wm, lx, ws->name, fg, bg, 7);
            lx += 1;
        }
        lx += 4;
    }

    /* ── LEFT: layout ─────────────────────────────────── */
    if (wm->cfg.bar_show_layout) {
        const char *li[] = {"[]=","[v]","|||","===","[M]","><>"};
        int l = (int)wm->workspaces[wm->cur_ws].layout;
        if (l < 0 || l >= LAYOUT_COUNT) l = 0;
        lx = btext(wm, lx, li[l], sfg, NULL, 4);
        lx += 6;
    }

    /* ── RIGHT: version ───────────────────────────────── */
    if (wm->cfg.bar_show_version) {
        const char *ver = "ltwm 0.10.0-alpha";
        int vw = xft_tw2(wm->dpy, ver) + 16;
        rx -= vw;
        btext(wm, rx, ver, sfg, NULL, 8);
    }

    /* ── RIGHT: buttons ───────────────────────────────── */
    if (wm->cfg.bar_show_btns) {
        for (int i = wm->cfg.bar_n_btns-1; i >= 0; i--) {
            BarBtn *b = &wm->cfg.bar_btns[i];
            int bw = xft_tw2(wm->dpy, b->icon) + 16;
            rx -= bw + 2;
            char cbg[16];
            snprintf(cbg, sizeof(cbg), "#%06lx", b->bg & 0xFFFFFF);
            btext(wm, rx, b->icon, sfg, b->bg ? cbg : NULL, 8);
        }
        rx -= 4;
    }

    /* ── RIGHT: commands ──────────────────────────────── */
    if (wm->cfg.bar_show_cmds) {
        /* measure total width first */
        int total = 0;
        for (int i = 0; i < wm->cfg.bar_n_cmds; i++) {
            if (!wm->bar_cmd_cache[i][0]) continue;
            if (wm->cfg.bar_cmds[i].sep[0] && i > 0)
                total += xft_tw2(wm->dpy, wm->cfg.bar_cmds[i].sep);
            total += xft_tw2(wm->dpy, wm->bar_cmd_cache[i]);
        }
        int cx = rx - total - 16;
        if (cx < lx + 8) cx = lx + 8;
        rx = cx - 8;

        for (int i = 0; i < wm->cfg.bar_n_cmds; i++) {
            if (!wm->bar_cmd_cache[i][0]) continue;
            if (wm->cfg.bar_cmds[i].sep[0] && i > 0)
                cx = btext(wm, cx, wm->cfg.bar_cmds[i].sep, sfg, NULL, 0);
            char cfg_fg[16];
            snprintf(cfg_fg, sizeof(cfg_fg), "#%06lx",
                     wm->cfg.bar_cmds[i].fg ? wm->cfg.bar_cmds[i].fg & 0xFFFFFF
                                            : wm->cfg.bar_fg & 0xFFFFFF);
            char cfg_bg[16];
            bool has_bg = wm->cfg.bar_cmds[i].bg != 0;
            snprintf(cfg_bg, sizeof(cfg_bg), "#%06lx", wm->cfg.bar_cmds[i].bg & 0xFFFFFF);
            cx = btext(wm, cx, wm->bar_cmd_cache[i],
                       cfg_fg, has_bg ? cfg_bg : NULL, 0);
        }
    }

    /* ── CENTER: title ────────────────────────────────── */
    {
        Client *f = wm->workspaces[wm->cur_ws].focused;
        if (f && f->title[0]) {
            char ttl[MAX_NAME_LEN+16];
            if      (f->floating)    snprintf(ttl,sizeof(ttl),"[float] %s",f->title);
            else if (f->fullscreen)  snprintf(ttl,sizeof(ttl),"[full] %s", f->title);
            else                     snprintf(ttl,sizeof(ttl),"%s",        f->title);
            int w  = xft_tw2(wm->dpy, ttl);
            int cx = (sw - w) / 2;
            if (cx > lx+8 && cx+w < rx-8)
                btext(wm, cx, ttl, sfg, NULL, 0);
        }
    }

    XFlush(wm->dpy);
}
