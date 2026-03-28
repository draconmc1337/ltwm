#include "ltwm.h"

static int ux(WM *wm) { return wm->strut_left; }
static int uy(WM *wm) { return wm->strut_top; }
static int uw(WM *wm) { return wm->sw - wm->strut_left - wm->strut_right; }
static int uh(WM *wm) { return wm->sh - wm->strut_top  - wm->strut_bottom; }

static int collect(WM *wm, int id, Client **out) {
    int n=0;
    for (Client *c=wm->workspaces[id].clients;c;c=c->next)
        if (!c->floating&&!c->fullscreen&&c->workspace==id) out[n++]=c;
    return n;
}

static void lay_tile(WM *wm,int id,Client **t,int n){
    int gap=wm->cfg.gap,bw=wm->cfg.border_width;
    int ox=ux(wm),oy=uy(wm),sw=uw(wm),sh=uh(wm);
    Workspace *ws=&wm->workspaces[id];
    int mc=(ws->master_count<n)?ws->master_count:n; if(!mc)mc=1;
    int sc=n-mc; float r=ws->master_ratio;
    if(sc==0){
        int uw2=sw-2*gap-2*bw;
        int uh2=(sh-(mc+1)*gap)/mc-2*bw; if(uh2<1)uh2=1;
        for(int i=0;i<mc;i++) client_move_resize(wm,t[i],ox+gap,oy+gap+i*(uh2+gap+2*bw),uw2,uh2);
    } else {
        int mw=(int)((float)(sw-3*gap)*r)-2*bw;
        int sw2=sw-mw-3*gap-2*bw; int sx=gap+mw+gap+2*bw;
        int mh=(sh-(mc+1)*gap)/mc-2*bw; if(mh<1)mh=1;
        for(int i=0;i<mc;i++) client_move_resize(wm,t[i],ox+gap,oy+gap+i*(mh+gap+2*bw),mw,mh);
        int seh=sc>0?(sh-(sc+1)*gap)/sc-2*bw:sh-2*gap-2*bw; if(seh<1)seh=1;
        for(int i=0;i<sc;i++) client_move_resize(wm,t[mc+i],ox+sx,oy+gap+i*(seh+gap+2*bw),sw2,seh);
    }
}

static void lay_vtile(WM *wm,int id,Client **t,int n){
    int gap=wm->cfg.gap,bw=wm->cfg.border_width;
    int ox=ux(wm),oy=uy(wm),sw=uw(wm),sh=uh(wm);
    Workspace *ws=&wm->workspaces[id];
    int mc=(ws->master_count<n)?ws->master_count:n; if(!mc)mc=1;
    int sc=n-mc; float r=ws->master_ratio;
    if(sc==0){
        int cw=(sw-(mc+1)*gap)/mc-2*bw; if(cw<1)cw=1;
        int ch=sh-2*gap-2*bw;
        for(int i=0;i<mc;i++) client_move_resize(wm,t[i],ox+gap+i*(cw+gap+2*bw),oy+gap,cw,ch);
    } else {
        int mh=(int)((float)(sh-3*gap)*r)-2*bw;
        int sh2=sh-mh-3*gap-2*bw; int sy=gap+mh+gap+2*bw;
        int mw=(sw-(mc+1)*gap)/mc-2*bw; if(mw<1)mw=1;
        for(int i=0;i<mc;i++) client_move_resize(wm,t[i],ox+gap+i*(mw+gap+2*bw),oy+gap,mw,mh);
        int sew=sc>0?(sw-(sc+1)*gap)/sc-2*bw:sw-2*gap-2*bw; if(sew<1)sew=1;
        for(int i=0;i<sc;i++) client_move_resize(wm,t[mc+i],ox+gap+i*(sew+gap+2*bw),oy+sy,sew,sh2);
    }
}

static void lay_vstripes(WM *wm,int id,Client **t,int n){
    if(n<=0) return;
    int gap=wm->cfg.gap,bw=wm->cfg.border_width;
    int ox=ux(wm),oy=uy(wm),sw=uw(wm),sh=uh(wm);
    int cw=(sw-(n+1)*gap)/n-2*bw; if(cw<1)cw=1;
    int ch=sh-2*gap-2*bw;
    for(int i=0;i<n;i++) client_move_resize(wm,t[i],ox+gap+i*(cw+gap+2*bw),oy+gap,cw,ch);
}

static void lay_hstripes(WM *wm,int id,Client **t,int n){
    if(n<=0) return;
    int gap=wm->cfg.gap,bw=wm->cfg.border_width;
    int ox=ux(wm),oy=uy(wm),sw=uw(wm),sh=uh(wm);
    int cw=sw-2*gap-2*bw;
    int ch=(sh-(n+1)*gap)/n-2*bw; if(ch<1)ch=1;
    for(int i=0;i<n;i++) client_move_resize(wm,t[i],ox+gap,oy+gap+i*(ch+gap+2*bw),cw,ch);
}

static void lay_monocle(WM *wm,int id,Client **t,int n){
    int gap=wm->cfg.gap,bw=wm->cfg.border_width;
    int ox=ux(wm),oy=uy(wm),sw=uw(wm),sh=uh(wm);
    Workspace *ws=&wm->workspaces[id];
    for(int i=0;i<n;i++){
        if(ws->focused==t[i])
            client_move_resize(wm,t[i],ox+gap,oy+gap,sw-2*gap-2*bw,sh-2*gap-2*bw);
        else
            XMoveWindow(wm->dpy,t[i]->frame,-sw*2,0);
    }
}

void ws_tile(WM *wm, int id) {
    /* struts cached — only update on dock events, not every tile */
    Workspace *ws=&wm->workspaces[id];
    Client *tiled[MAX_CLIENTS]; int n=collect(wm,id,tiled);

    if(n>0) switch(ws->layout){
    case LAYOUT_TILE:     lay_tile(wm,id,tiled,n);     break;
    case LAYOUT_VTILE:    lay_vtile(wm,id,tiled,n);    break;
    case LAYOUT_VSTRIPES: lay_vstripes(wm,id,tiled,n); break;
    case LAYOUT_HSTRIPES: lay_hstripes(wm,id,tiled,n); break;
    case LAYOUT_MONOCLE:  lay_monocle(wm,id,tiled,n);  break;
    case LAYOUT_FLOAT: default: break;
    }

    for(Client*c=ws->clients;c;c=c->next)
        if(c->floating&&!c->fullscreen&&c->workspace==id)
            XRaiseWindow(wm->dpy,c->frame);
    if(ws->focused&&ws->focused->workspace==id&&
       (ws->focused->floating||ws->focused->fullscreen))
        XRaiseWindow(wm->dpy,ws->focused->frame);
    for(Client*c=ws->clients;c;c=c->next)
        if(c->workspace==id) client_update_border(wm,c);
}

void ws_switch(WM *wm, int id) {
    if(id<0||id>=MAX_WORKSPACES||id==wm->cur_ws) return;
    int old=wm->cur_ws;
    wm->cur_ws=id;

    /* tile BEFORE map so geometry is correct immediately */
    ws_tile(wm,id);

    /* unmap old, map new */
    for(Client*c=wm->workspaces[old].clients;c;c=c->next)
        if(c->frame) { c->ignore_unmap++; XUnmapWindow(wm->dpy,c->frame); }
    for(Client*c=wm->workspaces[id].clients;c;c=c->next)
        if(c->frame) XMapWindow(wm->dpy,c->frame);

    XSync(wm->dpy,False);

    Client *focus=wm->workspaces[id].focused
                  ?wm->workspaces[id].focused
                  :wm->workspaces[id].clients;
    client_focus(wm,focus);

    char buf[64]; snprintf(buf,sizeof(buf),"workspace %d",id+1);
    ipc_event_emit(wm,buf);
    polybar_push_workspaces(wm);  /* push thẳng vào Polybar IPC pipe, 0 lag */
}

void ws_move_client(WM *wm, Client *c, int id) {
    if(!c||id<0||id>=MAX_WORKSPACES||c->workspace==id) return;
    int old=c->workspace;
    client_remove(wm,c);
    c->ignore_unmap++;
    XUnmapWindow(wm->dpy,c->frame);
    c->workspace=id;
    client_add(wm,c);
    ws_tile(wm,old);
    ws_tile(wm,id);
    client_focus(wm,wm->workspaces[old].focused
                    ?wm->workspaces[old].focused
                    :wm->workspaces[old].clients);
}

void ws_focus_dir(WM *wm, int dir) {
    Workspace *ws=&wm->workspaces[wm->cur_ws];
    if(!ws->clients) return;
    if(!ws->focused){client_focus(wm,ws->clients);return;}
    Client *best=NULL; int bscore=INT32_MAX;
    int cx=ws->focused->fx+ws->focused->fw/2;
    int cy=ws->focused->fy+ws->focused->fh/2;
    for(Client*c=ws->clients;c;c=c->next){
        if(c==ws->focused||c->workspace!=wm->cur_ws) continue;
        int tx=c->fx+c->fw/2,ty=c->fy+c->fh/2;
        int dx=tx-cx,dy=ty-cy,score=INT32_MAX;
        switch(dir){
        case 0: if(dx<0)score=-dx+abs(dy);break;
        case 1: if(dx>0)score= dx+abs(dy);break;
        case 2: if(dy<0)score=-dy+abs(dx);break;
        case 3: if(dy>0)score= dy+abs(dx);break;
        }
        if(score<bscore){bscore=score;best=c;}
    }
    if(best) client_focus(wm,best);
}

void ws_cycle_layout(WM *wm) {
    Workspace *ws=&wm->workspaces[wm->cur_ws];
    ws->layout=(Layout)(((int)ws->layout+1)%(int)LAYOUT_COUNT);
    ws_tile(wm,wm->cur_ws);
    const char *names[]={"tile","vtile","vstripes","hstripes","monocle","float"};
    char buf[64]; snprintf(buf,sizeof(buf),"layout %s",names[(int)ws->layout]);
    ipc_event_emit(wm,buf);
}
