#!/bin/bash

[ -z "$X11SPICE_PULSEDIR" ] && exit 1

RAND=$(head -c 256 < /dev/urandom | tr -cd "[:alnum:]" | head -c 16)
FIFO_FILE="$X11SPICE_PULSEDIR/playback/$RAND.fifo"

#DBUS_SESSION_BUS_ADDRESS=`dbus-daemon --session --print-address --fork`
dbus-run-session -- pulseaudio -n -F $X11SPICE_PULSEDIR/pulse.pa -L "module-pipe-sink sink_name=fifo file=$FIFO_FILE format=s16 rate=48000 channels=2" $@
