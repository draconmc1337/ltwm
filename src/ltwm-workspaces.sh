#!/bin/sh
LTWMC=/usr/local/bin/ltwmc
DNUM=$(echo "${DISPLAY:-:0}" | tr -dc '0-9')
EVT="/tmp/ltwm-${DNUM}.events"

render() {
    $LTWMC getstatus 2>/dev/null | \
    python3 -c "
import sys, json, re
try:
    d = json.loads(sys.stdin.read())
    cur = d['current_workspace']
    out = ''
    for ws in d['workspaces']:
        n = ws['name']
        if ws['active']:
            out += '%%{F#C0A251}%%{B#313244} '+n+' %%{B-}%%{F-}'
        elif ws['clients'] > 0:
            out += '%%{F#c8c7c5}%%{B#1e1e2e} '+n+' %%{B-}%%{F-}'
    print(out)
except: pass
" 2>/dev/null
}

render

# event loop — socat hoặc nc -U
while true; do
    if command -v socat >/dev/null 2>&1; then
        socat - UNIX-CONNECT:"$EVT" 2>/dev/null
    else
        nc -U "$EVT" 2>/dev/null
    fi
    render
    sleep 0.02
done
