#!/bin/sh
# Polybar workspace module for ltwm — event-driven, zero lag
LTWMC=/usr/local/bin/ltwmc
DNUM=$(echo "${DISPLAY:-:0}" | tr -dc '0-9')
EVT="/tmp/ltwm-${DNUM}.events"

render() {
    out=$($LTWMC getstatus 2>/dev/null) || return
    result=""
    # parse với awk thay vì python3 (nhanh hơn, không cần python)
    result=$(echo "$out" | awk '
    BEGIN { RS=","; FS=":" }
    /\"id\"/ { id=0; name=""; clients=0; active="false" }
    /\"id\"/ { gsub(/[^0-9]/,"",$2); id=$2 }
    /\"name\"/ { gsub(/[\"{}]/,"",$2); name=$2 }
    /\"clients\"/ { gsub(/[^0-9]/,"",$2); clients=$2 }
    /\"active\"/ { gsub(/[\"} ]/,"",$2); active=$2 }
    /\"active\"/ {
        if (name == "") name = id
        if (active == "true")
            printf "%%{F#C0A251}%%{B#313244} " name " %%{B-}%%{F-}"
        else if (clients+0 > 0)
            printf "%%{F#c8c7c5}%%{B#1e1e2e} " name " %%{B-}%%{F-}"
    }
    ')
    echo "$result"
}

render

while true; do
    if command -v socat >/dev/null 2>&1; then
        socat - UNIX-CONNECT:"$EVT" 2>/dev/null && render
    else
        nc -U "$EVT" 2>/dev/null && render
    fi
    sleep 0.02
done
