#include "ltwm.h"

/* ── helpers ────────────────────────────────────────────── */
static int usable_x(WM *wm)  { return wm->strut_left; }
static int usable_y(WM *wm)  { return wm->strut_top; }
static int usable_w(WM *wm)  { return wm->sw - wm->strut_left - wm->strut_right; }
static int usable_h(WM *wm)  { return wm->sh - wm->strut_top  - wm->strut_bottom; }

/* ── collect tiled clients for a workspace ──────────────── */
static int collect_tiled(WM *wm, int id, Client **out) {
    int n = 0;
    for (Client *c = wm->workspaces[id].clients; c; c = c->next)
        if (!c->floating && !c->fullscreen && c->workspace == id)
            out[n++] = c;
    return n;
}

/* ══════════════════════════════════════════════════════════
 *  LAYOUTS
 * ══════════════════════════════════════════════════════════ */

/* ── LAYOUT_TILE: horizontal master+stack ───────────────── */
static void layout_tile(WM *wm, int id, Client **t, int n) {
    int gap = wm->cfg.gap, bw = wm->cfg.border_width;
    int ox = usable_x(wm), oy = usable_y(wm);
    int sw = usable_w(wm), sh = usable_h(wm);
    Workspace *ws = &wm->workspaces[id];

    int mc = (ws->master_count < n) ? ws->master_count : n;
    if (mc == 0) mc = 1;
    int sc = n - mc;
    float ratio = ws->master_ratio;

    if (sc == 0) {
        int uw = sw - 2*gap - 2*bw;
        int uh = (sh - (mc+1)*gap) / mc - 2*bw;
        for (int i = 0; i < mc; i++)
            client_move_resize(wm, t[i],
                ox+gap, oy+gap + i*(uh+gap+2*bw), uw, uh);
    } else {
        int mw  = (int)((float)(sw-3*gap) * ratio) - 2*bw;
        int sw2 = sw - mw - 3*gap - 2*bw;
        int sx  = gap + mw + gap + 2*bw;
        int mh  = (sh-(mc+1)*gap)/mc - 2*bw;
        for (int i = 0; i < mc; i++)
            client_move_resize(wm, t[i],
                ox+gap, oy+gap + i*(mh+gap+2*bw), mw, mh);
        int seh = sc > 0 ? (sh-(sc+1)*gap)/sc - 2*bw : sh-2*gap-2*bw;
        for (int i = 0; i < sc; i++)
            client_move_resize(wm, t[mc+i],
                ox+sx, oy+gap + i*(seh+gap+2*bw), sw2, seh);
    }
}

/* ── LAYOUT_VTILE: vertical master+stack ────────────────── */
static void layout_vtile(WM *wm, int id, Client **t, int n) {
    int gap = wm->cfg.gap, bw = wm->cfg.border_width;
    int ox = usable_x(wm), oy = usable_y(wm);
    int sw = usable_w(wm), sh = usable_h(wm);
    Workspace *ws = &wm->workspaces[id];

    int mc = (ws->master_count < n) ? ws->master_count : n;
    if (mc == 0) mc = 1;
    int sc = n - mc;
    float ratio = ws->master_ratio;

    if (sc == 0) {
        int uw = (sw-(mc+1)*gap)/mc - 2*bw;
        int uh = sh - 2*gap - 2*bw;
        for (int i = 0; i < mc; i++)
            client_move_resize(wm, t[i],
                ox+gap + i*(uw+gap+2*bw), oy+gap, uw, uh);
    } else {
        /* master row on top, stack row on bottom */
        int mh  = (int)((float)(sh-3*gap) * ratio) - 2*bw;
        int sh2 = sh - mh - 3*gap - 2*bw;
        int sy  = gap + mh + gap + 2*bw;
        int mw  = (sw-(mc+1)*gap)/mc - 2*bw;
        for (int i = 0; i < mc; i++)
            client_move_resize(wm, t[i],
                ox+gap + i*(mw+gap+2*bw), oy+gap, mw, mh);
        int sew = sc > 0 ? (sw-(sc+1)*gap)/sc - 2*bw : sw-2*gap-2*bw;
        for (int i = 0; i < sc; i++)
            client_move_resize(wm, t[mc+i],
                ox+gap + i*(sew+gap+2*bw), oy+sy, sew, sh2);
    }
}

/* ── LAYOUT_VSTRIPES: equal vertical columns ────────────── */
static void layout_vstripes(WM *wm, int id, Client **t, int n) {
    if (n <= 0) return;
    int gap = wm->cfg.gap, bw = wm->cfg.border_width;
    int ox = usable_x(wm), oy = usable_y(wm);
    int sw = usable_w(wm), sh = usable_h(wm);
    int cw = (sw - (n+1)*gap) / n - 2*bw;
    int ch = sh - 2*gap - 2*bw;
    if (cw < 1) cw = 1;
    for (int i = 0; i < n; i++)
        client_move_resize(wm, t[i],
            ox+gap + i*(cw+gap+2*bw), oy+gap, cw, ch);
}

/* ── LAYOUT_HSTRIPES: equal horizontal rows ─────────────── */
static void layout_hstripes(WM *wm, int id, Client **t, int n) {
    if (n <= 0) return;
    int gap = wm->cfg.gap, bw = wm->cfg.border_width;
    int ox = usable_x(wm), oy = usable_y(wm);
    int sw = usable_w(wm), sh = usable_h(wm);
    int cw = sw - 2*gap - 2*bw;
    int ch = (sh - (n+1)*gap) / n - 2*bw;
    if (ch < 1) ch = 1;
    for (int i = 0; i < n; i++)
        client_move_resize(wm, t[i],
            ox+gap, oy+gap + i*(ch+gap+2*bw), cw, ch);
}

/* ── LAYOUT_MONOCLE: one window at a time ───────────────── */
static void layout_monocle(WM *wm, int id, Client **t, int n) {
    int gap = wm->cfg.gap, bw = wm->cfg.border_width;
    int ox = usable_x(wm), oy = usable_y(wm);
    int sw = usable_w(wm), sh = usable_h(wm);
    Workspace *ws = &wm->workspaces[id];
    for (int i = 0; i < n; i++) {
        if (ws->focused == t[i])
            client_move_resize(wm, t[i],
                ox+gap, oy+gap,
                sw-2*gap-2*bw, sh-2*gap-2*bw);
        else
            XMoveWindow(wm->dpy, t[i]->frame, -sw*2, 0);
    }
}

/* ══════════════════════════════════════════════════════════
 *  ws_tile — main entry point
 * ══════════════════════════════════════════════════════════ */
void ws_tile(WM *wm, int id) {
    Workspace *ws = &wm->workspaces[id];
    Client *tiled[MAX_CLIENTS];
    int n = collect_tiled(wm, id, tiled);

    if (n > 0) {
        switch (ws->layout) {
        case LAYOUT_TILE:     layout_tile(wm, id, tiled, n);     break;
        case LAYOUT_VTILE:    layout_vtile(wm, id, tiled, n);    break;
        case LAYOUT_VSTRIPES: layout_vstripes(wm, id, tiled, n); break;
        case LAYOUT_HSTRIPES: layout_hstripes(wm, id, tiled, n); break;
        case LAYOUT_MONOCLE:  layout_monocle(wm, id, tiled, n);  break;
        case LAYOUT_FLOAT:
        default: break;
        }
    }

    /* floating always on top of tiled */
    for (Client *c = ws->clients; c; c = c->next)
        if (c->floating && !c->fullscreen && c->workspace == id)
            XRaiseWindow(wm->dpy, c->frame);

    if (ws->focused && ws->focused->workspace == id &&
        (ws->focused->floating || ws->focused->fullscreen))
        XRaiseWindow(wm->dpy, ws->focused->frame);

    for (Client *c = ws->clients; c; c = c->next)
        if (c->workspace == id)
            client_update_border(wm, c);
}

/* ══════════════════════════════════════════════════════════
 *  workspace operations
 * ══════════════════════════════════════════════════════════ */
void ws_switch(WM *wm, int id) {
    if (id < 0 || id >= MAX_WORKSPACES || id == wm->cur_ws) return;

    int old = wm->cur_ws;
    wm->cur_ws = id;

    /* unmap old */
    for (Client *c = wm->workspaces[old].clients; c; c = c->next)
        if (c->frame) XUnmapWindow(wm->dpy, c->frame);

    /* tile new first so windows have correct geometry before mapping */
    ws_tile(wm, id);

    /* map new */
    for (Client *c = wm->workspaces[id].clients; c; c = c->next)
        if (c->frame) XMapWindow(wm->dpy, c->frame);

    /* flush once — avoid multiple round trips */
    XSync(wm->dpy, False);

    Client *focus = wm->workspaces[id].focused
                    ? wm->workspaces[id].focused
                    : wm->workspaces[id].clients;
    client_focus(wm, focus);

    char evtbuf[64];
    snprintf(evtbuf, sizeof(evtbuf), "workspace %d", id+1);
    ipc_event_emit(wm, evtbuf);
}

void ws_move_client(WM *wm, Client *c, int id) {
    if (!c || id < 0 || id >= MAX_WORKSPACES || c->workspace == id) return;

    int old_ws = c->workspace;
    client_remove(wm, c);
    XUnmapWindow(wm->dpy, c->frame);
    c->workspace = id;
    client_add(wm, c);

    ws_tile(wm, old_ws);
    ws_tile(wm, id);

    Client *focus = wm->workspaces[old_ws].focused
                    ? wm->workspaces[old_ws].focused
                    : wm->workspaces[old_ws].clients;
    client_focus(wm, focus);
}

void ws_focus_dir(WM *wm, int dir) {
    Workspace *ws = &wm->workspaces[wm->cur_ws];
    if (!ws->clients) return;
    if (!ws->focused) { client_focus(wm, ws->clients); return; }

    Client *best = NULL;
    int bscore   = INT32_MAX;
    int cx = ws->focused->fx + ws->focused->fw/2;
    int cy = ws->focused->fy + ws->focused->fh/2;

    for (Client *c = ws->clients; c; c = c->next) {
        if (c == ws->focused || c->workspace != wm->cur_ws) continue;
        int tx = c->fx+c->fw/2, ty = c->fy+c->fh/2;
        int dx = tx-cx, dy = ty-cy, score = INT32_MAX;
        switch (dir) {
        case 0: if(dx<0) score=-dx+abs(dy); break;
        case 1: if(dx>0) score= dx+abs(dy); break;
        case 2: if(dy<0) score=-dy+abs(dx); break;
        case 3: if(dy>0) score= dy+abs(dx); break;
        }
        if (score < bscore) { bscore=score; best=c; }
    }
    if (best) client_focus(wm, best);
}

void ws_cycle_layout(WM *wm) {
    Workspace *ws = &wm->workspaces[wm->cur_ws];
    int next = ((int)ws->layout + 1) % (int)LAYOUT_COUNT;
    ws->layout = (Layout)next;
    ws_tile(wm, wm->cur_ws);

    /* emit layout name for polybar */
    const char *names[] = {"tile","vtile","vstripes","hstripes","monocle","float"};
    char buf[64];
    snprintf(buf, sizeof(buf), "layout %s", names[ws->layout]);
    ipc_event_emit(wm, buf);
}
