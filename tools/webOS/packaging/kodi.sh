#!/bin/bash

# Path to your preload library
PRELOAD_LIB="/media/developer/apps/usr/palm/applications/org.xbmc.kodi/libgst_intercept.so"

# Path to Kodi executable
KODI_EXEC="/media/developer/apps/usr/palm/applications/org.xbmc.kodi/kodi-webos"

# Launch Kodi with LD_PRELOAD
echo "Launching Kodi with LD_PRELOAD=$PRELOAD_LIB"
LD_PRELOAD="$PRELOAD_LIB" "$KODI_EXEC"
