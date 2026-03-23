#include "ltwm.h"

void config_defaults(Config *cfg) {
    memset(cfg, 0, sizeof(Config));
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
