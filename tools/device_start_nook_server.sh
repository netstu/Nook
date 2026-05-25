#!/system/bin/sh
RUNTIME_DIR=/data/local/tmp/nook
SERVER="$RUNTIME_DIR/nook-server"
OUT="$RUNTIME_DIR/server.out"
ERR="$RUNTIME_DIR/server.err"

pkill -9 -f nook-server 2>/dev/null
rm -f "$OUT" "$ERR"
: > "$OUT"
: > "$ERR"
chmod 755 "$SERVER"
nohup /system/bin/linker64 "$SERVER" --enable-zygote-control >"$OUT" 2>"$ERR" </dev/null &
