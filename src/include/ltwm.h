#ifndef LTWM_H
#define LTWM_H

#define _POSIX_C_SOURCE 200809L
#define _DEFAULT_SOURCE

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>
#include <X11/Xproto.h>
#include <X11/keysym.h>
#include <X11/XF86keysym.h>
#include <X11/XKBlib.h>
#include <X11/extensions/Xrandr.h>
#include <X11/cursorfont.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <fcntl.h>
#include <errno.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdarg.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/select.h>
#include <time.h>

#define MAX_WORKSPACES  10
#define MAX_CLIENTS     256
#define MAX_KEYBINDS    192
#define MAX_AUTOSTART   32
#define MAX_CMD_LEN     512
#define MAX_NAME_LEN    256
#define DEFAULT_BAR_HEIGHT   28
#define DEFAULT_BORDER_WIDTH  2
#define DEFAULT_GAP           6
#define DEFAULT_MASTER_RATIO  0.55f
#define IPC_SOCKET_PATH_FMT  "/tmp/ltwm-%d.sock"
#define IPC_MAX_MSG          1024
#define IPC_MAX_CLIENTS      8
#define CONFIG_PATH   "/.config/ltwm/ltwm.cfg"

typedef enum {
    LAYOUT_TILE = 0,
    LAYOUT_MONOCLE,
    LAYOUT_FLOAT,
    LAYOUT_COUNT
} Layout;

typedef enum {
    ACTION_EXEC = 0,
    ACTION_KILLACTIVE,
    ACTION_TOGGLEFLOATING,
    ACTION_SETFLOATING,
    ACTION_SETTILED,
    ACTION_TOGGLEFULLSCREEN,
    ACTION_TOGGLESPLIT,
    ACTION_CYCLELAYOUT,
    ACTION_MOVEFOCUS_L,
    ACTION_MOVEFOCUS_R,
    ACTION_MOVEFOCUS_U,
    ACTION_MOVEFOCUS_D,
    ACTION_FOCUSMASTER,
    ACTION_SWAPMASTER,
    ACTION_WORKSPACE,
    ACTION_MOVETOWORKSPACE,
    ACTION_WORKSPACE_SCROLL_UP,
    ACTION_WORKSPACE_SCROLL_DOWN,
    ACTION_MASTER_RATIO_INC,
    ACTION_MASTER_RATIO_DEC,
    ACTION_QUIT,
    ACTION_RELOAD,
} ActionType;

typedef struct Client {
    Window  win;
    Window  frame;
    int x, y, w, h;
    int fx, fy, fw, fh;
    int saved_x, saved_y, saved_w, saved_h;
    bool floating;
    bool fullscreen;
    bool urgent;
    bool fixed;
    char title[MAX_NAME_LEN];
    int  workspace;
    struct Client *next;
    struct Client *prev;
} Client;

typedef struct {
    int    id;
    char   name[32];
    Client *clients;
    Client *focused;
    Layout  layout;
    float   master_ratio;
    int     master_count;
} Workspace;

typedef struct {
    unsigned int mod;
    KeySym       keysym;
    unsigned int button;
    ActionType   action;
    char         arg[MAX_CMD_LEN];
} Keybind;

typedef struct {
    unsigned long border_normal;
    unsigned long border_focused;
    unsigned long border_floating;
    unsigned long bar_bg;
    unsigned long bar_fg;
    unsigned long bar_ws_active_bg;
    unsigned long bar_ws_active_fg;
    unsigned long bar_ws_occupied_bg;
    unsigned long bar_ws_occupied_fg;
    char col_border_normal[16];
    char col_border_focused[16];
    char col_border_floating[16];
    char col_bar_bg[16];
    char col_bar_fg[16];
    char col_bar_ws_active_bg[16];
    char col_bar_ws_active_fg[16];
    char col_bar_ws_occupied_bg[16];
    char col_bar_ws_occupied_fg[16];
    int   border_width;
    int   bar_height;
    int   gap;
    char  font[MAX_NAME_LEN];
    char  ws_names[MAX_WORKSPACES][32];
    Keybind keybinds[MAX_KEYBINDS];
    int     n_keybinds;
    char autostart[MAX_AUTOSTART][MAX_CMD_LEN];
    int  n_autostart;
    unsigned int mainMod;
    unsigned int mod1;
    char terminal[MAX_CMD_LEN];
    char filemanager[MAX_CMD_LEN];
    char menu[MAX_CMD_LEN];
    int  float_default_w;
    int  float_default_h;
    char clock_fmt[64];
    bool bar_show_layout;
} Config;

typedef struct {
    int  fd;
    bool active;
} IPCClient;

typedef struct {
    Display    *dpy;
    Window      root;
    int         screen;
    int         sw, sh;
    Workspace   workspaces[MAX_WORKSPACES];
    int         cur_ws;
    Config      cfg;
    bool        running;
    bool        dragging;
    bool        resizing;
    Client     *drag_client;
    int         drag_start_x, drag_start_y;
    int         drag_win_x,   drag_win_y;
    int         drag_win_w,   drag_win_h;
    Window      bar_win;
    GC          bar_gc;
    XFontStruct *bar_font;
    bool        bar_visible;
    Pixmap      bar_buf;
    int         ipc_fd;
    int         evt_fd;
    char        evt_path[108];
    IPCClient   ipc_clients[IPC_MAX_CLIENTS];
    char        ipc_path[108];
    Atom        atom_wm_protocols;
    Atom        atom_wm_delete;
    Atom        atom_wm_take_focus;
    Atom        atom_wm_name;
    Atom        atom_net_wm_name;
    Atom        atom_net_wm_state;
    Atom        atom_net_wm_state_fullscreen;
    Atom        atom_net_active_window;
    Atom        atom_net_wm_window_type;
    Atom        atom_net_wm_window_type_dialog;
    Atom        atom_net_wm_window_type_utility;
    Atom        atom_net_wm_window_type_splash;
    Atom        atom_net_wm_window_type_dock;
    /* reserved screen area from docks/panels */
    int         strut_top, strut_bottom, strut_left, strut_right;

    Cursor      cursor_normal;
    Cursor      cursor_move;
    Cursor      cursor_resize;
} WM;

/* wm.c */
void wm_init(WM *wm);
void update_struts(WM *wm);
void wm_run(WM *wm);
void wm_cleanup(WM *wm);
void wm_quit(WM *wm);
void wm_reload_config(WM *wm);

/* events.c */
void handle_event(WM *wm, XEvent *e);
void on_map_request(WM *wm, XMapRequestEvent *e);
void on_unmap_notify(WM *wm, XUnmapEvent *e);
void on_destroy_notify(WM *wm, XDestroyWindowEvent *e);
void on_configure_request(WM *wm, XConfigureRequestEvent *e);
void on_key_press(WM *wm, XKeyEvent *e);
void on_button_press(WM *wm, XButtonEvent *e);
void on_button_release(WM *wm, XButtonEvent *e);
void on_motion_notify(WM *wm, XMotionEvent *e);
void on_enter_notify(WM *wm, XCrossingEvent *e);
void on_property_notify(WM *wm, XPropertyEvent *e);
void on_expose(WM *wm, XExposeEvent *e);
void on_client_message(WM *wm, XClientMessageEvent *e);

/* client.c */
Client *client_new(WM *wm, Window win);
void    client_frame(WM *wm, Client *c);
void    client_unframe(WM *wm, Client *c);
void    client_focus(WM *wm, Client *c);
void    client_kill(WM *wm, Client *c);
void    client_set_floating(WM *wm, Client *c, bool floating);
void    client_toggle_float(WM *wm, Client *c);
void    client_toggle_fullscreen(WM *wm, Client *c);
void    client_move_resize(WM *wm, Client *c, int x, int y, int w, int h);
void    client_center_on_screen(WM *wm, Client *c);
void    client_update_title(WM *wm, Client *c);
void    client_update_border(WM *wm, Client *c);
bool    client_wants_float(WM *wm, Client *c);
Client *client_find_by_win(WM *wm, Window win);
Client *client_find_by_frame(WM *wm, Window frame);
void    client_remove(WM *wm, Client *c);
void    client_add(WM *wm, Client *c);
Client *client_get_master(WM *wm, int ws_id);
void    client_swap_with_master(WM *wm, Client *c);

/* workspace.c */
void ws_switch(WM *wm, int id);
void ws_move_client(WM *wm, Client *c, int id);
void ws_tile(WM *wm, int id);
void ws_focus_dir(WM *wm, int dir);
void ws_cycle_layout(WM *wm);

/* config.c */
bool         config_load(Config *cfg, const char *path);
void         config_defaults(Config *cfg);
void         config_apply_colors(WM *wm);
unsigned int config_parse_mod(const char *s);
KeySym       config_parse_keysym(const char *s);

/* bar.c */
void bar_create(WM *wm);
void bar_draw(WM *wm);
void bar_destroy(WM *wm);

/* ipc.c */
void ipc_init(WM *wm);
void ipc_event_emit(WM *wm, const char *event);
void ipc_event_emit(WM *wm, const char *event);
void ipc_poll(WM *wm);
void ipc_cleanup(WM *wm);
void ipc_dispatch(WM *wm, int fd, const char *msg);

/* spawn.c */
void spawn(const char *cmd);
void spawn_autostart(WM *wm);

/* util.c */
unsigned long parse_color(Display *dpy, int screen, const char *hex);
void          die(const char *fmt, ...);
char         *trim(char *s);
bool          starts_with(const char *s, const char *prefix);

#endif
