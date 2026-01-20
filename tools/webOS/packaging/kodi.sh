#!/bin/sh

STRACE_BIN="/media/developer/apps/usr/palm/applications/org.xbmc.kodi/strace"
STRACE_OUT="/media/developer/temp/strace_output.txt"
KODI_BIN="/media/developer/apps/usr/palm/applications/org.xbmc.kodi/kodi-webos"
KODI_LOG="/media/developer/temp/output.log"

mkdir -p /media/developer/temp

# Dump environment
env | grep -E 'LD_LIBRARY_PATH|APPID' >> "$KODI_LOG"

# Append strace + Kodi output
"$STRACE_BIN" -s 2000 -v -o "$STRACE_OUT" "$KODI_BIN" --debug >> "$KODI_LOG" 2>&1
