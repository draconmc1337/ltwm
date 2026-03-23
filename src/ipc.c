#include "ltwm.h"

void ipc_init(WM *wm) {
    const char *disp = getenv("DISPLAY");
    int dnum = 0;
    if (disp) { const char *c = strchr(disp,':'); if(c) dnum=atoi(c+1); }
    snprintf(wm->ipc_path, sizeof(wm->ipc_path), IPC_SOCKET_PATH_FMT, dnum);
    unlink(wm->ipc_path);

    wm->ipc_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (wm->ipc_fd < 0) { perror("ltwm: ipc socket"); wm->ipc_fd=-1; return; }

    int flags = fcntl(wm->ipc_fd, F_GETFL, 0);
    fcntl(wm->ipc_fd, F_SETFL, flags | O_NONBLOCK);

    struct sockaddr_un sa = {0};
    sa.sun_family = AF_UNIX;
    strncpy(sa.sun_path, wm->ipc_path, sizeof(sa.sun_path)-1);

    if (bind(wm->ipc_fd, (struct sockaddr*)&sa, sizeof(sa)) < 0) {
        perror("ltwm: ipc bind"); close(wm->ipc_fd); wm->ipc_fd=-1; return;
    }
    chmod(wm->ipc_path, 0600);
    listen(wm->ipc_fd, IPC_MAX_CLIENTS);
    for (int i=0; i<IPC_MAX_CLIENTS; i++) wm->ipc_clients[i].fd=-1;
    fprintf(stderr, "ltwm: socket %s\n", wm->ipc_path);

    /* event broadcast socket — clients tail this for realtime updates */
    snprintf(wm->evt_path, sizeof(wm->evt_path), "/tmp/ltwm-%d.events", dnum);
    unlink(wm->evt_path);
    wm->evt_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (wm->evt_fd >= 0) {
        int f2 = fcntl(wm->evt_fd, F_GETFL, 0);
        fcntl(wm->evt_fd, F_SETFL, f2 | O_NONBLOCK);
        struct sockaddr_un sa2 = {0};
        sa2.sun_family = AF_UNIX;
        strncpy(sa2.sun_path, wm->evt_path, sizeof(sa2.sun_path)-1);
        if (bind(wm->evt_fd, (struct sockaddr*)&sa2, sizeof(sa2)) < 0) {
            close(wm->evt_fd); wm->evt_fd = -1;
        } else {
            chmod(wm->evt_path, 0600);
            listen(wm->evt_fd, 32);
        }
    }
}

void ipc_cleanup(WM *wm) {
    for (int i=0; i<IPC_MAX_CLIENTS; i++)
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
    for (int i=0; i<IPC_MAX_CLIENTS; i++) {
        if (!wm->ipc_clients[i].active) continue;
        FD_SET(wm->ipc_clients[i].fd, &rfds);
        if (wm->ipc_clients[i].fd > maxfd) maxfd=wm->ipc_clients[i].fd;
    }
    struct timeval tv={0,0};
    if (select(maxfd+1,&rfds,NULL,NULL,&tv)<=0) return;

    if (FD_ISSET(wm->ipc_fd,&rfds)) {
        int cfd=accept(wm->ipc_fd,NULL,NULL);
        if (cfd>=0) {
            int f=fcntl(cfd,F_GETFL,0); fcntl(cfd,F_SETFL,f|O_NONBLOCK);
            for (int i=0; i<IPC_MAX_CLIENTS; i++)
                if (!wm->ipc_clients[i].active) {
                    wm->ipc_clients[i].fd=cfd;
                    wm->ipc_clients[i].active=true;
                    break;
                }
        }
    }
    for (int i=0; i<IPC_MAX_CLIENTS; i++) {
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

static void ipc_getstatus(WM *wm, int fd) {
    char buf[4096]; int pos=0;
    pos+=snprintf(buf+pos,sizeof(buf)-pos,
        "{\"current_workspace\":%d,\"workspaces\":[",wm->cur_ws+1);
    for (int i=0; i<MAX_WORKSPACES; i++) {
        Workspace *ws=&wm->workspaces[i];
        int cnt=0; for(Client*c=ws->clients;c;c=c->next)cnt++;
        const char *layouts[]={"tile","monocle","float"};
        pos+=snprintf(buf+pos,sizeof(buf)-pos,
            "{\"id\":%d,\"name\":\"%s\",\"clients\":%d,\"layout\":\"%s\",\"active\":%s}%s",
            i+1,ws->name,cnt,layouts[ws->layout],
            (i==wm->cur_ws)?"true":"false",(i<MAX_WORKSPACES-1)?",":"");
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

/* ── broadcast event to all subscribers ─────────────────── */
void ipc_event_emit(WM *wm, const char *event) {
    if (wm->evt_fd < 0) return;
    /* accept any pending subscriber connections */
    int cfd;
    while ((cfd = accept(wm->evt_fd, NULL, NULL)) >= 0) {
        /* write event and close — subscribers reconnect each time */
        (void)write(cfd, event, strlen(event));
        (void)write(cfd, "\n", 1);
        close(cfd);
    }
    /* also try to notify any already-connected listeners via a fresh accept loop */
}

/* ── dispatch ───────────────────────────────────────────── */
void ipc_dispatch(WM *wm, int fd, const char *msg) {
    char cmd[64]={0}, arg[MAX_CMD_LEN]={0}, arg2[MAX_CMD_LEN]={0};
    sscanf(msg, "%63s %511s %511[^\n]", cmd, arg, arg2);

    Workspace *ws = &wm->workspaces[wm->cur_ws];
    const char *ok="ok\n";

    /* ── window actions ─────────────────────────────────── */
    if      (!strcmp(cmd,"quit"))          wm_quit(wm);
    else if (!strcmp(cmd,"kill"))          client_kill(wm,ws->focused);
    else if (!strcmp(cmd,"togglefloat"))   client_toggle_float(wm,ws->focused);
    else if (!strcmp(cmd,"setfloat"))      client_set_floating(wm,ws->focused,true);
    else if (!strcmp(cmd,"settiled"))      client_set_floating(wm,ws->focused,false);
    else if (!strcmp(cmd,"fullscreen"))    client_toggle_fullscreen(wm,ws->focused);
    else if (!strcmp(cmd,"cyclelayout"))   ws_cycle_layout(wm);
    else if (!strcmp(cmd,"focusleft"))     ws_focus_dir(wm,0);
    else if (!strcmp(cmd,"focusright"))    ws_focus_dir(wm,1);
    else if (!strcmp(cmd,"focusup"))       ws_focus_dir(wm,2);
    else if (!strcmp(cmd,"focusdown"))     ws_focus_dir(wm,3);
    else if (!strcmp(cmd,"focusmaster"))   client_focus(wm,client_get_master(wm,wm->cur_ws));
    else if (!strcmp(cmd,"swapmaster"))    client_swap_with_master(wm,ws->focused);
    else if (!strcmp(cmd,"exec") && arg[0]) spawn(arg);
    else if (!strcmp(cmd,"getstatus"))     { ipc_getstatus(wm,fd); return; }
    else if (!strcmp(cmd,"subscribe"))     {
        /* print current ws line and flush — caller polls with a loop */
        char buf[256]={0}; int pos=0;
        for(int i=0;i<MAX_WORKSPACES;i++){
            Workspace *w=&wm->workspaces[i];
            bool occ=(w->clients!=NULL);
            if(!occ && i!=wm->cur_ws) continue;
            pos+=snprintf(buf+pos,sizeof(buf)-pos,"%s%s%s ",
                (i==wm->cur_ws)?">":"", w->name, occ?"*":"");
        }
        buf[pos>0?pos-1:0]='\n';
        (void)write(fd,buf,(size_t)(pos>0?pos:1));
        return;
    }

    /* ── workspace ──────────────────────────────────────── */
    else if (!strcmp(cmd,"workspace")) {
        int n=atoi(arg);
        if (n>=1&&n<=MAX_WORKSPACES) ws_switch(wm,n-1);
    }
    else if (!strcmp(cmd,"movetoworkspace")) {
        int n=atoi(arg);
        if (n>=1&&n<=MAX_WORKSPACES) ws_move_client(wm,ws->focused,n-1);
    }

    /* ── config setters (called from ltwmrc) ────────────── */
    else if (!strcmp(cmd,"config")) {
        /* ltwmc config <key> <value> */
        if (!strcmp(arg,"border_width")) {
            wm->cfg.border_width=atoi(arg2);
            /* reframe all clients */
            for(int i=0;i<MAX_WORKSPACES;i++)
                for(Client*c=wm->workspaces[i].clients;c;c=c->next) {
                    XSetWindowBorderWidth(wm->dpy,c->frame,(unsigned)wm->cfg.border_width);
                    client_update_border(wm,c);
                }
        }
        else if (!strcmp(arg,"gap")) {
            wm->cfg.gap=atoi(arg2);
            for(int i=0;i<MAX_WORKSPACES;i++) ws_tile(wm,i);
        }
        else if (!strcmp(arg,"border_normal")) {
            wm->cfg.border_normal=parse_color(wm->dpy,wm->screen,arg2);
            for(int i=0;i<MAX_WORKSPACES;i++)
                for(Client*c=wm->workspaces[i].clients;c;c=c->next)
                    client_update_border(wm,c);
        }
        else if (!strcmp(arg,"border_focused")) {
            wm->cfg.border_focused=parse_color(wm->dpy,wm->screen,arg2);
            for(int i=0;i<MAX_WORKSPACES;i++)
                for(Client*c=wm->workspaces[i].clients;c;c=c->next)
                    client_update_border(wm,c);
        }
        else if (!strcmp(arg,"border_floating")) {
            wm->cfg.border_floating=parse_color(wm->dpy,wm->screen,arg2);
            for(int i=0;i<MAX_WORKSPACES;i++)
                for(Client*c=wm->workspaces[i].clients;c;c=c->next)
                    client_update_border(wm,c);
        }
        else if (!strcmp(arg,"master_ratio")) {
            float r=atof(arg2);
            if(r>0.1f&&r<0.9f) {
                for(int i=0;i<MAX_WORKSPACES;i++)
                    wm->workspaces[i].master_ratio=r;
                for(int i=0;i<MAX_WORKSPACES;i++) ws_tile(wm,i);
            }
        }
        else if (!strcmp(arg,"layout")) {
            Layout l=LAYOUT_TILE;
            if(!strcmp(arg2,"monocle")) l=LAYOUT_MONOCLE;
            else if(!strcmp(arg2,"float")) l=LAYOUT_FLOAT;
            wm->workspaces[wm->cur_ws].layout=l;
            ws_tile(wm,wm->cur_ws);
        }
        else if (!strcmp(arg,"workspace_name")) {
            /* ltwmc config workspace_name <N> <name> */
            char nstr[8]={0}; char name[32]={0};
            sscanf(arg2,"%7s %31[^\n]",nstr,name);
            int n=atoi(nstr);
            if(n>=1&&n<=MAX_WORKSPACES)
                strncpy(wm->workspaces[n-1].name,name,31);
        }
        else if (!strcmp(arg,"float_default_w")) wm->cfg.float_default_w=atoi(arg2);
        else if (!strcmp(arg,"float_default_h")) wm->cfg.float_default_h=atoi(arg2);
    }

    (void)write(fd,ok,3);
}
