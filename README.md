# LTWM — Lightweight Tiling Window Manager

> **⚠ Alpha software.** LTWM is under active development. Expect bugs, missing features, and breaking config changes between versions. Not recommended as a daily driver yet — unless you enjoy fixing things.

A minimal, fast X11 tiling window manager written in C. ~2200 lines of code.  
Configured via a shell script (`ltwmrc`) and controlled at runtime through a Unix socket IPC (`ltwmc`).

```
ltwm 0.11.0-alpha
```

---

## Features

- **4 tiling layouts** — Tiled Master, Vertical Master, Vertical Stripes, Horizontal Stripes
- **Focus-follows-mouse** (sloppy focus) — hover to focus, click to raise floating windows
- **10 workspaces** with custom names (numbers, Roman numerals, Nerd Font icons, anything)
- **Floating & fullscreen** per-window, togglable at runtime
- **Built-in bar** with Xft font rendering, pywal color support, custom right-side commands
- **Polybar integration** — EWMH `_NET_CURRENT_DESKTOP` + `_NET_DESKTOP_NAMES` for `internal/xworkspaces`, zero lag
- **IPC** via Unix socket — query status, configure everything, trigger actions, all from shell
- **pywal** support — colors sourced from `~/.cache/wal/colors.sh` at startup
- **sxhkd**-friendly — no built-in keybindings, full control from your hotkey daemon

---

## Dependencies

| Package | Purpose |
|---------|---------|
| `libx11` | X11 core |
| `libxrandr` | Multi-monitor support |
| `libxft` | Font rendering in native bar |
| `gcc`, `make` | Build |
| `sxhkd` | Keybind daemon (recommended) |
| `polybar` | Status bar (optional, native bar available) |
| `feh` | Wallpaper (optional) |
| `picom` | Compositor (optional) |

---

## Installation

```sh
git clone https://github.com/draconmc1337/ltwm
cd ltwm
make
sudo make install          # installs ltwm + ltwmc to /usr/local/bin
make install-config        # copies ltwmrc to ~/.config/ltwm/ltwmrc
```

Select **LTWM** from your display manager session list, or add `exec ltwm` to `~/.xinitrc`.

### Uninstall

```sh
sudo make uninstall
```

---

## Configuration

Config lives at `~/.config/ltwm/ltwmrc` — a plain shell script executed on startup.  
Every setting is applied via `ltwmc config <key> <value>`. Reload anytime with `ltwmc reload`.

```sh
#!/bin/sh
ltwmc() { /usr/local/bin/ltwmc "$@"; }

# borders
ltwmc config border_width    2
ltwmc config border_active   '#89B4FA'
ltwmc config border_inactive '#45475A'

# layout
ltwmc config gap          6
ltwmc config master_ratio 0.55

# workspace names — index followed by display name
ltwmc config workspace_name '1 I'
ltwmc config workspace_name '2 II'
ltwmc config workspace_name '3 III'

# bar — set all options first, then bar_commit creates it in one shot
ltwmc config bar_enable   1
ltwmc config bar_height   28
ltwmc config bar_font     'JetBrains Mono:size=12'
ltwmc config bar_bg       '#1E1E2E'
ltwmc config bar_fg       '#CDD6F4'
ltwmc config bar_commit   # create bar here, not before

# autostart — event loop is already running, & is safe
sxhkd &
feh --bg-fill ~/Pictures/wallpaper.png &
```

### All config keys

#### Borders

| Key | Value | Description |
|-----|-------|-------------|
| `border_width` | integer | Border width in pixels |
| `border_active` | `#rrggbb` | Focused window border color |
| `border_inactive` | `#rrggbb` | Unfocused window border color |

#### Layout

| Key | Value | Description |
|-----|-------|-------------|
| `gap` | integer | Gap between windows in pixels |
| `master_ratio` | `0.1`–`0.9` | Master area width/height ratio |
| `float_default_w` | integer | Default floating window width |
| `float_default_h` | integer | Default floating window height |

#### Workspaces

```sh
ltwmc config workspace_name '1 I'     # index + space + display name
ltwmc config workspace_name '2 '     # Nerd Font icon example
```

#### Native bar

> Set all bar options **before** calling `bar_commit`. This ensures the bar is created once with the correct font and colors — no flash on startup.

Full example with pywal + fallback colors:

```sh
# ── bar ─────────────────────────────────────────────────────
ltwmc config bar_enable   1
ltwmc config bar_height   28
ltwmc config bar_padding_x 6
ltwmc config bar_font     'JetBrains Mono:size=12'
# other Xft font examples:
# ltwmc config bar_font 'IosevkaNerdFont:size=14'
# ltwmc config bar_font 'monospace:size=11'

# read pywal colors — falls back to catppuccin mocha if not installed
. "${HOME}/.cache/wal/colors.sh" 2>/dev/null || true
ltwmc config bar_bg             "${color0:-#1E1E2E}"
ltwmc config bar_fg             "${foreground:-#CDD6F4}"
ltwmc config bar_ws_active_bg   "${color4:-#89B4FA}"
ltwmc config bar_ws_active_fg   "${color0:-#1E1E2E}"
ltwmc config bar_ws_occupied_bg "${color8:-#313244}"
ltwmc config bar_ws_occupied_fg "${foreground:-#CDD6F4}"

# left side
ltwmc config bar_show_layout  1   # show current layout: []=  [v]  |||  ===
ltwmc config bar_show_version 0   # show "ltwm x.x.x-alpha" — usually off

# right side commands — each runs every 2 seconds, output shown separated by sep
ltwmc config bar_clear_cmds           # clear any previously set commands
ltwmc config bar_cmd_sep ' | '        # separator between command outputs

ltwmc config bar_add_cmd 'iwgetid -r 2>/dev/null || echo "--"'
ltwmc config bar_add_cmd 'amixer get Master 2>/dev/null | grep -oP "\d+(?=%)" | tail -1 | sed "s/^/vol: /"'
ltwmc config bar_add_cmd 'cat /sys/class/power_supply/BAT0/capacity 2>/dev/null | sed "s/$/%/" || true'
ltwmc config bar_add_cmd 'date "+%H:%M  %a %d/%m"'

# custom CPU/RAM example (as seen in screenshot):
# ltwmc config bar_add_cmd 'echo "CPU: $(grep -o "^[^ ]*" /proc/loadavg)%"'
# ltwmc config bar_add_cmd 'free | awk "/Mem:/{printf \"RAM: %.0f%%\", \$3/\$2*100}"'

# create bar — must be called LAST after all bar_* config above
ltwmc config bar_commit
```

| Key | Value | Description |
|-----|-------|-------------|
| `bar_enable` | `0` / `1` | Enable native bar |
| `bar_height` | integer | Bar height in pixels |
| `bar_padding_x` | integer | Horizontal padding |
| `bar_font` | Xft string | e.g. `JetBrains Mono:size=12` |
| `bar_bg` | `#rrggbb` | Background color |
| `bar_fg` | `#rrggbb` | Foreground color |
| `bar_ws_active_bg` | `#rrggbb` | Active workspace background |
| `bar_ws_active_fg` | `#rrggbb` | Active workspace foreground |
| `bar_ws_occupied_bg` | `#rrggbb` | Occupied workspace background |
| `bar_ws_occupied_fg` | `#rrggbb` | Occupied workspace foreground |
| `bar_show_layout` | `0` / `1` | Show current layout indicator |
| `bar_show_version` | `0` / `1` | Show version string |
| `bar_cmd_sep` | string | Separator between right-side commands |
| `bar_clear_cmds` | — | Clear all right-side commands |
| `bar_add_cmd` | shell string | Add a right-side command (runs every 2s) |
| `bar_commit` | — | Create/recreate bar with current config — call this last |

#### Polybar integration

ltwm sets `_NET_CURRENT_DESKTOP` and `_NET_DESKTOP_NAMES` on every workspace switch.  
Use Polybar's `internal/xworkspaces` module — no scripts, no hooks, instant updates.

```ini
[module/ltwm-workspaces]
type = internal/xworkspaces
pin-workspaces = false

label-active          = %name%
label-active-foreground = #89B4FA
label-active-padding  = 1

label-occupied        = %name%
label-occupied-foreground = #6b8e98
label-occupied-padding = 1

label-empty =
```

To disable the native bar and use Polybar instead:

```sh
ltwmc config bar_enable 0
# polybar & in autostart
```

#### pywal

ltwmrc sources `~/.cache/wal/colors.sh` automatically. Colors fall back to Catppuccin Mocha defaults if pywal is not installed.

```sh
. "${HOME}/.cache/wal/colors.sh" 2>/dev/null || true
ltwmc config bar_bg "${color0:-#1E1E2E}"
ltwmc config bar_fg "${foreground:-#CDD6F4}"
```

---

## IPC — ltwmc

All runtime control goes through `ltwmc`. The socket lives at `/tmp/ltwm-<display>.sock`.

### Actions

```sh
ltwmc exec "kitty"              # spawn a command
ltwmc kill                      # kill focused window
ltwmc togglefloating            # toggle floating on focused window
ltwmc setfloating               # force floating
ltwmc settiled                  # force tiled
ltwmc fullscreen                # toggle fullscreen
ltwmc cyclelayout               # cycle: Tile → VTile → VStripes → HStripes
ltwmc focusleft                 # focus window to the left
ltwmc focusright
ltwmc focusup
ltwmc focusdown
ltwmc focusmaster               # focus master window
ltwmc swapmaster                # swap focused window with master
ltwmc workspace 3               # switch to workspace 3
ltwmc movetoworkspace 3         # move focused window to workspace 3
ltwmc master_ratio_inc          # increase master ratio by 0.05
ltwmc master_ratio_dec          # decrease master ratio by 0.05
ltwmc reload                    # re-execute ltwmrc
ltwmc quit                      # exit ltwm
```

### Status

```sh
ltwmc getstatus                 # JSON: workspaces, focused window, layout
ltwmc bar_workspaces            # Polybar-formatted workspace string
```

### Example sxhkd config

```
# terminal
super + Return
    kitty

# launcher
super + r
    rofi -show drun

# close window
super + q
    ltwmc kill

# layouts
super + j
    ltwmc cyclelayout

# focus
super + {h,l,k,j}
    ltwmc focus{left,right,up,down}

super + m
    ltwmc focusmaster

super + shift + m
    ltwmc swapmaster

# floating
super + f
    ltwmc togglefloating

super + shift + f
    ltwmc fullscreen

# workspaces
super + {1-9,0}
    ltwmc workspace {1-9,10}

super + shift + {1-9,0}
    ltwmc movetoworkspace {1-9,10}

# master ratio
super + equal
    ltwmc master_ratio_inc

super + minus
    ltwmc master_ratio_dec

# reload / quit
super + shift + r
    ltwmc reload

super + shift + q
    ltwmc quit
```

---

## Layouts

Cycle with `ltwmc cyclelayout` (or `super + j` in example config above).  
Monocle and Float are not in the cycle — set manually via `ltwmc config layout monocle`.

| Name | Description |
|------|-------------|
| `tile` | Tiled Master — master on left, stack on right |
| `vtile` | Vertical Master — master on top, stack below |
| `vstripes` | Vertical Stripes — all windows in equal columns |
| `hstripes` | Horizontal Stripes — all windows in equal rows |
| `monocle` | One window at a time, focused window fills the area |
| `float` | No automatic tiling |

---

## Changelog

### 0.11.0-alpha — current

**Bug fixes**

- `wal -R` crash — replaced `signal(SIGCHLD, ...)` with `sigaction()` + `SA_RESTART | SA_NOCLDSTOP`; system calls no longer interrupted by `EINTR` during child reaping
- Workspace names flashing `1 2 3` on startup before switching to `I II III` — `bar_create` deferred until after ltwmrc has applied workspace names
- Windows covering the bar — native bar now sets `_NET_WM_WINDOW_TYPE_DOCK` and `_NET_WM_STRUT_PARTIAL`; tiling respects reserved screen area
- Autostart delay (~2 minutes) — removed sequential `usleep(80000)` between autostart spawns; all autostart programs now launched in parallel
- IPC zombie processes — `ipc_poll` now drains the full accept queue each tick; connections rejected when slots are full are handled inline and closed immediately instead of leaking the fd; `IPC_MAX_CLIENTS` raised from 8 to 32
- Border colors not distinguishing active/inactive — `client_update_border` now checks the client's own workspace focused state, not always `cur_ws`; border color updates now propagate across all workspaces

**New features / changes**

- Border config renamed: `border_active` / `border_inactive` (replaces `border_focused` / `border_normal` / `border_floating`)
- `bar_commit` command — create bar in one shot after all config is applied, prevents partial config flash
- `bar_workspaces` IPC command — returns Polybar-formatted workspace string directly
- EWMH desktop atoms — ltwm now sets `_NET_NUMBER_OF_DESKTOPS`, `_NET_CURRENT_DESKTOP`, `_NET_DESKTOP_NAMES`, `_NET_DESKTOP_VIEWPORT`; Polybar `internal/xworkspaces` works natively
- Focus-follows-mouse no longer raises floating windows — hover activates focus, raise requires a click (`client_focus_no_raise` for `EnterNotify`)
- Layout cycle — `cyclelayout` now only cycles the 4 tiling layouts (Tile → VTile → VStripes → HStripes); Monocle and Float are excluded
- Polybar IPC push — ltwm can push workspace updates directly to Polybar's IPC socket via `ltwmc config polybar_pipe <module-name>`

---

## Source layout

```
ltwm/
├── include/
│   └── ltwm.h          # all types, structs, defines, declarations
├── src/
│   ├── wm.c            # init, event loop, EWMH setup
│   ├── events.c        # X event handlers
│   ├── client.c        # window framing, focus, borders, floating
│   ├── workspace.c     # layouts, workspace switching, focus direction
│   ├── bar.c           # native bar rendering (Xft)
│   ├── ipc.c           # Unix socket server, command dispatch, Polybar push
│   ├── spawn.c         # fork/exec helpers, autostart
│   ├── config.c        # defaults, color parsing
│   ├── util.c          # parse_color, die, trim
│   └── ltwmc.c         # client binary — sends commands to ltwm
├── ltwmrc              # example config
├── polybar-example.ini # example Polybar config for use with ltwm
├── Makefile
└── ltwm.desktop        # XSESSIONS entry for display managers
```

---

## License

MIT
