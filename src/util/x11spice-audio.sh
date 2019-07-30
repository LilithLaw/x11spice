#!/bin/bash
#  This script prepares a temporary directory to host audio
#   output for x11spice
#  It's intended usage is as follows:
#   eval `/path/to/this/script setup`
#  done prior to running x11spice.  It will echo 'export X11SPICE_PULSEDIR=/tmp/path`
#  The caller then needs to put that into the x11spice config file.
#
#  Then, in a client session that is expected to make audio content,
#   you need to arrange to have that same environment variable set, and
#   then if you run:
#   eval `$X11SPICE_PULSEDIR/x11spice-audio.sh`
#  That will set environment variables controlling pulse audio such
#  that the audio will be directed into a fifo in $X11SPICE_PULSEDIR/playback/
#  where the x11spice server can pick it up and relay it to the caller.
#  Cleaning up $X11SPICE_PULSEDIR is an exercise left for the reader.

zerodir=`dirname $0`
utildir=`(cd $zerodir; pwd)`

if [ "$1" = "setup" ] ; then
    d=`mktemp -d --tmpdir x11spice.tmpaudio.XXXXXX`
    echo export X11SPICE_PULSEDIR=$d
    mkdir -p $d/playback
    mkdir -p $d/client

    cat $utildir/pulse.conf.in | sed -e "s!_X11SPICE_PULSEDIR_!$d!" > $d/pulse.conf
    cp $utildir/x11spice-pulseaudio.sh $d/
    cp $0 $d/
    cp $utildir/pulse.pa $d/

elif [ "$1" = "client" ] ; then
    if [ ! -d "$X11SPICE_PULSEDIR" ] ; then
        echo 1>&2 "Error:  X11SPICE_PULSEDIR [$X11SPICE_PULSEDIR] is not a valid directory."
        exit 1
    fi
    echo export PULSE_CLIENTCONFIG=$X11SPICE_PULSEDIR/pulse.conf
    echo export PULSE_STATE_PATH=$X11SPICE_PULSEDIR/client/pulsestate
    echo export PULSE_RUNTIME_PATH=$X11SPICE_PULSEDIR/client/pulserun
else
    echo 1>&2 "Error:  Specify 'setup' to setup prior to xorg/x11spice or"
    echo 1>&2 "                'client' (with X11SPICE_PULSEDIR environment variable set)"
    exit 1
fi
