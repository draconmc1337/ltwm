#include "ltwm.h"

void config_defaults(Config *cfg) {
    memset(cfg, 0, sizeof(Config));
    /* bar defaults */
    cfg->bar_enabled          = false;   /* off by default, enable via ltwmrc */
    cfg->bar_height           = 28;
    cfg->bar_padding_x        = 4;
    cfg->bar_padding_y        = 0;
    cfg->bar_show_workspaces  = true;
    cfg->bar_show_layout      = true;
    cfg->bar_show_version     = false;
    cfg->bar_show_cmds        = true;
    cfg->bar_show_btns        = false;
    cfg->bar_n_cmds           = 0;
    cfg->bar_n_btns           = 0;
    strncpy(cfg->bar_cmd_sep,        " | ", 15);
    strncpy(cfg->col_bar_bg,         "1E1E2E", 15);
    strncpy(cfg->col_bar_fg,         "CDD6F4", 15);
    strncpy(cfg->col_bar_ws_active_bg,   "89B4FA", 15);
    strncpy(cfg->col_bar_ws_active_fg,   "1E1E2E", 15);
    strncpy(cfg->col_bar_ws_occupied_bg, "313244", 15);
    strncpy(cfg->col_bar_ws_occupied_fg, "CDD6F4", 15);
    /* Xft format: "FontName:size=N" — hỗ trợ Nerd Fonts, Unicode, emoji
       Fallback về monospace nếu IosevkaNerdFont chưa cài */
    strncpy(cfg->bar_font, "IosevkaNerdFont:size=14", MAX_NAME_LEN-1);

    cfg->border_width      = DEFAULT_BORDER_WIDTH;
    cfg->gap               = DEFAULT_GAP;
    cfg->float_default_w   = 0;
    cfg->float_default_h   = 0;
    cfg->mainMod           = Mod4Mask;
    cfg->mod1              = Mod1Mask;

    /* catppuccin mocha defaults — overridden by ltwmrc */
    strncpy(cfg->col_border_normal,   "45475A", 15);
    strncpy(cfg->col_border_focused,  "89B4FA", 15);
    strncpy(cfg->col_border_floating, "A6E3A1", 15);

    snprintf(cfg->terminal,    MAX_CMD_LEN-1, "kitty");
    snprintf(cfg->filemanager, MAX_CMD_LEN-1, "thunar");
    snprintf(cfg->menu,        MAX_CMD_LEN-1, "rofi -show drun");

    for (int i = 0; i < MAX_WORKSPACES; i++)
        snprintf(cfg->ws_names[i], 32, "%d", i+1);
}

void config_apply_colors(WM *wm) {
    Config *cfg = &wm->cfg;
    cfg->border_normal   = parse_color(wm->dpy, wm->screen, cfg->col_border_normal);
    cfg->border_focused  = parse_color(wm->dpy, wm->screen, cfg->col_border_focused);
    cfg->border_floating = parse_color(wm->dpy, wm->screen, cfg->col_border_floating);
    cfg->bar_bg             = parse_color(wm->dpy, wm->screen, cfg->col_bar_bg);
    cfg->bar_fg             = parse_color(wm->dpy, wm->screen, cfg->col_bar_fg);
    cfg->bar_ws_active_bg   = parse_color(wm->dpy, wm->screen, cfg->col_bar_ws_active_bg);
    cfg->bar_ws_active_fg   = parse_color(wm->dpy, wm->screen, cfg->col_bar_ws_active_fg);
    cfg->bar_ws_occupied_bg = parse_color(wm->dpy, wm->screen, cfg->col_bar_ws_occupied_bg);
    cfg->bar_ws_occupied_fg = parse_color(wm->dpy, wm->screen, cfg->col_bar_ws_occupied_fg);
    /* apply per-cmd colors */
    for (int i=0; i<cfg->bar_n_cmds; i++) {
        if (cfg->bar_cmds[i].col_fg[0])
            cfg->bar_cmds[i].fg = parse_color(wm->dpy, wm->screen, cfg->bar_cmds[i].col_fg);
        if (cfg->bar_cmds[i].col_bg[0])
            cfg->bar_cmds[i].bg = parse_color(wm->dpy, wm->screen, cfg->bar_cmds[i].col_bg);
    }
    for (int i=0; i<cfg->bar_n_btns; i++) {
        if (cfg->bar_btns[i].col_bg[0])
            cfg->bar_btns[i].bg = parse_color(wm->dpy, wm->screen, cfg->bar_btns[i].col_bg);
    }
}

char *trim(char *s) {
    while (*s==' '||*s=='\t') s++;
    if (!*s) return s;
    char *e=s+strlen(s)-1;
    while(e>s&&(*e==' '||*e=='\t'||*e=='\n'||*e=='\r'))e--;
    *(e+1)='\0';
    return s;
}

bool starts_with(const char *s, const char *p) {
    return strncmp(s,p,strlen(p))==0;
}

void safe_strncpy(char *d, const char *s, size_t n) {
    strncpy(d,s,n-1); d[n-1]='\0';
}

/* ── apply window rules to a new client ─────────────────── */
void apply_rules(WM *wm, Client *c) {
    /* get WM_CLASS */
    char wm_class[64] = "";
    XClassHint hint;
    if (XGetClassHint(wm->dpy, c->win, &hint)) {
        if (hint.res_class)
            strncpy(wm_class, hint.res_class, sizeof(wm_class)-1);
        if (hint.res_name)  XFree(hint.res_name);
        if (hint.res_class) XFree(hint.res_class);
    }

    for (int i = 0; i < wm->cfg.n_rules; i++) {
        Rule *r = &wm->cfg.rules[i];
        bool match_class = !r->class[0] ||
                           strcasestr(wm_class, r->class) != NULL;
        bool match_title = !r->title[0] ||
                           strcasestr(c->title, r->title) != NULL;
        if (!match_class || !match_title) continue;

        if (r->workspace >= 1 && r->workspace <= MAX_WORKSPACES)
            c->workspace = r->workspace - 1;
        if (r->floating)    c->floating    = true;
        if (r->fullscreen)  c->fullscreen  = true;
        if (r->center)      c->floating    = true; /* center implies float */
    }
}
