# Changelog

All notable changes to LTWM will be documented here.

---

## [0.11.0-alpha] — 2026-03-29

### Fixed

- **Border active/inactive not distinguishing correctly** — `client_update_border` was checking `wm->workspaces[cur_ws].focused` instead of the client's own workspace; all windows appeared inactive when `ws_tile` was called on a non-current workspace. Now checks `wm->workspaces[c->workspace].focused`.
- **Border color changes only applied to current workspace** — IPC handlers for `border_active` and `border_inactive` now propagate across all workspaces, not just `cur_ws`.
- **IPC zombie processes** — `ipc_poll` previously accepted only one connection per tick; when ltwmrc sent a burst of commands on startup, the accept queue overflowed, leaving `ltwmc` processes hanging indefinitely. Now drains the full accept queue each tick. Connections that exceed `IPC_MAX_CLIENTS` are handled inline and closed immediately. `IPC_MAX_CLIENTS` raised from 8 to 32.
- **Bar not appearing until a window was opened** — `bar_enable` and `bar_font` were racing; bar was created before font was configured, causing silent Xft failure. Introduced `bar_commit` to defer bar creation until all config is applied.
- **Xft text width returning zero** — `xft_tw()` was passing `NULL` as the Display argument to `XftTextExtentsUtf8`, causing font measurement to silently fail and fall back to `strlen * 8`. All Xft measurement calls now use the correct display handle.
- **`wal -R` crashing intermittently** — replaced `signal(SIGCHLD, ...)` with `sigaction()` using `SA_RESTART | SA_NOCLDSTOP`; system calls (read, select, write) no longer return `EINTR` during child reaping, preventing subprocess crashes.
- **Windows covering the bar on startup** — native bar now sets `_NET_WM_WINDOW_TYPE_DOCK` and `_NET_WM_STRUT_PARTIAL` after `XMapRaised`; `update_struts` is called immediately so tiling respects the reserved area from the first frame.
- **Workspace names showing `1 2 3` on startup** — `bar_create` was called in `wm_init` before ltwmrc had a chance to apply workspace names. Bar creation now deferred to after ltwmrc runs.
- **Autostart taking up to 2 minutes** — `spawn_autostart` had `usleep(80000)` between each program. Removed; all autostart programs now launch in parallel.
- **Focus-follows-mouse raising floating windows** — hovering over a window behind a floating window caused the background window to jump to the front. `EnterNotify` now calls `client_focus_no_raise`; raise only happens on click.

### Added

- **`bar_commit` IPC command** — explicitly triggers bar creation after all bar config has been set in ltwmrc. Prevents partial config flash (wrong font or color appearing briefly on startup).
- **`bar_workspaces` IPC command** — returns a Polybar-formatted workspace string directly from ltwm, using configured active/occupied colors. Useful for `custom/ipc` module if preferred over `internal/xworkspaces`.
- **EWMH desktop atoms** — ltwm now advertises and maintains `_NET_NUMBER_OF_DESKTOPS`, `_NET_CURRENT_DESKTOP`, `_NET_DESKTOP_NAMES`, and `_NET_DESKTOP_VIEWPORT` on the root window. Updated on every workspace switch and workspace name change. Enables Polybar `internal/xworkspaces` with zero lag and no scripts.
- **Polybar IPC push** — `ltwmc config polybar_pipe <module-name>` causes ltwm to write `action:#<module>.hook.0` directly to Polybar's IPC socket (`/tmp/polybar_mqueue.*`) on every workspace or client change. Alternative to `internal/xworkspaces` for custom hook-based setups.
- **`client_focus_no_raise`** — new internal function for focus-without-raise, used by `EnterNotify`. `client_focus` (used by click, workspace switch, new window) still raises floating windows as before.

### Changed

- **Border config renamed** — `border_focused` → `border_active`, `border_normal` → `border_inactive`. `border_floating` removed; floating and tiled windows use the same inactive color. Update your ltwmrc:
  ```sh
  # old
  ltwmc config border_focused  '#89B4FA'
  ltwmc config border_normal   '#45475A'
  ltwmc config border_floating '#A6E3A1'

  # new
  ltwmc config border_active   '#89B4FA'
  ltwmc config border_inactive '#45475A'
  ```
- **Layout cycle** — `cyclelayout` now only cycles the 4 tiling layouts: Tile → Vertical Tile → Vertical Stripes → Horizontal Stripes. Monocle and Float are excluded from the cycle. Set them explicitly via `ltwmc config layout monocle` if needed.
- **Default bar font** — changed from a legacy X11 bitmap font string to `JetBrains Mono:size=12` (Xft format). Falls back through `monospace:size=12`, `DejaVu Sans Mono:size=12`, `Liberation Mono:size=12` if the primary font is not installed.

---

## [0.10.0-alpha] — initial release

- Initial public release
- Tiled Master, Vertical Master, Vertical Stripes, Horizontal Stripes, Monocle, Float layouts
- 10 workspaces with custom names
- Built-in Xft bar with pywal support
- Unix socket IPC (`ltwmc`)
- Focus-follows-mouse
- sxhkd-friendly — no built-in keybindings
- SDDM / display manager support via `.desktop` session entry
