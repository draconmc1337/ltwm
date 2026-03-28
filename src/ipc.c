#include "ltwm.h"

/* ══════════════════════════════════════════════════════════
 *  IPC — Unix socket server
 *  Command socket: /tmp/ltwm-N.sock
 *  Event socket:   /tmp/ltwm-N.events
 * ══════════════════════════════════════════════════════════ */

void ipc_init(WM *wm) {
    const char *disp = getenv("DISPLAY");
    int dnum = 0;
    if (disp) { const char *p = strchr(disp,':'); if(p) dnum=atoi(p+1); }

    /* command socket */
    snprintf(wm->ipc_path, sizeof(wm->ipc_path), IPC_SOCKET_PATH_FMT, dnum);
    unlink(wm->ipc_path);

    wm->ipc_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (wm->ipc_fd < 0) { perror("ltwm: ipc"); wm->ipc_fd=-1; goto evt; }
    fcntl(wm->ipc_fd, F_SETFL, fcntl(wm->ipc_fd,F_GETFL,0)|O_NONBLOCK);
    {
        struct sockaddr_un sa={0}; sa.sun_family=AF_UNIX;
        strncpy(sa.sun_path, wm->ipc_path, sizeof(sa.sun_path)-1);
        if (bind(wm->ipc_fd,(struct sockaddr*)&sa,sizeof(sa))<0) {
            close(wm->ipc_fd); wm->ipc_fd=-1; goto evt;
        }
        chmod(wm->ipc_path, 0600);
        listen(wm->ipc_fd, IPC_MAX_CLIENTS);
    }
    for (int i=0;i<IPC_MAX_CLIENTS;i++) wm->ipc_clients[i].fd=-1;
    fprintf(stderr,"ltwm: socket %s\n", wm->ipc_path);

evt:
    /* event socket */
    snprintf(wm->evt_path, sizeof(wm->evt_path), "/tmp/ltwm-%d.events", dnum);
    unlink(wm->evt_path);
    wm->evt_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (wm->evt_fd < 0) { wm->evt_fd=-1; return; }
    fcntl(wm->evt_fd, F_SETFL, fcntl(wm->evt_fd,F_GETFL,0)|O_NONBLOCK);
    {
        struct sockaddr_un sa2={0}; sa2.sun_family=AF_UNIX;
        strncpy(sa2.sun_path, wm->evt_path, sizeof(sa2.sun_path)-1);
        if (bind(wm->evt_fd,(struct sockaddr*)&sa2,sizeof(sa2))<0) {
            close(wm->evt_fd); wm->evt_fd=-1; return;
        }
        chmod(wm->evt_path, 0600);
        listen(wm->evt_fd, 32);
    }
}

void ipc_cleanup(WM *wm) {
    for (int i=0;i<IPC_MAX_CLIENTS;i++)
        if (wm->ipc_clients[i].active) {
            close(wm->ipc_clients[i].fd);
            wm->ipc_clients[i].active=false;
        }
    if (wm->ipc_fd>=0) { close(wm->ipc_fd); wm->ipc_fd=-1; }
    unlink(wm->ipc_path);
    if (wm->evt_fd>=0) { close(wm->evt_fd); wm->evt_fd=-1; }
    unlink(wm->evt_path);
}

void ipc_poll(WM *wm) {
    if (wm->ipc_fd<0) return;
    fd_set rfds; FD_ZERO(&rfds);
    FD_SET(wm->ipc_fd, &rfds);
    int maxfd = wm->ipc_fd;
    for (int i=0;i<IPC_MAX_CLIENTS;i++) {
        if (!wm->ipc_clients[i].active) continue;
        FD_SET(wm->ipc_clients[i].fd, &rfds);
        if (wm->ipc_clients[i].fd > maxfd) maxfd=wm->ipc_clients[i].fd;
    }
    struct timeval tv={0, 10000}; /* 10ms — no busy-poll */
    if (select(maxfd+1,&rfds,NULL,NULL,&tv)<=0) return;

    if (FD_ISSET(wm->ipc_fd,&rfds)) {
        int cfd=accept(wm->ipc_fd,NULL,NULL);
        if (cfd>=0) {
            fcntl(cfd,F_SETFL,fcntl(cfd,F_GETFL,0)|O_NONBLOCK);
            for (int i=0;i<IPC_MAX_CLIENTS;i++)
                if (!wm->ipc_clients[i].active) {
                    wm->ipc_clients[i].fd=cfd;
                    wm->ipc_clients[i].active=true;
                    break;
                }
        }
    }
    for (int i=0;i<IPC_MAX_CLIENTS;i++) {
        if (!wm->ipc_clients[i].active) continue;
        int fd=wm->ipc_clients[i].fd;
        if (!FD_ISSET(fd,&rfds)) continue;
        char buf[IPC_MAX_MSG]={0};
        ssize_t n=read(fd,buf,sizeof(buf)-1);
        if (n<=0) { close(fd); wm->ipc_clients[i].active=false; continue; }
        buf[n]='\0';
        char *nl=strchr(buf,'\n'); if(nl)*nl='\0';
        ipc_dispatch(wm,fd,buf);
        close(fd);
        wm->ipc_clients[i].active=false;
    }
}

/* broadcast event to subscribers — one shot per connect */
void ipc_event_emit(WM *wm, const char *event) {
    if (wm->evt_fd<0) return;
    int cfd;
    /* drain all pending connects, write event, close */
    while ((cfd=accept(wm->evt_fd,NULL,NULL))>=0) {
        char buf[256];
        int n=snprintf(buf,sizeof(buf),"%s\n",event);
        (void)write(cfd,buf,(size_t)n);
        close(cfd);
    }
}

/* ── build workspace string dùng màu từ config (dùng chung cho cả Polybar lẫn ltwmc) ── */
static int build_ws_string(WM *wm, char *out, int outsz) {
    int pos = 0;
    char sa_fg[16], sa_bg[16], so_fg[16], so_bg[16];
    snprintf(sa_fg, sizeof(sa_fg), "#%06lx", wm->cfg.bar_ws_active_fg   & 0xFFFFFF);
    snprintf(sa_bg, sizeof(sa_bg), "#%06lx", wm->cfg.bar_ws_active_bg   & 0xFFFFFF);
    snprintf(so_fg, sizeof(so_fg), "#%06lx", wm->cfg.bar_ws_occupied_fg & 0xFFFFFF);
    snprintf(so_bg, sizeof(so_bg), "#%06lx", wm->cfg.bar_ws_occupied_bg & 0xFFFFFF);
    for (int i = 0; i < MAX_WORKSPACES; i++) {
        Workspace *ws = &wm->workspaces[i];
        int cnt = 0;
        for (Client *c = ws->clients; c; c = c->next) cnt++;
        bool active   = (i == wm->cur_ws);
        bool occupied = (cnt > 0);
        if (!active && !occupied) continue;
        pos += snprintf(out+pos, outsz-pos,
            "%%{F%s}%%{B%s} %s %%{B-}%%{F-}",
            active ? sa_fg : so_fg,
            active ? sa_bg : so_bg,
            ws->name);
    }
    return pos;
}

/* ── push workspace string trực tiếp vào Polybar IPC named pipe ────────────
   Polybar config: type = custom/ipc   hook-0 = ltwmc bar_workspaces
   Không cần script polling, ltwm push mỗi khi workspace thay đổi.       ── */
void polybar_push_workspaces(WM *wm) {
    if (!wm->polybar_pipe[0]) return;
    int fd = open(wm->polybar_pipe, O_WRONLY | O_NONBLOCK);
    if (fd < 0) return;   /* Polybar chưa chạy — bỏ qua, không crash */
    char out[2048] = {0};
    build_ws_string(wm, out, sizeof(out));
    dprintf(fd, "%s\n", out);
    close(fd);
}

/* ── bar_workspaces: ltwmc gọi để query, dùng cùng format builder ── */
static void send_bar_workspaces(WM *wm, int fd) {
    char out[2048] = {0};
    build_ws_string(wm, out, sizeof(out));
    dprintf(fd, "%s\n", out);
}

/* ── JSON status ────────────────────────────────────────── */
static void send_status(WM *wm, int fd) {
    char buf[4096]; int pos=0;
    pos+=snprintf(buf+pos,sizeof(buf)-pos,
        "{\"current_workspace\":%d,\"workspaces\":[",wm->cur_ws+1);
    for (int i=0;i<MAX_WORKSPACES;i++) {
        Workspace *ws=&wm->workspaces[i];
        int cnt=0; for(Client*c=ws->clients;c;c=c->next)cnt++;
        const char *layouts[]={"tile","vtile","vstripes","hstripes","monocle","float"};
        int li = (int)ws->layout;
        if (li<0||li>=(int)LAYOUT_COUNT) li=0;
        pos+=snprintf(buf+pos,sizeof(buf)-pos,
            "{\"id\":%d,\"name\":\"%s\",\"clients\":%d,"
            "\"layout\":\"%s\",\"active\":%s}%s",
            i+1, ws->name, cnt, layouts[li],
            (i==wm->cur_ws)?"true":"false",
            (i<MAX_WORKSPACES-1)?",":"");
    }
    pos+=snprintf(buf+pos,sizeof(buf)-pos,"],\"focused\":");
    Client *f=wm->workspaces[wm->cur_ws].focused;
    if (f)
        pos+=snprintf(buf+pos,sizeof(buf)-pos,
            "{\"title\":\"%s\",\"floating\":%s,\"fullscreen\":%s}",
            f->title,f->floating?"true":"false",f->fullscreen?"true":"false");
    else
        pos+=snprintf(buf+pos,sizeof(buf)-pos,"null");
    pos+=snprintf(buf+pos,sizeof(buf)-pos,"}\n");
    (void)write(fd,buf,(size_t)pos);
}

/* ══════════════════════════════════════════════════════════
 *  DISPATCH
 * ══════════════════════════════════════════════════════════ */
void ipc_dispatch(WM *wm, int fd, const char *msg) {
    char cmd[64]={0}, arg[MAX_CMD_LEN]={0}, arg2[MAX_CMD_LEN]={0};
    sscanf(msg, "%63s %511s %511[^\n]", cmd, arg, arg2);

    Workspace *ws = &wm->workspaces[wm->cur_ws];
    const char *ok = "ok\n";

    /* ── window actions ───────────────────────────────── */
    if      (!strcmp(cmd,"quit"))          { wm_quit(wm); }
    else if (!strcmp(cmd,"kill"))          { client_kill(wm,ws->focused); }
    else if (!strcmp(cmd,"togglefloat"))   { client_toggle_float(wm,ws->focused); }
    else if (!strcmp(cmd,"setfloat"))      { client_set_floating(wm,ws->focused,true); }
    else if (!strcmp(cmd,"settiled"))      { client_set_floating(wm,ws->focused,false); }
    else if (!strcmp(cmd,"fullscreen"))    { client_toggle_fullscreen(wm,ws->focused); }
    else if (!strcmp(cmd,"cyclelayout"))   { ws_cycle_layout(wm); }
    else if (!strcmp(cmd,"focusleft"))     { ws_focus_dir(wm,0); }
    else if (!strcmp(cmd,"focusright"))    { ws_focus_dir(wm,1); }
    else if (!strcmp(cmd,"focusup"))       { ws_focus_dir(wm,2); }
    else if (!strcmp(cmd,"focusdown"))     { ws_focus_dir(wm,3); }
    else if (!strcmp(cmd,"focusmaster"))   { client_focus(wm,client_get_master(wm,wm->cur_ws)); }
    else if (!strcmp(cmd,"swapmaster"))    { client_swap_with_master(wm,ws->focused); }
    else if (!strcmp(cmd,"reload"))        { wm_reload_config(wm); }
    else if (!strcmp(cmd,"exec") && arg[0]){ spawn(arg); }
    else if (!strcmp(cmd,"getstatus"))     { send_status(wm,fd); return; }
    else if (!strcmp(cmd,"bar_workspaces")){ send_bar_workspaces(wm,fd); return; }
    else if (!strcmp(cmd,"autostart") && arg[0]) {
        /* ltwmc autostart "cmd" — queue một lệnh để chạy khi autostart_run */
        if (wm->cfg.n_autostart < MAX_AUTOSTART)
            strncpy(wm->cfg.autostart[wm->cfg.n_autostart++], arg, MAX_CMD_LEN-1);
    }
    else if (!strcmp(cmd,"autostart_run")) {
        /* ltwmc autostart_run — spawn tất cả đã queue, song song */
        spawn_autostart(wm);
        wm->cfg.n_autostart = 0; /* reset để reload không chạy 2 lần */
    }

    /* ── workspace ────────────────────────────────────── */
    else if (!strcmp(cmd,"workspace")) {
        int n=atoi(arg);
        if (n>=1&&n<=MAX_WORKSPACES) ws_switch(wm,n-1);
    }
    else if (!strcmp(cmd,"movetoworkspace")) {
        int n=atoi(arg);
        if (n>=1&&n<=MAX_WORKSPACES) ws_move_client(wm,ws->focused,n-1);
    }
    else if (!strcmp(cmd,"setlayout")) {
        Layout l=LAYOUT_TILE;
        if      (!strcmp(arg,"vtile"))    l=LAYOUT_VTILE;
        else if (!strcmp(arg,"vstripes")) l=LAYOUT_VSTRIPES;
        else if (!strcmp(arg,"hstripes")) l=LAYOUT_HSTRIPES;
        else if (!strcmp(arg,"monocle"))  l=LAYOUT_MONOCLE;
        else if (!strcmp(arg,"float"))    l=LAYOUT_FLOAT;
        ws->layout=l;
        ws_tile(wm,wm->cur_ws);
    }

    /* ── window rules ─────────────────────────────────── */
    else if (!strcmp(cmd,"rule")) {
        if (!strcmp(arg,"clear")) {
            wm->cfg.n_rules=0;
        } else if (!strcmp(arg,"add") && wm->cfg.n_rules<MAX_RULES) {
            Rule *r=&wm->cfg.rules[wm->cfg.n_rules];
            memset(r,0,sizeof(*r));
            /* parse: class=X title=Y workspace=N float=true ... */
            char tmp[MAX_CMD_LEN]; strncpy(tmp,arg2,sizeof(tmp)-1);
            char *tok=strtok(tmp," ");
            while (tok) {
                if      (strncmp(tok,"class=",6)==0)          strncpy(r->class,tok+6,63);
                else if (strncmp(tok,"title=",6)==0)          strncpy(r->title,tok+6,63);
                else if (strncmp(tok,"workspace=",10)==0)     r->workspace=atoi(tok+10);
                else if (!strcmp(tok,"float=true"))           r->floating=true;
                else if (!strcmp(tok,"fullscreen=true"))      r->fullscreen=true;
                else if (!strcmp(tok,"center=true"))          r->center=true;
                tok=strtok(NULL," ");
            }
            wm->cfg.n_rules++;
        }
    }

    /* ── config setters ───────────────────────────────── */
    else if (!strcmp(cmd,"config")) {
        /* borders */
        if      (!strcmp(arg,"border_width")) {
            wm->cfg.border_width=atoi(arg2);
            for (Client*c=ws->clients;c;c=c->next) {
                XSetWindowBorderWidth(wm->dpy,c->frame,(unsigned)wm->cfg.border_width);
                client_update_border(wm,c);
            }
        }
        else if (!strcmp(arg,"gap")) {
            wm->cfg.gap=atoi(arg2);
            ws_tile(wm,wm->cur_ws);  /* only cur_ws */
        }
        else if (!strcmp(arg,"border_normal")) {
            wm->cfg.border_normal=parse_color(wm->dpy,wm->screen,arg2);
            for(Client*c=ws->clients;c;c=c->next) client_update_border(wm,c);
        }
        else if (!strcmp(arg,"border_focused")) {
            wm->cfg.border_focused=parse_color(wm->dpy,wm->screen,arg2);
            for(Client*c=ws->clients;c;c=c->next) client_update_border(wm,c);
        }
        else if (!strcmp(arg,"border_floating")) {
            wm->cfg.border_floating=parse_color(wm->dpy,wm->screen,arg2);
            for(Client*c=ws->clients;c;c=c->next) client_update_border(wm,c);
        }
        else if (!strcmp(arg,"master_ratio")) {
            float r=atof(arg2);
            if (r>0.1f&&r<0.9f) {
                /* set all workspaces but only tile current */
                for(int i=0;i<MAX_WORKSPACES;i++) wm->workspaces[i].master_ratio=r;
                ws_tile(wm,wm->cur_ws);
            }
        }
        else if (!strcmp(arg,"layout")) {
            Layout l=LAYOUT_TILE;
            if(!strcmp(arg2,"monocle"))  l=LAYOUT_MONOCLE;
            else if(!strcmp(arg2,"float")) l=LAYOUT_FLOAT;
            else if(!strcmp(arg2,"vtile")) l=LAYOUT_VTILE;
            else if(!strcmp(arg2,"vstripes")) l=LAYOUT_VSTRIPES;
            else if(!strcmp(arg2,"hstripes")) l=LAYOUT_HSTRIPES;
            ws->layout=l;
            ws_tile(wm,wm->cur_ws);
        }
        else if (!strcmp(arg,"workspace_name")) {
            char nstr[8]={0}, name[32]={0};
            sscanf(arg2,"%7s %31[^\n]",nstr,name);
            int n=atoi(nstr);
            if (n>=1&&n<=MAX_WORKSPACES)
                strncpy(wm->workspaces[n-1].name,name,31);
        }
        else if (!strcmp(arg,"float_default_w")) wm->cfg.float_default_w=atoi(arg2);
        else if (!strcmp(arg,"float_default_h")) wm->cfg.float_default_h=atoi(arg2);
        /* polybar IPC pipe path — ltwm push workspace string trực tiếp, không cần script */
        else if (!strcmp(arg,"polybar_pipe")) {
            strncpy(wm->polybar_pipe, arg2, sizeof(wm->polybar_pipe)-1);
            polybar_push_workspaces(wm); /* push ngay lập tức sau khi set */
        }

        /* bar */
        else if (!strcmp(arg,"bar_enable")) {
            bool en=!!atoi(arg2);
            if (en && !wm->bar_win) {
                wm->cfg.bar_enabled=true;
                config_apply_colors(wm);
                bar_create(wm);
                update_struts(wm);
                ws_tile(wm,wm->cur_ws);
            } else if (!en && wm->bar_win) {
                wm->cfg.bar_enabled=false;
                bar_destroy(wm);
                update_struts(wm);
                ws_tile(wm,wm->cur_ws);
            }
        }
        else if (!strcmp(arg,"bar_height")) {
            wm->cfg.bar_height=atoi(arg2);
            if (wm->bar_win) { bar_destroy(wm); bar_create(wm); }
        }
        else if (!strcmp(arg,"bar_font")) {
            strncpy(wm->cfg.bar_font,arg2,MAX_NAME_LEN-1);
            if (wm->bar_win) { bar_destroy(wm); bar_create(wm); }
        }
        else if (!strcmp(arg,"bar_padding_x"))  { wm->cfg.bar_padding_x=atoi(arg2); bar_draw(wm); }
        else if (!strcmp(arg,"bar_bg"))          { strncpy(wm->cfg.col_bar_bg,arg2+(arg2[0]=='#'),15); config_apply_colors(wm); bar_draw(wm); }
        else if (!strcmp(arg,"bar_fg"))          { strncpy(wm->cfg.col_bar_fg,arg2+(arg2[0]=='#'),15); config_apply_colors(wm); bar_draw(wm); }
        else if (!strcmp(arg,"bar_ws_active_bg"))   { strncpy(wm->cfg.col_bar_ws_active_bg,  arg2+(arg2[0]=='#'),15); config_apply_colors(wm); bar_draw(wm); }
        else if (!strcmp(arg,"bar_ws_active_fg"))   { strncpy(wm->cfg.col_bar_ws_active_fg,  arg2+(arg2[0]=='#'),15); config_apply_colors(wm); bar_draw(wm); }
        else if (!strcmp(arg,"bar_ws_occupied_bg")) { strncpy(wm->cfg.col_bar_ws_occupied_bg,arg2+(arg2[0]=='#'),15); config_apply_colors(wm); bar_draw(wm); }
        else if (!strcmp(arg,"bar_ws_occupied_fg")) { strncpy(wm->cfg.col_bar_ws_occupied_fg,arg2+(arg2[0]=='#'),15); config_apply_colors(wm); bar_draw(wm); }
        else if (!strcmp(arg,"bar_show_layout"))    { wm->cfg.bar_show_layout =!!atoi(arg2); bar_draw(wm); }
        else if (!strcmp(arg,"bar_show_version"))   { wm->cfg.bar_show_version=!!atoi(arg2); bar_draw(wm); }
        else if (!strcmp(arg,"bar_show_btns"))      { wm->cfg.bar_show_btns   =!!atoi(arg2); bar_draw(wm); }
        else if (!strcmp(arg,"bar_cmd_sep"))        { strncpy(wm->cfg.bar_cmd_sep,arg2,15); bar_draw(wm); }
        else if (!strcmp(arg,"bar_add_cmd")) {
            if (wm->cfg.bar_n_cmds<BAR_MAX_CMDS) {
                int i=wm->cfg.bar_n_cmds++;
                strncpy(wm->cfg.bar_cmds[i].cmd,arg2,MAX_CMD_LEN-1);
                strncpy(wm->cfg.bar_cmds[i].sep,wm->cfg.bar_cmd_sep,15);
                wm->cfg.bar_show_cmds=true;
                wm->bar_cmd_last[i]=0; /* force refresh */
            }
        }
        else if (!strcmp(arg,"bar_add_btn")) {
            if (wm->cfg.bar_n_btns<BAR_MAX_BTNS) {
                int i=wm->cfg.bar_n_btns++;
                char *col=strchr(arg2,':');
                if (col) {
                    *col='\0';
                    strncpy(wm->cfg.bar_btns[i].icon,arg2,31);
                    strncpy(wm->cfg.bar_btns[i].cmd, col+1,MAX_CMD_LEN-1);
                } else strncpy(wm->cfg.bar_btns[i].icon,arg2,31);
                wm->cfg.bar_show_btns=true;
            }
        }
        else if (!strcmp(arg,"bar_clear_cmds")) { wm->cfg.bar_n_cmds=0; wm->cfg.bar_show_cmds=false; bar_draw(wm); }
        else if (!strcmp(arg,"bar_clear_btns")) { wm->cfg.bar_n_btns=0; wm->cfg.bar_show_btns=false; bar_draw(wm); }
    }

    (void)write(fd,ok,3);
}
