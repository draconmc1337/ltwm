#include "ltwm.h"

/* forward – defined in config.c */
void safe_strncpy(char *d, const char *s, size_t n);

/* ── helpers ────────────────────────────────────────────── */
static void send_wm_event(WM *wm, Client *c, Atom proto) {
    XEvent ev = {0};
    ev.type                 = ClientMessage;
    ev.xclient.window       = c->win;
    ev.xclient.message_type = wm->atom_wm_protocols;
    ev.xclient.format       = 32;
    ev.xclient.data.l[0]    = (long)proto;
    ev.xclient.data.l[1]    = CurrentTime;
    XSendEvent(wm->dpy, c->win, False, NoEventMask, &ev);
}

static bool has_wm_protocol(WM *wm, Client *c, Atom proto) {
    Atom *prots; int n; bool found = false;
    if (XGetWMProtocols(wm->dpy, c->win, &prots, &n)) {
        for (int i = 0; i < n; i++) if (prots[i] == proto) { found = true; break; }
        XFree(prots);
    }
    return found;
}

/* ── detect if window should default to floating ────────── */
bool client_wants_float(WM *wm, Client *c) {
    /* check _NET_WM_WINDOW_TYPE */
    Atom type; int fmt; unsigned long n, after;
    unsigned char *data = NULL;
    if (XGetWindowProperty(wm->dpy, c->win, wm->atom_net_wm_window_type,
                           0, 1, False, XA_ATOM,
                           &type, &fmt, &n, &after, &data) == Success && data) {
        Atom t = *(Atom *)data;
        XFree(data);
        if (t == wm->atom_net_wm_window_type_dialog  ||
            t == wm->atom_net_wm_window_type_utility ||
            t == wm->atom_net_wm_window_type_splash)
            return true;
    }
    /* check WM_TRANSIENT_FOR */
    Window trans = None;
    if (XGetTransientForHint(wm->dpy, c->win, &trans) && trans != None)
        return true;
    /* check size hints for fixed size */
    XSizeHints hints; long supplied;
    if (XGetWMNormalHints(wm->dpy, c->win, &hints, &supplied)) {
        if ((hints.flags & PMinSize) && (hints.flags & PMaxSize) &&
            hints.min_width  == hints.max_width &&
            hints.min_height == hints.max_height) {
            c->fixed = true;
            return true;
        }
    }
    return false;
}

/* ── allocate new client ────────────────────────────────── */
Client *client_new(WM *wm, Window win) {
    Client *c = calloc(1, sizeof(Client));
    if (!c) die("client_new: calloc");
    c->win       = win;
    c->workspace = wm->cur_ws;

    XWindowAttributes wa;
    if (XGetWindowAttributes(wm->dpy, win, &wa)) {
        c->x = wa.x; c->y = wa.y;
        c->w = wa.width; c->h = wa.height;
    }

    /* pick decent default float size */
    int dfw = wm->cfg.float_default_w ? wm->cfg.float_default_w : wm->sw / 2;
    int dfh = wm->cfg.float_default_h ? wm->cfg.float_default_h : wm->sh / 2;
    c->saved_w = dfw; c->saved_h = dfh;
    c->saved_x = (wm->sw - dfw) / 2;
    c->saved_y = (wm->sh - dfh) / 2;

    client_update_title(wm, c);

    if (client_wants_float(wm, c))
        c->floating = true;

    return c;
}

/* ── create frame and reparent ──────────────────────────── */
void client_frame(WM *wm, Client *c) {
    int bw = wm->cfg.border_width;
    int bh = 0;

    c->fx = c->x - bw;
    c->fy = c->y - bw;
    c->fw = c->w + 2*bw;
    c->fh = c->h + 2*bw;

    unsigned long border_col = wm->cfg.border_inactive;

    XSetWindowAttributes swa;
    swa.border_pixel     = border_col;
    swa.background_pixel = BlackPixel(wm->dpy, wm->screen);
    swa.event_mask       = SubstructureRedirectMask | SubstructureNotifyMask |
                           ButtonPressMask | ButtonReleaseMask |
                           PointerMotionMask | EnterWindowMask | ExposureMask;

    c->frame = XCreateWindow(
        wm->dpy, wm->root,
        c->fx, c->fy, c->fw, c->fh, bw,
        DefaultDepth(wm->dpy, wm->screen),
        InputOutput,
        DefaultVisual(wm->dpy, wm->screen),
        CWBorderPixel | CWBackPixel | CWEventMask, &swa);

    XAddToSaveSet(wm->dpy, c->win);
    XReparentWindow(wm->dpy, c->win, c->frame, bw, bw);
    XMapWindow(wm->dpy, c->frame);
    /* watch enter events on client window for sloppy focus */
    XSelectInput(wm->dpy, c->win, EnterWindowMask | PropertyChangeMask);

    /* grab mod+button on the FRAME window only
       frame is our window — app cannot interfere with it */
    unsigned int mods[] = {0, LockMask, Mod2Mask, LockMask|Mod2Mask};
    for (int btn = 1; btn <= 3; btn++)
        for (int m = 0; m < 4; m++)
            XGrabButton(wm->dpy, btn, wm->cfg.mainMod | mods[m],
                        c->frame, False,
                        ButtonPressMask|ButtonReleaseMask|PointerMotionMask,
                        GrabModeAsync, GrabModeAsync, None, None);

    client_update_border(wm, c);
}

/* ── remove frame, reparent back to root ────────────────── */
void client_unframe(WM *wm, Client *c) {
    if (!c->frame) return;
    Window frame = c->frame;
    c->frame = None;  /* clear first to prevent re-entry */
    XUnmapWindow(wm->dpy, frame);
    /* reparent client back to root before destroying frame */
    XReparentWindow(wm->dpy, c->win, wm->root, c->x, c->y);
    XRemoveFromSaveSet(wm->dpy, c->win);
    XDestroyWindow(wm->dpy, frame);
}

/* ── focus ──────────────────────────────────────────────── */
static void client_focus_impl(WM *wm, Client *c, bool do_raise) {
    Workspace *ws = &wm->workspaces[wm->cur_ws];

    /* un-highlight old focus */
    if (ws->focused && ws->focused != c)
        client_update_border(wm, ws->focused);

    ws->focused = c;
    if (!c) {
        XSetInputFocus(wm->dpy, wm->root, RevertToPointerRoot, CurrentTime);
        return;
    }

    XSetInputFocus(wm->dpy, c->win, RevertToPointerRoot, CurrentTime);
    /* raise floating/fullscreen chỉ khi được yêu cầu (click), không raise khi lia chuột */
    if (do_raise && (c->floating || c->fullscreen))
        XRaiseWindow(wm->dpy, c->frame);
    client_update_border(wm, c);

    /* WM_TAKE_FOCUS */
    if (has_wm_protocol(wm, c, wm->atom_wm_take_focus))
        send_wm_event(wm, c, wm->atom_wm_take_focus);

    /* _NET_ACTIVE_WINDOW */
    XChangeProperty(wm->dpy, wm->root, wm->atom_net_active_window,
                    XA_WINDOW, 32, PropModeReplace,
                    (unsigned char *)&c->win, 1);

    /* notify ltwm-bar of new focused title */
    char buf[MAX_NAME_LEN + 8];
    snprintf(buf, sizeof(buf), "title %s", c->title);
    ipc_event_emit(wm, buf);

}

/* focus + raise (dùng khi click, switch workspace, map window mới) */
void client_focus(WM *wm, Client *c) {
    client_focus_impl(wm, c, true);
}

/* focus only, KHÔNG raise — dùng cho EnterNotify (lia chuột)
   floating window phía sau vẫn active nhưng không nhảy lên trên */
void client_focus_no_raise(WM *wm, Client *c) {
    client_focus_impl(wm, c, false);
}

/* ── kill window ────────────────────────────────────────── */
void client_kill(WM *wm, Client *c) {
    if (!c) return;
    if (has_wm_protocol(wm, c, wm->atom_wm_delete))
        send_wm_event(wm, c, wm->atom_wm_delete);
    else
        XKillClient(wm->dpy, c->win);
}

/* ── set floating state ─────────────────────────────────── */
void client_set_floating(WM *wm, Client *c, bool floating) {
    if (!c || c->floating == floating) return;
    int bh = 0;

    if (floating) {
        /* save current tiled geometry */
        c->saved_x = c->x; c->saved_y = c->y;
        c->saved_w = c->w; c->saved_h = c->h;
        c->floating = true;

        /* place centred at a reasonable size */
        int fw = wm->cfg.float_default_w ? wm->cfg.float_default_w : (int)(wm->sw * 0.6);
        int fh = wm->cfg.float_default_h ? wm->cfg.float_default_h : (int)(wm->sh * 0.6);
        int fx = (wm->sw - fw) / 2;
        int fy = bh + (wm->sh - bh - fh) / 2;
        client_move_resize(wm, c, fx, fy, fw, fh);
        XRaiseWindow(wm->dpy, c->frame);
    } else {
        c->floating = false;
        /* tiling algo will reposition */
    }

    client_update_border(wm, c);
    ws_tile(wm, c->workspace);
}

void client_toggle_float(WM *wm, Client *c) {
    if (!c) return;
    client_set_floating(wm, c, !c->floating);
}

/* ── fullscreen ─────────────────────────────────────────── */
void client_toggle_fullscreen(WM *wm, Client *c) {
    if (!c) return;
    int bh = 0;

    if (!c->fullscreen) {
        c->saved_x = c->x; c->saved_y = c->y;
        c->saved_w = c->w; c->saved_h = c->h;
        c->fullscreen = true;

        XSetWindowBorderWidth(wm->dpy, c->frame, 0);
        XMoveResizeWindow(wm->dpy, c->frame, 0, bh, wm->sw, wm->sh - bh);
        XMoveResizeWindow(wm->dpy, c->win,   0,  0, wm->sw, wm->sh - bh);
        XRaiseWindow(wm->dpy, c->frame);

        /* advertise via EWMH */
        XChangeProperty(wm->dpy, c->win, wm->atom_net_wm_state, XA_ATOM, 32,
                        PropModeReplace,
                        (unsigned char *)&wm->atom_net_wm_state_fullscreen, 1);
    } else {
        c->fullscreen = false;
        XSetWindowBorderWidth(wm->dpy, c->frame, wm->cfg.border_width);
        XDeleteProperty(wm->dpy, c->win, wm->atom_net_wm_state);
        ws_tile(wm, c->workspace);
    }
}

/* ── move + resize (frame and client together) ──────────── */
void client_move_resize(WM *wm, Client *c, int x, int y, int w, int h) {
    int bw = wm->cfg.border_width;
    if (w < 40) w = 40;
    if (h < 40) h = 40;
    c->x = x; c->y = y; c->w = w; c->h = h;
    c->fx = x - bw; c->fy = y - bw;
    c->fw = w + 2*bw; c->fh = h + 2*bw;
    XMoveResizeWindow(wm->dpy, c->frame, c->fx, c->fy, (unsigned)c->fw, (unsigned)c->fh);
    XMoveResizeWindow(wm->dpy, c->win,   bw,    bw,    (unsigned)c->w,  (unsigned)c->h);
}

void client_center_on_screen(WM *wm, Client *c) {
    int bh = 0;
    int cx = (wm->sw - c->w) / 2;
    int cy = bh + (wm->sh - bh - c->h) / 2;
    client_move_resize(wm, c, cx, cy, c->w, c->h);
}

/* ── update title ───────────────────────────────────────── */
void client_update_title(WM *wm, Client *c) {
    XTextProperty tp;
    if (XGetTextProperty(wm->dpy, c->win, &tp, wm->atom_net_wm_name) && tp.value) {
        safe_strncpy(c->title, (char *)tp.value, MAX_NAME_LEN);
        XFree(tp.value); return;
    }
    if (XGetWMName(wm->dpy, c->win, &tp) && tp.value) {
        safe_strncpy(c->title, (char *)tp.value, MAX_NAME_LEN);
        XFree(tp.value); return;
    }
    safe_strncpy(c->title, "[no name]", MAX_NAME_LEN);
}

/* ── update border colour based on state ────────────────── */
void client_update_border(WM *wm, Client *c) {
    if (!c || !c->frame) return;
    /* use the client's own workspace to check focus —
       avoids wrong color when ws_tile is called on a non-current workspace */
    Workspace *ws = &wm->workspaces[c->workspace];
    bool is_active = (ws->focused == c) && (c->workspace == wm->cur_ws);
    unsigned long col = is_active ? wm->cfg.border_active : wm->cfg.border_inactive;
    XSetWindowBorder(wm->dpy, c->frame, col);
    XSetWindowBorderWidth(wm->dpy, c->frame,
                          c->fullscreen ? 0 : (unsigned)wm->cfg.border_width);
}

/* ── linked-list helpers ────────────────────────────────── */
void client_add(WM *wm, Client *c) {
    Workspace *ws = &wm->workspaces[c->workspace];
    c->next = ws->clients;
    c->prev = NULL;
    if (ws->clients) ws->clients->prev = c;
    ws->clients = c;
}

void client_remove(WM *wm, Client *c) {
    Workspace *ws = &wm->workspaces[c->workspace];
    if (c->prev) c->prev->next = c->next;
    else         ws->clients   = c->next;
    if (c->next) c->next->prev = c->prev;
    if (ws->focused == c)
        ws->focused = ws->clients;
}

Client *client_find_by_win(WM *wm, Window win) {
    for (int i = 0; i < MAX_WORKSPACES; i++)
        for (Client *c = wm->workspaces[i].clients; c; c = c->next)
            if (c->win == win) return c;
    return NULL;
}

Client *client_find_by_frame(WM *wm, Window frame) {
    for (int i = 0; i < MAX_WORKSPACES; i++)
        for (Client *c = wm->workspaces[i].clients; c; c = c->next)
            if (c->frame == frame) return c;
    return NULL;
}

/* ── get master (first tiled) client on a workspace ─────── */
Client *client_get_master(WM *wm, int ws_id) {
    for (Client *c = wm->workspaces[ws_id].clients; c; c = c->next)
        if (!c->floating && !c->fullscreen) return c;
    return NULL;
}

/* ── swap focused with master ───────────────────────────── */
void client_swap_with_master(WM *wm, Client *c) {
    if (!c || c->floating || c->fullscreen) return;
    Client *master = client_get_master(wm, wm->cur_ws);
    if (!master || master == c) return;

    /* swap positions in linked list */
    /* simplest: exchange win+frame pointers & metadata */
    Window tw = c->win;     c->win = master->win;         master->win = tw;
    Window tf = c->frame;   c->frame = master->frame;     master->frame = tf;
    char tmp[MAX_NAME_LEN]; memcpy(tmp, c->title, MAX_NAME_LEN);
    memcpy(c->title, master->title, MAX_NAME_LEN);
    memcpy(master->title, tmp, MAX_NAME_LEN);

    /* re-grab buttons on swapped frames */
    for (int btn = 1; btn <= 3; btn++) {
        XGrabButton(wm->dpy, btn, wm->cfg.mainMod, c->frame, False,
                    ButtonPressMask|ButtonReleaseMask|PointerMotionMask,
                    GrabModeAsync, GrabModeAsync, None, None);
        XGrabButton(wm->dpy, btn, wm->cfg.mainMod, master->frame, False,
                    ButtonPressMask|ButtonReleaseMask|PointerMotionMask,
                    GrabModeAsync, GrabModeAsync, None, None);
    }
    ws_tile(wm, wm->cur_ws);
    client_focus(wm, master);
}
