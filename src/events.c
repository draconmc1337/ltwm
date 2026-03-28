#include "ltwm.h"
#include <math.h>

void handle_event(WM *wm, XEvent *e) {
    switch (e->type) {
    case MapRequest:       on_map_request(wm, &e->xmaprequest);       break;
    case UnmapNotify:      on_unmap_notify(wm, &e->xunmap);           break;
    case DestroyNotify:    on_destroy_notify(wm, &e->xdestroywindow); break;
    case ConfigureRequest: on_configure_request(wm, &e->xconfigurerequest); break;
    case KeyPress:         on_key_press(wm, &e->xkey);                break;
    case ButtonPress:      on_button_press(wm, &e->xbutton);          break;
    case ButtonRelease:    on_button_release(wm, &e->xbutton);        break;
    case MotionNotify:     on_motion_notify(wm, &e->xmotion);         break;
    case EnterNotify:      on_enter_notify(wm, &e->xcrossing);        break;
    case PropertyNotify:   on_property_notify(wm, &e->xproperty);     break;
    case Expose:           on_expose(wm, &e->xexpose);                break;
    case ClientMessage:    on_client_message(wm, &e->xclient);        break;
    default: break;
    }
}

/* ── safe client removal — handles both unmap+destroy ─────
   Returns the workspace id that was tiled, or -1 if not found */
static int remove_client(WM *wm, Window win) {
    Client *c = client_find_by_win(wm, win);
    if (!c) return -1;

    int ws = c->workspace;

    /* stop drag if we're dragging this window */
    if (wm->drag_client == c) {
        wm->dragging    = false;
        wm->resizing    = false;
        wm->drag_client = NULL;
        XUngrabPointer(wm->dpy, CurrentTime);
    }

    client_unframe(wm, c);
    client_remove(wm, c);
    free(c);

    ws_tile(wm, ws);

    /* refocus */
    if (ws == wm->cur_ws) {
        Client *next = wm->workspaces[ws].focused
                       ? wm->workspaces[ws].focused
                       : wm->workspaces[ws].clients;
        client_focus(wm, next);
    }
    if (wm->cfg.bar_enabled) bar_draw(wm);
    polybar_push_workspaces(wm);  /* client count thay đổi → update Polybar */
    return ws;
}

void on_map_request(WM *wm, XMapRequestEvent *e) {
    XWindowAttributes wa;
    if (!XGetWindowAttributes(wm->dpy, e->window, &wa)) return;
    if (wa.override_redirect) { XMapWindow(wm->dpy, e->window); return; }
    if (client_find_by_win(wm, e->window)) return;

    /* skip dock/panel */
    {
        Atom type; int fmt; unsigned long n, after;
        unsigned char *data = NULL;
        if (XGetWindowProperty(wm->dpy, e->window,
                               wm->atom_net_wm_window_type, 0, 1, False,
                               XA_ATOM, &type, &fmt, &n, &after, &data)
            == Success && data) {
            bool dock = (*(Atom*)data == wm->atom_net_wm_window_type_dock);
            XFree(data);
            if (dock) {
                XMapWindow(wm->dpy, e->window);
                update_struts(wm);
                for (int i = 0; i < MAX_WORKSPACES; i++) ws_tile(wm, i);
                return;
            }
        }
    }

    Client *c = client_new(wm, e->window);
    apply_rules(wm, c);
    client_add(wm, c);
    client_frame(wm, c);
    XMapWindow(wm->dpy, e->window);
    ws_tile(wm, wm->cur_ws);
    if (c->floating) client_center_on_screen(wm, c);
    client_focus(wm, c);
    if (wm->cfg.bar_enabled) bar_draw(wm);
    polybar_push_workspaces(wm);  /* client mới → occupied dot thay đổi */
}

void on_unmap_notify(WM *wm, XUnmapEvent *e) {
    /* ignore synthetic unmaps from reparenting */
    if (e->send_event) return;
    /* ignore unmaps of our own frame windows */
    if (client_find_by_frame(wm, e->window)) return;
    /* ignore unmaps we triggered ourselves (ws_switch, ws_move_client) */
    Client *c = client_find_by_win(wm, e->window);
    if (c && c->ignore_unmap > 0) { c->ignore_unmap--; return; }
    remove_client(wm, e->window);
}

void on_destroy_notify(WM *wm, XDestroyWindowEvent *e) {
    /* might be frame or client window */
    Client *c = client_find_by_frame(wm, e->window);
    if (c) {
        /* frame destroyed externally — unusual, clean up */
        c->frame = None;
        remove_client(wm, c->win);
        return;
    }
    remove_client(wm, e->window);
}

void on_configure_request(WM *wm, XConfigureRequestEvent *e) {
    Client *c = client_find_by_win(wm, e->window);
    if (c && (c->floating || c->fullscreen)) {
        if (e->value_mask & CWWidth)  c->w = e->width;
        if (e->value_mask & CWHeight) c->h = e->height;
        if (e->value_mask & CWX)      c->x = e->x;
        if (e->value_mask & CWY)      c->y = e->y;
        client_move_resize(wm, c, c->x, c->y, c->w, c->h);
    } else if (!c) {
        XWindowChanges wc = {
            .x=e->x, .y=e->y, .width=e->width, .height=e->height,
            .border_width=e->border_width, .sibling=e->above,
            .stack_mode=e->detail
        };
        XConfigureWindow(wm->dpy, e->window, e->value_mask, &wc);
    }
}

void on_key_press(WM *wm, XKeyEvent *e) {
    KeySym ks  = XkbKeycodeToKeysym(wm->dpy, e->keycode, 0, 0);
    unsigned int mod = e->state & ~(LockMask | Mod2Mask);

    for (int i = 0; i < wm->cfg.n_keybinds; i++) {
        Keybind *kb = &wm->cfg.keybinds[i];
        if (kb->button || kb->keysym != ks || mod != kb->mod) continue;
        Workspace *ws = &wm->workspaces[wm->cur_ws];
        switch (kb->action) {
        case ACTION_EXEC:             spawn(kb->arg); break;
        case ACTION_KILLACTIVE:       client_kill(wm, ws->focused); break;
        case ACTION_TOGGLEFLOATING:   client_toggle_float(wm, ws->focused); break;
        case ACTION_SETFLOATING:      if(ws->focused) client_set_floating(wm,ws->focused,true); break;
        case ACTION_SETTILED:         if(ws->focused) client_set_floating(wm,ws->focused,false); break;
        case ACTION_TOGGLEFULLSCREEN: client_toggle_fullscreen(wm, ws->focused); break;
        case ACTION_TOGGLESPLIT:
        case ACTION_CYCLELAYOUT:      ws_cycle_layout(wm); break;
        case ACTION_MOVEFOCUS_L:      ws_focus_dir(wm, 0); break;
        case ACTION_MOVEFOCUS_R:      ws_focus_dir(wm, 1); break;
        case ACTION_MOVEFOCUS_U:      ws_focus_dir(wm, 2); break;
        case ACTION_MOVEFOCUS_D:      ws_focus_dir(wm, 3); break;
        case ACTION_FOCUSMASTER:      client_focus(wm, client_get_master(wm,wm->cur_ws)); break;
        case ACTION_SWAPMASTER:       client_swap_with_master(wm, ws->focused); break;
        case ACTION_WORKSPACE: {
            int n = atoi(kb->arg);
            if (n>=1 && n<=MAX_WORKSPACES) ws_switch(wm, n-1);
            break;
        }
        case ACTION_MOVETOWORKSPACE: {
            int n = atoi(kb->arg);
            if (n>=1 && n<=MAX_WORKSPACES) ws_move_client(wm, ws->focused, n-1);
            break;
        }
        case ACTION_MASTER_RATIO_INC:
            ws->master_ratio = fminf(ws->master_ratio+0.05f, 0.9f);
            ws_tile(wm, wm->cur_ws); break;
        case ACTION_MASTER_RATIO_DEC:
            ws->master_ratio = fmaxf(ws->master_ratio-0.05f, 0.1f);
            ws_tile(wm, wm->cur_ws); break;
        case ACTION_QUIT:   wm_quit(wm); break;
        case ACTION_RELOAD: wm_reload_config(wm); break;
        default: break;
        }
        return;
    }
}

void on_button_press(WM *wm, XButtonEvent *e) {
    /* bar clicks */
    if (wm->cfg.bar_enabled && e->window == wm->bar_win) {
        if (e->button == Button1) bar_handle_click(wm, e->x);
        if (e->button == Button4) ws_switch(wm,(wm->cur_ws+1)%MAX_WORKSPACES);
        if (e->button == Button5) ws_switch(wm,(wm->cur_ws+MAX_WORKSPACES-1)%MAX_WORKSPACES);
        return;
    }

    Client *c = client_find_by_frame(wm, e->window);
    if (!c) return;

    client_focus(wm, c);

    unsigned int mod = e->state & ~(LockMask | Mod2Mask);
    if (mod != wm->cfg.mainMod) return;

    if (e->button == Button1) {
        if (!c->floating && !c->fullscreen)
            client_set_floating(wm, c, true);
        wm->dragging     = true;
        wm->drag_client  = c;
        wm->drag_start_x = e->x_root;
        wm->drag_start_y = e->y_root;
        wm->drag_win_x   = c->fx;
        wm->drag_win_y   = c->fy;
        XDefineCursor(wm->dpy, c->frame, wm->cursor_move);
        XGrabPointer(wm->dpy, wm->root, False,
                     ButtonReleaseMask|PointerMotionMask,
                     GrabModeAsync, GrabModeAsync, None, wm->cursor_move, CurrentTime);
    } else if (e->button == Button3) {
        if (!c->floating && !c->fullscreen)
            client_set_floating(wm, c, true);
        wm->resizing     = true;
        wm->drag_client  = c;
        wm->drag_start_x = e->x_root;
        wm->drag_start_y = e->y_root;
        wm->drag_win_w   = c->w;
        wm->drag_win_h   = c->h;
        XDefineCursor(wm->dpy, c->frame, wm->cursor_resize);
        XGrabPointer(wm->dpy, wm->root, False,
                     ButtonReleaseMask|PointerMotionMask,
                     GrabModeAsync, GrabModeAsync, None, wm->cursor_resize, CurrentTime);
    }
}

void on_button_release(WM *wm, XButtonEvent *e) {
    (void)e;
    if (wm->dragging || wm->resizing) {
        if (wm->drag_client)
            XDefineCursor(wm->dpy, wm->drag_client->frame, wm->cursor_normal);
        XUngrabPointer(wm->dpy, CurrentTime);
        wm->dragging = wm->resizing = false;
        wm->drag_client = NULL;
    }
}

void on_motion_notify(WM *wm, XMotionEvent *e) {
    if (!wm->drag_client) return;
    /* drain stale motion events */
    XEvent latest; latest.xmotion = *e;
    while (XCheckMaskEvent(wm->dpy, PointerMotionMask, &latest));
    e = &latest.xmotion;

    if (wm->dragging) {
        int nx = wm->drag_win_x + (e->x_root - wm->drag_start_x);
        int ny = wm->drag_win_y + (e->y_root - wm->drag_start_y);
        int bw = wm->cfg.border_width;
        XMoveWindow(wm->dpy, wm->drag_client->frame, nx, ny);
        wm->drag_client->fx = nx;
        wm->drag_client->fy = ny;
        wm->drag_client->x  = nx + bw;
        wm->drag_client->y  = ny + bw;
    } else if (wm->resizing) {
        int nw = wm->drag_win_w + (e->x_root - wm->drag_start_x);
        int nh = wm->drag_win_h + (e->y_root - wm->drag_start_y);
        if (nw < 40) nw = 40;
        if (nh < 40) nh = 40;
        client_move_resize(wm, wm->drag_client,
                           wm->drag_client->x, wm->drag_client->y, nw, nh);
    }
    XFlush(wm->dpy);
}

void on_enter_notify(WM *wm, XCrossingEvent *e) {
    if (e->mode != NotifyNormal || e->detail == NotifyInferior) return;
    Client *c = client_find_by_win(wm, e->window);
    if (!c) c = client_find_by_frame(wm, e->window);
    if (c && c->workspace == wm->cur_ws)
        client_focus(wm, c);
}

void on_property_notify(WM *wm, XPropertyEvent *e) {
    /* detect polybar/dock setting strut — re-tile immediately */
    Atom strut      = XInternAtom(wm->dpy, "_NET_WM_STRUT", False);
    Atom strut_part = XInternAtom(wm->dpy, "_NET_WM_STRUT_PARTIAL", False);
    if (e->atom == strut || e->atom == strut_part) {
        update_struts(wm);
        for (int i = 0; i < MAX_WORKSPACES; i++) ws_tile(wm, i);
        return;
    }

    Client *c = client_find_by_win(wm, e->window);
    if (!c) return;
    if (e->atom == wm->atom_wm_name || e->atom == wm->atom_net_wm_name) {
        client_update_title(wm, c);
        if (wm->cfg.bar_enabled) bar_draw(wm);
    }
}

void on_expose(WM *wm, XExposeEvent *e) {
    if (e->window == wm->bar_win && e->count == 0)
        bar_draw(wm);
}

void on_client_message(WM *wm, XClientMessageEvent *e) {
    Client *c = client_find_by_win(wm, e->window);
    if (!c) return;
    if (e->message_type == wm->atom_net_wm_state) {
        for (int i = 1; i <= 2; i++) {
            if ((Atom)e->data.l[i] == wm->atom_net_wm_state_fullscreen) {
                int act = (int)e->data.l[0];
                bool want = (act==1)||(act==2 && !c->fullscreen);
                if (want != c->fullscreen) client_toggle_fullscreen(wm, c);
            }
        }
    }
    if (e->message_type == wm->atom_net_active_window) {
        if (c->workspace != wm->cur_ws) ws_switch(wm, c->workspace);
        client_focus(wm, c);
    }
}
