/*
 * ltwmc — LTWM IPC client
 * Usage: ltwmc <command> [args...]
 *
 * Examples:
 *   ltwmc workspace 2
 *   ltwmc togglefloat
 *   ltwmc exec "xterm"
 *   ltwmc getstatus
 *   ltwmc quit
 */
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>

int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr,
            "Usage: ltwmc <command> [args...]\n"
            "Commands:\n"
            "  workspace <N>        switch to workspace N\n"
            "  movetoworkspace <N>  move window to workspace N\n"
            "  focusleft|right|up|down\n"
            "  togglefloat          toggle floating\n"
            "  setfloat / settiled\n"
            "  fullscreen\n"
            "  cyclelayout\n"
            "  kill                 kill focused window\n"
            "  exec <cmd>           spawn command\n"
            "  getstatus            print JSON status\n"
            "  reload               reload config\n"
            "  quit                 quit ltwm\n");
        return 1;
    }

    /* build message from argv */
    char msg[1024] = {0};
    for (int i = 1; i < argc; i++) {
        if (i > 1) strncat(msg, " ", sizeof(msg)-strlen(msg)-1);
        strncat(msg, argv[i], sizeof(msg)-strlen(msg)-1);
    }

    /* find socket path */
    const char *disp = getenv("DISPLAY");
    int dnum = 0;
    if (disp) { const char *c = strchr(disp,':'); if(c) dnum=atoi(c+1); }
    char path[108];
    snprintf(path, sizeof(path), "/tmp/ltwm-%d.sock", dnum);

    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) { perror("socket"); return 1; }

    struct sockaddr_un sa = {0};
    sa.sun_family = AF_UNIX;
    strncpy(sa.sun_path, path, sizeof(sa.sun_path)-1);

    if (connect(fd, (struct sockaddr *)&sa, sizeof(sa)) < 0) {
        fprintf(stderr, "ltwmc: cannot connect to %s\n", path);
        close(fd); return 1;
    }

    write(fd, msg, strlen(msg));
    shutdown(fd, SHUT_WR);

    /* print response */
    char buf[4096]; ssize_t n;
    while ((n = read(fd, buf, sizeof(buf)-1)) > 0) {
        buf[n] = '\0';
        printf("%s", buf);
    }
    close(fd);
    return 0;
}
