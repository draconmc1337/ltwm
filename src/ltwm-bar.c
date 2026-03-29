/*
 * ltwm-bar — standalone status bar for LTWM
 *
 * Architecture:
 *   - Connects to ltwm IPC socket to query initial state (getstatus)
 *   - Subscribes to ltwm event socket for push updates (workspace, layout, title)
 *   - Polls system stats (CPU, RAM, battery, wifi) on a timer
 *   - Renders via Xft directly onto its own X window
 *
 * Events received from ltwm:
 *   workspace N   — switched to workspace N
 *   layout NAME   — layout changed
 *   title TEXT    — focused window title changed
 *
 * Build: gcc -o ltwm-bar src/ltwm-bar.c $(pkg-config --cflags --libs x11 xft) -lm
 */

#define _GNU_SOURCE
#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/Xft/Xft.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/sysinfo.h>

#define BAR_HEIGHT       28
#define BAR_PADDING_X    6
#define BAR_FONT         "monospace:size=11"
#define MAX_WORKSPACES   10
#define STAT_INTERVAL    2
#define IPC_PATH_FMT     "/tmp/ltwm-%d.sock"
#define EVT_PATH_FMT     "/tmp/ltwm-%d.events"
#define MAX_TITLE        256
#define MAX_WS_NAME      32

#define COL_BG              "#1E1E2E"
#define COL_FG              "#CDD6F4"
#define COL_WS_ACTIVE_BG    "#89B4FA"
#define COL_WS_ACTIVE_FG    "#1E1E2E"
#define COL_WS_OCCUPIED_BG  "#313244"
#define COL_WS_OCCUPIED_FG  "#CDD6F4"

typedef struct {
    char name[MAX_WS_NAME];
    int  occupied;
    int  active;
} Workspace;

typedef struct {
    Display    *dpy;
    int         screen;
    Window      root, win;
    GC          gc;
    XftFont    *font;
    XftDraw    *draw;
    int         sw, sh;
    int         height, padding;
    char        font_name[256];
    unsigned long col_bg, col_fg;
    unsigned long col_ws_active_bg, col_ws_active_fg;
    unsigned long col_ws_occupied_bg, col_ws_occupied_fg;
    Workspace   workspaces[MAX_WORKSPACES];
    int         cur_ws;
    char        layout[32];
    char        title[MAX_TITLE];
    char        stat_cpu[16];
    char        stat_ram[16];
    char        stat_bat[16];
    char        stat_net[64];
    char        stat_time[32];
    char        ipc_path[108];
    char        evt_path[108];
    long        cpu_prev_total, cpu_prev_idle;
} Bar;

static unsigned long parse_color(Bar *b, const char *hex) {
    XColor c;
    char name[16];
    if (!hex || !hex[0]) return WhitePixel(b->dpy, b->screen);
    snprintf(name, sizeof(name), hex[0]=='#' ? "%s" : "#%s", hex);
    if (!XParseColor(b->dpy, DefaultColormap(b->dpy, b->screen), name, &c))
        return WhitePixel(b->dpy, b->screen);
    if (!XAllocColor(b->dpy, DefaultColormap(b->dpy, b->screen), &c))
        return WhitePixel(b->dpy, b->screen);
    return c.pixel;
}

static XftColor xft_col(Bar *b, unsigned long pixel) {
    XColor xc = {0}; XftColor c;
    xc.pixel = pixel;
    XQueryColor(b->dpy, DefaultColormap(b->dpy, b->screen), &xc);
    c.color.red=xc.red; c.color.green=xc.green;
    c.color.blue=xc.blue; c.color.alpha=0xFFFF;
    c.pixel=pixel; return c;
}

static int tw(Bar *b, const char *s) {
    if (!b->font||!s||!*s) return (int)strlen(s)*8;
    XGlyphInfo e;
    XftTextExtentsUtf8(b->dpy, b->font, (FcChar8*)s, strlen(s), &e);
    return e.xOff;
}

static void fill(Bar *b, int x, int y, int w, int h, unsigned long col) {
    XSetForeground(b->dpy, b->gc, col);
    XFillRectangle(b->dpy, b->win, b->gc, x, y, (unsigned)w, (unsigned)h);
}

static int btext(Bar *b, int x, const char *s,
                 unsigned long fg, unsigned long bg, int px) {
    if (!s||!*s) return x;
    int w   = tw(b,s) + 2*px;
    int asc = b->font ? b->font->ascent  : 12;
    int dsc = b->font ? b->font->descent : 2;
    int ty  = (b->height - asc - dsc)/2 + asc;
    fill(b, x, 0, w, b->height, bg);
    if (b->draw && b->font) {
        XftColor c = xft_col(b, fg);
        XftDrawStringUtf8(b->draw, &c, b->font,
                          x+px, ty, (FcChar8*)s, strlen(s));
    }
    return x + w;
}

static int ipc_cmd(const char *path, const char *cmd, char *out, int outsz) {
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd<0) return -1;
    struct sockaddr_un sa; memset(&sa,0,sizeof(sa));
    sa.sun_family = AF_UNIX;
    strncpy(sa.sun_path, path, sizeof(sa.sun_path)-1);
    if (connect(fd,(struct sockaddr*)&sa,sizeof(sa))<0) { close(fd); return -1; }
    write(fd, cmd, strlen(cmd)); write(fd, "\n", 1);
    int n=0;
    if (out&&outsz>0) {
        n=(int)read(fd,out,outsz-1);
        if (n>0) out[n]='\0'; else out[0]='\0';
    }
    close(fd); return n;
}

static void parse_status(Bar *b, const char *j) {
    const char *p;
    p=strstr(j,"\"current_workspace\":"); if(p) b->cur_ws=atoi(p+20)-1;
    p=strstr(j,"\"workspaces\":["); if(!p) return; p+=14;
    for(int i=0;i<MAX_WORKSPACES;i++){
        const char *obj=strchr(p,'{'); if(!obj) break; p=obj+1;
        const char *idp=strstr(p,"\"id\":"); if(!idp) break;
        int id=atoi(idp+5)-1; if(id<0||id>=MAX_WORKSPACES) break;
        const char *nm=strstr(p,"\"name\":\"");
        if(nm){ nm+=8; const char *e=strchr(nm,'"');
            int l=e?(int)(e-nm):0; if(l>=MAX_WS_NAME)l=MAX_WS_NAME-1;
            strncpy(b->workspaces[id].name,nm,l); b->workspaces[id].name[l]='\0'; }
        const char *cl=strstr(p,"\"clients\":"); if(cl) b->workspaces[id].occupied=atoi(cl+10)>0;
        const char *ac=strstr(p,"\"active\":"); if(ac) b->workspaces[id].active=strncmp(ac+9,"true",4)==0;
        const char *end=strchr(p,'}'); if(!end) break; p=end+1;
    }
    p=strstr(j,"\"focused\":"); if(!p) return;
    const char *ft=strstr(p,"\"title\":\"");
    if(ft){ ft+=9; const char *e=strchr(ft,'"');
        int l=e?(int)(e-ft):0; if(l>=MAX_TITLE)l=MAX_TITLE-1;
        strncpy(b->title,ft,l); b->title[l]='\0'; }
    /* layout — use first workspace's layout as current */
    const char *ly=strstr(j,"\"layout\":\"");
    if(ly){ ly+=10; const char *e=strchr(ly,'"');
        int l=e?(int)(e-ly):0; if(l>=32)l=31;
        strncpy(b->layout,ly,l); b->layout[l]='\0'; }
}

static void update_cpu(Bar *b) {
    FILE *f=fopen("/proc/stat","r"); if(!f){snprintf(b->stat_cpu,sizeof(b->stat_cpu),"CPU:--");return;}
    long u,n,s,id,io,irq,si;
    fscanf(f,"cpu %ld %ld %ld %ld %ld %ld %ld",&u,&n,&s,&id,&io,&irq,&si);
    fclose(f);
    long tot=u+n+s+id+io+irq+si, dt=tot-b->cpu_prev_total, di=id-b->cpu_prev_idle;
    int pct=dt>0?(int)((dt-di)*100/dt):0;
    b->cpu_prev_total=tot; b->cpu_prev_idle=id;
    snprintf(b->stat_cpu,sizeof(b->stat_cpu),"CPU: %d%%",pct);
}

static void update_ram(Bar *b) {
    struct sysinfo si;
    if(sysinfo(&si)<0){snprintf(b->stat_ram,sizeof(b->stat_ram),"RAM:--");return;}
    int pct=(int)((si.totalram-si.freeram-si.bufferram)*100/si.totalram);
    snprintf(b->stat_ram,sizeof(b->stat_ram),"RAM: %d%%",pct);
}

static void update_bat(Bar *b) {
    FILE *f=fopen("/sys/class/power_supply/BAT0/capacity","r");
    if(!f){b->stat_bat[0]='\0';return;}
    int cap; fscanf(f,"%d",&cap); fclose(f);
    char st[16]="?";
    FILE *sf=fopen("/sys/class/power_supply/BAT0/status","r");
    if(sf){fscanf(sf,"%15s",st);fclose(sf);}
    snprintf(b->stat_bat,sizeof(b->stat_bat),"BAT: %s%d%%",
             strncmp(st,"Charging",8)==0?"+":"",cap);
}

static void update_net(Bar *b) {
    FILE *f=popen("iwgetid -r 2>/dev/null","r");
    if(!f){b->stat_net[0]='\0';return;}
    char ssid[64]={0};
    if(fgets(ssid,sizeof(ssid),f)) {
        ssid[strcspn(ssid,"\n")]='\0';
        if(ssid[0]) snprintf(b->stat_net,sizeof(b->stat_net)," %s",ssid);
        else        snprintf(b->stat_net,sizeof(b->stat_net)," --");
    } else b->stat_net[0]='\0';
    pclose(f);
}

static void update_clock(Bar *b) {
    time_t t=time(NULL); struct tm *tm=localtime(&t);
    strftime(b->stat_time,sizeof(b->stat_time),"%H:%M  %a %d/%m",tm);
}

static void update_stats(Bar *b) {
    update_cpu(b); update_ram(b); update_bat(b);
    update_net(b); update_clock(b);
}

static const char *layout_icon(const char *n) {
    if(!n||!*n)                    return "[]";
    if(strncmp(n,"vtile",5)==0)    return "[v]";
    if(strncmp(n,"tile",4)==0)     return "[]=";
    if(strncmp(n,"vstripe",7)==0)  return "|||";
    if(strncmp(n,"hstripe",7)==0)  return "===";
    if(strncmp(n,"monocle",7)==0)  return "[M]";
    if(strncmp(n,"float",5)==0)    return "><>";
    return n;
}

static void bar_draw(Bar *b) {
    int sw=b->sw, bh=b->height, px=b->padding;
    fill(b,0,0,sw,bh,b->col_bg);
    int lx=px, rx=sw-px;

    /* workspaces */
    for(int i=0;i<MAX_WORKSPACES;i++){
        Workspace *ws=&b->workspaces[i];
        if(!ws->active&&!ws->occupied) continue;
        unsigned long fg=ws->active?b->col_ws_active_fg:b->col_ws_occupied_fg;
        unsigned long bg=ws->active?b->col_ws_active_bg:b->col_ws_occupied_bg;
        lx=btext(b,lx,ws->name,fg,bg,7); lx+=1;
    }
    lx+=4;

    /* layout */
    lx=btext(b,lx,layout_icon(b->layout),b->col_fg,b->col_bg,4);
    lx+=6;

    /* right: clock */
    { int w=tw(b,b->stat_time)+16; rx-=w; btext(b,rx,b->stat_time,b->col_fg,b->col_bg,8); rx-=4; }
    /* battery */
    if(b->stat_bat[0]){ int w=tw(b,b->stat_bat)+16; rx-=w; btext(b,rx,b->stat_bat,b->col_fg,b->col_bg,8); rx-=4; }
    /* RAM */
    { int w=tw(b,b->stat_ram)+16; rx-=w; btext(b,rx,b->stat_ram,b->col_fg,b->col_bg,8); rx-=4; }
    /* CPU */
    { int w=tw(b,b->stat_cpu)+16; rx-=w; btext(b,rx,b->stat_cpu,b->col_fg,b->col_bg,8); rx-=4; }
    /* network */
    if(b->stat_net[0]){ int w=tw(b,b->stat_net)+16; rx-=w; btext(b,rx,b->stat_net,b->col_fg,b->col_bg,8); rx-=4; }

    /* center: title */
    if(b->title[0]){
        int w=tw(b,b->title), cx=(sw-w)/2;
        if(cx>lx+8&&cx+w<rx-8) btext(b,cx,b->title,b->col_fg,b->col_bg,0);
    }

    XFlush(b->dpy);
}

static void bar_init_window(Bar *b) {
    XSetWindowAttributes swa;
    swa.background_pixel=b->col_bg;
    swa.override_redirect=True;
    swa.event_mask=ExposureMask;
    b->win=XCreateWindow(b->dpy,b->root,0,0,b->sw,b->height,0,
        DefaultDepth(b->dpy,b->screen),InputOutput,
        DefaultVisual(b->dpy,b->screen),
        CWBackPixel|CWOverrideRedirect|CWEventMask,&swa);

    Atom wtype=XInternAtom(b->dpy,"_NET_WM_WINDOW_TYPE",False);
    Atom dock =XInternAtom(b->dpy,"_NET_WM_WINDOW_TYPE_DOCK",False);
    XChangeProperty(b->dpy,b->win,wtype,XA_ATOM,32,PropModeReplace,(unsigned char*)&dock,1);
    long strut[12]={0}; strut[2]=b->height; strut[9]=b->sw-1;
    Atom sp=XInternAtom(b->dpy,"_NET_WM_STRUT_PARTIAL",False);
    XChangeProperty(b->dpy,b->win,sp,XA_CARDINAL,32,PropModeReplace,(unsigned char*)strut,12);
    Atom s=XInternAtom(b->dpy,"_NET_WM_STRUT",False);
    XChangeProperty(b->dpy,b->win,s,XA_CARDINAL,32,PropModeReplace,(unsigned char*)strut,4);

    b->gc=XCreateGC(b->dpy,b->win,0,NULL);
    b->font=XftFontOpenName(b->dpy,b->screen,b->font_name);
    if(!b->font){
        fprintf(stderr,"ltwm-bar: font failed: %s\n",b->font_name);
        b->font=XftFontOpenName(b->dpy,b->screen,"monospace:size=11");
    }
    b->draw=XftDrawCreate(b->dpy,b->win,DefaultVisual(b->dpy,b->screen),
                          DefaultColormap(b->dpy,b->screen));
    XMapRaised(b->dpy,b->win);
}

static void handle_event(Bar *b, const char *ev) {
    if(!ev||!*ev) return;
    if(strncmp(ev,"workspace ",10)==0){
        char json[4096]={0};
        if(ipc_cmd(b->ipc_path,"getstatus",json,sizeof(json))>0) parse_status(b,json);
    } else if(strncmp(ev,"layout ",7)==0){
        strncpy(b->layout,ev+7,sizeof(b->layout)-1);
    } else if(strncmp(ev,"title ",6)==0){
        strncpy(b->title,ev+6,sizeof(b->title)-1);
        b->title[sizeof(b->title)-1]='\0';
    }
}

int main(int argc, char *argv[]) {
    Bar b; memset(&b,0,sizeof(b));

    int dnum=0;
    const char *disp=getenv("DISPLAY");
    if(disp){ const char *p=disp; while(*p&&(*p<'0'||*p>'9'))p++; if(*p)dnum=atoi(p); }
    if(argc>1) dnum=atoi(argv[1]);

    b.height=BAR_HEIGHT; b.padding=BAR_PADDING_X;
    strncpy(b.font_name,BAR_FONT,sizeof(b.font_name)-1);
    snprintf(b.ipc_path,sizeof(b.ipc_path),IPC_PATH_FMT,dnum);
    snprintf(b.evt_path,sizeof(b.evt_path),EVT_PATH_FMT,dnum);

    b.dpy=XOpenDisplay(NULL);
    if(!b.dpy){fprintf(stderr,"ltwm-bar: cannot open display\n");return 1;}
    b.screen=DefaultScreen(b.dpy);
    b.root=RootWindow(b.dpy,b.screen);
    b.sw=DisplayWidth(b.dpy,b.screen);
    b.sh=DisplayHeight(b.dpy,b.screen);

    b.col_bg            =parse_color(&b,COL_BG);
    b.col_fg            =parse_color(&b,COL_FG);
    b.col_ws_active_bg  =parse_color(&b,COL_WS_ACTIVE_BG);
    b.col_ws_active_fg  =parse_color(&b,COL_WS_ACTIVE_FG);
    b.col_ws_occupied_bg=parse_color(&b,COL_WS_OCCUPIED_BG);
    b.col_ws_occupied_fg=parse_color(&b,COL_WS_OCCUPIED_FG);

    const char *dn[]={"I","II","III","IV","V","VI","VII","VIII","IX","X"};
    for(int i=0;i<MAX_WORKSPACES;i++) strncpy(b.workspaces[i].name,dn[i],MAX_WS_NAME-1);

    /* wait for ltwm to be ready */
    for(int i=0;i<20;i++){
        char json[4096]={0};
        if(ipc_cmd(b.ipc_path,"getstatus",json,sizeof(json))>0){ parse_status(&b,json); break; }
        usleep(200000);
    }

    bar_init_window(&b);
    update_stats(&b);
    bar_draw(&b);

    int xfd=ConnectionNumber(b.dpy);
    time_t last_stat=time(NULL);

    while(1){
        while(XPending(b.dpy)){
            XEvent ev; XNextEvent(b.dpy,&ev);
            if(ev.type==Expose&&ev.xexpose.count==0) bar_draw(&b);
        }

        /* subscribe to ltwm event socket — reconnect each time (push model) */
        int efd=socket(AF_UNIX,SOCK_STREAM,0);
        if(efd>=0){
            struct sockaddr_un sa; memset(&sa,0,sizeof(sa));
            sa.sun_family=AF_UNIX;
            strncpy(sa.sun_path,b.evt_path,sizeof(sa.sun_path)-1);
            if(connect(efd,(struct sockaddr*)&sa,sizeof(sa))<0){ close(efd); efd=-1; }
        }

        fd_set rfds; FD_ZERO(&rfds); FD_SET(xfd,&rfds);
        int maxfd=xfd;
        if(efd>=0){ FD_SET(efd,&rfds); if(efd>maxfd) maxfd=efd; }
        struct timeval tv={STAT_INTERVAL,0};
        select(maxfd+1,&rfds,NULL,NULL,&tv);

        if(efd>=0&&FD_ISSET(efd,&rfds)){
            char buf[512]={0};
            ssize_t n=read(efd,buf,sizeof(buf)-1);
            if(n>0){ buf[n]='\0'; buf[strcspn(buf,"\n")]='\0'; handle_event(&b,buf); bar_draw(&b); }
        }
        if(efd>=0) close(efd);

        time_t now=time(NULL);
        if(now-last_stat>=STAT_INTERVAL){ last_stat=now; update_stats(&b); bar_draw(&b); }

        while(XPending(b.dpy)){
            XEvent ev; XNextEvent(b.dpy,&ev);
            if(ev.type==Expose&&ev.xexpose.count==0) bar_draw(&b);
        }
    }
    return 0;
}
