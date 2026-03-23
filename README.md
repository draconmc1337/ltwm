<div align="center">

# ltwm

**Lightweight Tiling Window Manager for X11**

*Tiling · Floating · IPC · sxhkd-compatible · Polybar-ready*

![version](https://img.shields.io/badge/version-0.10.0--alpha-blue)
![license](https://img.shields.io/badge/license-MIT-green)
![platform](https://img.shields.io/badge/platform-X11-orange)
![language](https://img.shields.io/badge/language-C-blue)

</div>

---

## What is ltwm?

ltwm is a minimal X11 tiling window manager written in C.  
It takes inspiration from **bspwm** in architecture — the WM itself handles only windows and layouts, while a companion CLI (`ltwmc`) handles everything else via a Unix socket.

```
sxhkd ──ltwmc──► ltwm  (keybindings)
ltwmrc ─ltwmc──► ltwm  (config on startup)
polybar ─────────► ltwm-workspaces.sh  (workspace display)
```

No keybindings hardcoded. No config format to learn. `ltwmrc` is just a shell script.

---

## Features

- **Tiling layouts**: master+stack, monocle, free float
- **Floating windows**: toggle with a keybind, auto-detected for dialogs/popups
- **IPC**: Unix socket at `/tmp/ltwm-N.sock` — scriptable from anywhere
- **sxhkd**: all keybindings handled externally, zero conflicts
- **Polybar**: workspace module with push-based updates (no polling lag)
- **EWMH**: fullscreen, active window, window type, struts (Polybar doesn't overlap)
- **pywal**: colors flow through naturally
- **Fastfetch/neofetch**: shows `ltwm 0.10.0-alpha (X11)`

---

## Dependencies

| Package | Purpose |
|---|---|
| `libx11` | X11 core |
| `libxrandr` | multi-monitor |
| `sxhkd` | keybindings |
| `polybar` | status bar |
| `picom` | compositor |

```sh
# Arch Linux
pacman -S libx11 libxrandr sxhkd polybar picom
```

---

## Build & Install

```sh
git clone https://github.com/yourname/ltwm
cd ltwm
make
sudo make install          # ltwm + ltwmc → /usr/local/bin
make install-config        # ltwmrc → ~/.config/ltwm/ltwmrc
```

---

## Configuration

Everything is configured through `~/.config/ltwm/ltwmrc` — a plain shell script:

```sh
#!/bin/sh
ltwmc config border_width    2
ltwmc config border_normal   '#45475A'
ltwmc config border_focused  '#89B4FA'
ltwmc config border_floating '#A6E3A1'
ltwmc config gap             6
ltwmc config master_ratio    0.55

# autostart (only on first launch, not on reload)
if [ "${1:-0}" -eq 0 ]; then
    sxhkd &
    polybar ltwm &
    picom --config ~/.config/picom/picom.conf &
    feh --bg-fill ~/Pictures/wall.jpg &
fi
```

Keybindings go in `~/.config/sxhkd/sxhkdrc`:

```sh
super + Return
    kitty

super + {1-9,0}
    ltwmc workspace {1-9,10}

alt + v
    ltwmc togglefloat
```

---

## ltwmc commands

```
ltwmc workspace <N>          switch to workspace N
ltwmc movetoworkspace <N>    move focused window
ltwmc togglefloat            toggle floating
ltwmc fullscreen             toggle fullscreen
ltwmc cyclelayout            tile → monocle → float
ltwmc focusleft|right|up|down
ltwmc kill                   close focused window
ltwmc exec <cmd>             spawn a command
ltwmc getstatus              JSON status output
ltwmc config <key> <value>   set config at runtime
ltwmc reload                 re-run ltwmrc
ltwmc quit                   exit ltwm
```

---

## Project structure

```
ltwm/
├── include/ltwm.h       structs, enums, declarations
├── src/
│   ├── wm.c             main loop, init, EWMH
│   ├── events.c         X event dispatch
│   ├── client.c         window management
│   ├── workspace.c      layouts, switching
│   ├── ipc.c            Unix socket server
│   ├── config.c         defaults
│   ├── spawn.c          fork/exec
│   ├── util.c           helpers
│   └── ltwmc.c          CLI client
├── ltwmrc               example config script
├── ltwm-workspaces.sh   Polybar workspace module
└── polybar/config.ini   Polybar config (pywal)
```

---

## Login manager / startx

Display manager (LightDM, SDDM): select **LTWM** from the session list.

`startx`:
```sh
echo "exec ltwm" >> ~/.xinitrc
```

---

<div align="center">
<sub>ltwm 0.10.0-alpha · X11 · C99 · ~2200 lines</sub>
</div>
