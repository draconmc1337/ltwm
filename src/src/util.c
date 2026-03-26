#include "ltwm.h"

unsigned long parse_color(Display *dpy, int screen, const char *hex) {
    XColor c;
    char name[16];
    if (hex[0]=='#') snprintf(name,sizeof(name),"%s",hex);
    else             snprintf(name,sizeof(name),"#%s",hex);
    if (!XParseColor(dpy, DefaultColormap(dpy,screen), name, &c))
        return WhitePixel(dpy,screen);
    if (!XAllocColor(dpy, DefaultColormap(dpy,screen), &c))
        return WhitePixel(dpy,screen);
    return c.pixel;
}

void die(const char *fmt, ...) {
    va_list ap; va_start(ap,fmt);
    vfprintf(stderr,fmt,ap); va_end(ap);
    fputc('\n',stderr); exit(1);
}
