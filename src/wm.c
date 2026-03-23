#include "ltwm.h"

static WM g_wm;

static int xerror_handler(Display *dpy, XErrorEvent *e) {
    if (e->error_code == BadWindow)   return 0;
    if (e->error_code == BadMatch)    return 0;
    if (e->error_code == BadAccess)   return 0;
    if (e->request_code == X_SetInputFocus)    return 0;
    if (e->request_code == X_PolyText8)        return 0;
    if (e->request_code == X_PolyFillRectangle)return 0;
    if (e->request_code == X_PolySegment)      return 0;
    if (e->request_code == X_ConfigureWindow)  return 0;
    if (e->request_code == X_GrabButton)       return 0;
    if (e->request_code == X_GrabKey)          return 0;
    char buf[256];
    XGetErrorText(dpy, e->error_code, buf, sizeof(buf));
    fprintf(stderr, "ltwm: X error: %s (req %d)\n", buf, e->request_code);
    return 0;
}

static int xerror_checkwm(Display *dpy, XErrorEvent *e) {
    (void)dpy;
    if (e->error_code == BadAccess)
        die("ltwm: another window manager is already running");
    return 0;
}

static void sigchld_handler(int sig) {
    (void)sig;
    while (waitpid(-1, NULL, WNOHANG) > 0);
}

static void grab_keys(WM *wm) {
    /* keys handled entirely by sxhkd — release everything */
    XUngrabKey(wm->dpy, AnyKey, AnyModifier, wm->root);
}

static void cache_atoms(WM *wm) {
    wm->atom_wm_protocols   = XInternAtom(wm->dpy, "WM_PROTOCOLS", False);
    wm->atom_wm_delete      = XInternAtom(wm->dpy, "WM_DELETE_WINDOW", False);
    wm->atom_wm_take_focus  = XInternAtom(wm->dpy, "WM_TAKE_FOCUS", False);
    wm->atom_wm_name        = XInternAtom(wm->dpy, "WM_NAME", False);
    wm->atom_net_wm_name    = XInternAtom(wm->dpy, "_NET_WM_NAME", False);
    wm->atom_net_wm_state   = XInternAtom(wm->dpy, "_NET_WM_STATE", False);
    wm->atom_net_wm_state_fullscreen = XInternAtom(wm->dpy, "_NET_WM_STATE_FULLSCREEN", False);
    wm->atom_net_active_window       = XInternAtom(wm->dpy, "_NET_ACTIVE_WINDOW", False);
    wm->atom_net_wm_window_type      = XInternAtom(wm->dpy, "_NET_WM_WINDOW_TYPE", False);
    wm->atom_net_wm_window_type_dialog  = XInternAtom(wm->dpy, "_NET_WM_WINDOW_TYPE_DIALOG", False);
    wm->atom_net_wm_window_type_utility = XInternAtom(wm->dpy, "_NET_WM_WINDOW_TYPE_UTILITY", False);
    wm->atom_net_wm_window_type_splash  = XInternAtom(wm->dpy, "_NET_WM_WINDOW_TYPE_SPLASH", False);
    wm->atom_net_wm_window_type_dock    = XInternAtom(wm->dpy, "_NET_WM_WINDOW_TYPE_DOCK", False);
}

static void adopt_existing(WM *wm) {
    Window root_r, parent_r; Window *ch; unsigned int n;
    if (!XQueryTree(wm->dpy, wm->root, &root_r, &parent_r, &ch, &n)) return;
    for (unsigned int i = 0; i < n; i++) {
        XWindowAttributes wa;
        if (!XGetWindowAttributes(wm->dpy, ch[i], &wa)) continue;
        if (wa.override_redirect || wa.map_state != IsViewable) continue;
        Client *c = client_new(wm, ch[i]);
        client_add(wm, c);
        client_frame(wm, c);
        XMapWindow(wm->dpy, ch[i]);
    }
    if (ch) XFree(ch);
}


/* ── đọc reserved area từ các dock/panel (polybar, etc.) ── */
void update_struts(WM *wm) {
    Atom net_strut_partial = XInternAtom(wm->dpy, "_NET_WM_STRUT_PARTIAL", False);
    Atom net_strut         = XInternAtom(wm->dpy, "_NET_WM_STRUT", False);
    wm->strut_top = wm->strut_bottom = wm->strut_left = wm->strut_right = 0;

    Window root_r, parent_r; Window *ch; unsigned int n;
    if (!XQueryTree(wm->dpy, wm->root, &root_r, &parent_r, &ch, &n)) return;
    for (unsigned int i = 0; i < n; i++) {
        Atom type; int fmt; unsigned long nitems, after;
        unsigned char *data = NULL;
        /* try _NET_WM_STRUT_PARTIAL first, then _NET_WM_STRUT */
        if (XGetWindowProperty(wm->dpy, ch[i], net_strut_partial, 0, 12, False,
                               XA_CARDINAL, &type, &fmt, &nitems, &after, &data)
            == Success && data && nitems >= 4) {
            long *s = (long*)data;
            if (s[0] > wm->strut_left)   wm->strut_left   = (int)s[0];
            if (s[1] > wm->strut_right)  wm->strut_right  = (int)s[1];
            if (s[2] > wm->strut_top)    wm->strut_top    = (int)s[2];
            if (s[3] > wm->strut_bottom) wm->strut_bottom = (int)s[3];
            XFree(data); continue;
        }
        if (data) { XFree(data); data = NULL; }
        if (XGetWindowProperty(wm->dpy, ch[i], net_strut, 0, 4, False,
                               XA_CARDINAL, &type, &fmt, &nitems, &after, &data)
            == Success && data && nitems >= 4) {
            long *s = (long*)data;
            if (s[0] > wm->strut_left)   wm->strut_left   = (int)s[0];
            if (s[1] > wm->strut_right)  wm->strut_right  = (int)s[1];
            if (s[2] > wm->strut_top)    wm->strut_top    = (int)s[2];
            if (s[3] > wm->strut_bottom) wm->strut_bottom = (int)s[3];
            XFree(data);
        }
    }
    if (ch) XFree(ch);
}

void wm_init(WM *wm) {
    memset(wm, 0, sizeof(WM));
    signal(SIGCHLD, sigchld_handler);
    signal(SIGTERM, SIG_DFL);
    signal(SIGINT,  SIG_DFL);

    wm->dpy = XOpenDisplay(NULL);
    if (!wm->dpy) die("ltwm: cannot open display");
    wm->screen = DefaultScreen(wm->dpy);
    wm->root   = RootWindow(wm->dpy, wm->screen);
    wm->sw     = DisplayWidth(wm->dpy, wm->screen);
    wm->sh     = DisplayHeight(wm->dpy, wm->screen);

    config_defaults(&wm->cfg);

    cache_atoms(wm);
    config_apply_colors(wm);

    for (int i = 0; i < MAX_WORKSPACES; i++) {
        wm->workspaces[i].id           = i;
        wm->workspaces[i].layout       = LAYOUT_TILE;
        wm->workspaces[i].master_ratio = DEFAULT_MASTER_RATIO;
        wm->workspaces[i].master_count = 1;
        strncpy(wm->workspaces[i].name, wm->cfg.ws_names[i], 31);
    }
    wm->cur_ws  = 0;
    wm->running = true;

    /* claim SubstructureRedirect (fail if another WM exists) */
    XSetErrorHandler(xerror_checkwm);
    XSelectInput(wm->dpy, wm->root,
                 SubstructureRedirectMask | SubstructureNotifyMask |
                 ButtonPressMask |
                 StructureNotifyMask | PropertyChangeMask);
    XSync(wm->dpy, False);
    XSetErrorHandler(xerror_handler);

    /* cursors */
    wm->cursor_normal = XCreateFontCursor(wm->dpy, XC_left_ptr);
    wm->cursor_move   = XCreateFontCursor(wm->dpy, XC_fleur);
    wm->cursor_resize = XCreateFontCursor(wm->dpy, XC_bottom_right_corner);
    XDefineCursor(wm->dpy, wm->root, wm->cursor_normal);

    grab_keys(wm);

    /* no button grabs — move/resize via keyboard shortcuts only */


    /* no global button grab — focus via click on frame only */


    update_struts(wm);
    adopt_existing(wm);
    for (int i = 0; i < MAX_WORKSPACES; i++) ws_tile(wm, i);
    client_focus(wm, wm->workspaces[0].clients);


    ipc_init(wm);

    /* EWMH: advertise WM name + version so fastfetch/neofetch can read it */
    Atom net_supporting  = XInternAtom(wm->dpy, "_NET_SUPPORTING_WM_CHECK", False);
    Atom net_wm_name_ewmh= XInternAtom(wm->dpy, "_NET_WM_NAME", False);
    Atom utf8_string     = XInternAtom(wm->dpy, "UTF8_STRING", False);
    Atom net_supported   = XInternAtom(wm->dpy, "_NET_SUPPORTED", False);

    /* create a small child window to hold the WM identity */
    Window wmcheck = XCreateSimpleWindow(wm->dpy, wm->root, 0,0,1,1,0,0,0);

    XChangeProperty(wm->dpy, wmcheck, net_supporting, XA_WINDOW, 32,
                    PropModeReplace, (unsigned char*)&wmcheck, 1);
    XChangeProperty(wm->dpy, wmcheck, net_wm_name_ewmh, utf8_string, 8,
                    PropModeReplace, (unsigned char*)"ltwm 0.10.0-alpha",
                    sizeof("ltwm 0.10.0-alpha")-1);

    XChangeProperty(wm->dpy, wm->root, net_supporting, XA_WINDOW, 32,
                    PropModeReplace, (unsigned char*)&wmcheck, 1);
    XChangeProperty(wm->dpy, wm->root, net_wm_name_ewmh, utf8_string, 8,
                    PropModeReplace, (unsigned char*)"ltwm 0.10.0-alpha",
                    sizeof("ltwm 0.10.0-alpha")-1);

    /* advertise basic EWMH support */
    Atom supported[] = {
        net_supporting, net_wm_name_ewmh,
        wm->atom_net_wm_state, wm->atom_net_wm_state_fullscreen,
        wm->atom_net_active_window, wm->atom_net_wm_window_type,
        wm->atom_net_wm_window_type_dialog, wm->atom_net_wm_window_type_dock,
    };
    XChangeProperty(wm->dpy, wm->root, net_supported, XA_ATOM, 32,
                    PropModeReplace, (unsigned char*)supported,
                    sizeof(supported)/sizeof(Atom));

    XSync(wm->dpy, False);
}

void wm_run(WM *wm) {
    /* run ltwmrc — the shell script that configures ltwm via ltwmc */
    {
        const char *home = getenv("HOME"); if (!home) home = "/root";
        char rc[MAX_CMD_LEN];
        snprintf(rc, sizeof(rc), "%s/.config/ltwm/ltwmrc", home);
        if (access(rc, X_OK) == 0)
            spawn(rc);
        else
            fprintf(stderr, "ltwm: ltwmrc not found at %s\n", rc);
    }

    XEvent ev;
    while (wm->running) {
        /* drain all pending X events first */
        while (XPending(wm->dpy)) {
            XNextEvent(wm->dpy, &ev);
            handle_event(wm, &ev);
        }
        /* then check IPC without blocking */
        ipc_poll(wm);

        /* block for next X event (short timeout so IPC stays responsive) */
        if (!XPending(wm->dpy)) {
            /* use select to wait on X fd and IPC fd simultaneously */
            fd_set rfds; FD_ZERO(&rfds);
            int xfd = ConnectionNumber(wm->dpy);
            FD_SET(xfd, &rfds);
            int maxfd = xfd;
            if (wm->ipc_fd >= 0) {
                FD_SET(wm->ipc_fd, &rfds);
                if (wm->ipc_fd > maxfd) maxfd = wm->ipc_fd;
            }
            struct timeval tv = {1, 0};   /* 1s timeout → clock refresh */
            select(maxfd+1, &rfds, NULL, NULL, &tv);

            /* clock tick: redraw bar every ~1s */
        
        }
    }
}

void wm_cleanup(WM *wm) {
    ipc_cleanup(wm);
    for (int i = 0; i < MAX_WORKSPACES; i++) {
        Client *c = wm->workspaces[i].clients;
        while (c) {
            Client *nx = c->next;
            if (c->frame) client_unframe(wm, c);
            free(c); c = nx;
        }
    }
    XFreeCursor(wm->dpy, wm->cursor_normal);
    XFreeCursor(wm->dpy, wm->cursor_move);
    XFreeCursor(wm->dpy, wm->cursor_resize);
    XUngrabKey(wm->dpy, AnyKey, AnyModifier, wm->root);
    /* keys handled by sxhkd */
    return;
    XSetInputFocus(wm->dpy, PointerRoot, RevertToPointerRoot, CurrentTime);
    XSync(wm->dpy, False);
    XCloseDisplay(wm->dpy);
}

void wm_quit(WM *wm)  { wm->running = false; }

void wm_reload_config(WM *wm) {
    /* save workspace names (user may want to keep them) */
    const char *home = getenv("HOME"); if (!home) home = "/root";
    char rc[MAX_CMD_LEN];
    snprintf(rc, sizeof(rc), "%s/.config/ltwm/ltwmrc", home);
    if (access(rc, X_OK) == 0) spawn(rc);
    fprintf(stderr, "ltwm: ltwmrc reloaded\n");
}

int main(int argc, char *argv[]) {
    (void)argc; (void)argv;
    wm_init(&g_wm);
    wm_run(&g_wm);
    wm_cleanup(&g_wm);
    return 0;
}
