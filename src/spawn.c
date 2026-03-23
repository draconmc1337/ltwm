#include "ltwm.h"

void spawn(const char *cmd) {
    if (!cmd || !*cmd) return;
    if (fork() == 0) {
        if (fork() == 0) {
            setsid();
            execl("/bin/sh", "/bin/sh", "-c", cmd, (char *)NULL);
            _exit(127);
        }
        _exit(0);
    }
    wait(NULL);
}

void spawn_autostart(WM *wm) {
    for (int i = 0; i < wm->cfg.n_autostart; i++) {
        spawn(wm->cfg.autostart[i]);
        usleep(80000);
    }
}
