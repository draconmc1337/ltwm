#include "ltwm.h"

void ws_tile(WM *wm, int id) {
    update_struts(wm);

    Workspace *ws = &wm->workspaces[id];
    int gap = wm->cfg.gap;
    int bw  = wm->cfg.border_width;
    int ox  = wm->strut_left;
    int oy  = wm->strut_top;
    int sw  = wm->sw - wm->strut_left - wm->strut_right;
    int sh  = wm->sh - wm->strut_top  - wm->strut_bottom;

    Client *tiled[MAX_CLIENTS];
    int n = 0;
    for (Client *c = ws->clients; c; c = c->next)
        if (!c->floating && !c->fullscreen && c->workspace == id)
            tiled[n++] = c;

    switch (ws->layout) {

    case LAYOUT_MONOCLE:
        for (int i = 0; i < n; i++) {
            if (ws->focused == tiled[i])
                client_move_resize(wm, tiled[i],
                    ox + gap, oy + gap,
                    sw - 2*gap - 2*bw,
                    sh - 2*gap - 2*bw);
            else
                XMoveWindow(wm->dpy, tiled[i]->frame, -sw*2, 0);
        }
        break;

    case LAYOUT_FLOAT:
        break;

    case LAYOUT_TILE:
    default:
        if (n == 0) break;
        {
            int mc = (ws->master_count < n) ? ws->master_count : n;
            if (mc == 0) mc = 1;
            int sc = n - mc;
            float ratio = ws->master_ratio;

            if (sc == 0) {
                int uw = sw - 2*gap - 2*bw;
                int uh = (sh - (mc+1)*gap) / mc - 2*bw;
                for (int i = 0; i < mc; i++)
                    client_move_resize(wm, tiled[i],
                        ox + gap,
                        oy + gap + i*(uh + gap + 2*bw),
                        uw, uh);
            } else {
                int mw  = (int)((float)(sw - 3*gap) * ratio) - 2*bw;
                int sw2 = sw - mw - 3*gap - 2*bw;
                int sx  = gap + mw + gap + 2*bw;

                int mh  = (sh - (mc+1)*gap) / mc - 2*bw;
                for (int i = 0; i < mc; i++)
                    client_move_resize(wm, tiled[i],
                        ox + gap,
                        oy + gap + i*(mh + gap + 2*bw),
                        mw, mh);

                int seh = (sh - (sc+1)*gap) / sc - 2*bw;
                for (int i = 0; i < sc; i++)
                    client_move_resize(wm, tiled[mc+i],
                        ox + sx,
                        oy + gap + i*(seh + gap + 2*bw),
                        sw2, seh);
            }
        }
        break;
    }

    for (Client *c = ws->clients; c; c = c->next)
        if (c->floating && !c->fullscreen && c->workspace == id)
            XRaiseWindow(wm->dpy, c->frame);

    if (ws->focused && ws->focused->workspace == id)
        XRaiseWindow(wm->dpy, ws->focused->frame);

    for (Client *c = ws->clients; c; c = c->next)
        if (c->workspace == id)
            client_update_border(wm, c);
}

void ws_switch(WM *wm, int id) {
    if (id < 0 || id >= MAX_WORKSPACES || id == wm->cur_ws) return;

    for (Client *c = wm->workspaces[wm->cur_ws].clients; c; c = c->next) {
        c->ignore_unmap++;
        XUnmapWindow(wm->dpy, c->frame);
    }

    wm->cur_ws = id;

    for (Client *c = wm->workspaces[id].clients; c; c = c->next)
        XMapWindow(wm->dpy, c->frame);

    ws_tile(wm, id);

    Client *focus = wm->workspaces[id].focused
                    ? wm->workspaces[id].focused
                    : wm->workspaces[id].clients;
    client_focus(wm, focus);

    /* notify polybar / subscribers */
    char evtbuf[64];
    snprintf(evtbuf, sizeof(evtbuf), "workspace %d", id+1);
    ipc_event_emit(wm, evtbuf);
}

void ws_move_client(WM *wm, Client *c, int id) {
    if (!c || id < 0 || id >= MAX_WORKSPACES || c->workspace == id) return;

    int old_ws = c->workspace;
    client_remove(wm, c);
    c->ignore_unmap++;
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
        int tx = c->fx + c->fw/2, ty = c->fy + c->fh/2;
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
    ws->layout = (ws->layout + 1) % LAYOUT_COUNT;
    ws_tile(wm, wm->cur_ws);
}
