#!/bin/bash
#  Example script that shows how to launch the spice video driver
#   and attach x11spice to it.
#  This is not recommended for production use without some study
#   and understanding of what is happening.  At the very least,
#   any user should explore using ssl to encrypt the x11spice traffic.

# Note on XDMCP
#   To use XDMCP on fairly modern systems, you need to make 2 changes.
# First, you need to edit /etc/gdm/custom.conf and under the
#  [xdmcp]
# section, add:
#  Enabled=true
# Next, you more than likely want to avoid policy kit complaining
#  about color devices.  A hint on how to this can be found here:
#    http://c-nergy.be/blog/?p=12043
# Also note that if you are running inside a virtual machine,
#  you will most likely not get 'real' hardware acceleration,
#  and a result, a gnome session will generally fail with a 'Mr. Yuk'
#  screen.

usage()
{
    cat << EOF
Usage:
  spicedummy.sh [--display :n] [--query hostname] [--agent] [x11spice-arguments]
If no x11spice arguments are provided, a random password will be
  generated and passed in along with --allow-control.
If no display is given, :2 will be used.
If --query is provided, Xorg will be started with a request to query the
  given host via XDMCP.  Otherwise, xinit will be invoked.
Note that XDMCP usually requires some further configuration.
If --agent is provided, the spice-vdagentd and spice-vdagent will be started.
EOF
    exit 1
}

# Parse arguments
use_agent=0
export DISPLAY=:2
while [ $# -gt 0 ] ; do
    case "$1" in
    --display)
        shift;
        if [ $# -lt 1 ] ; then
            echo Error: no display given
            usage
        fi
        export DISPLAY="$1"
        shift;
        ;;
    --query)
        shift;
        if [ $# -lt 1 ] ; then
            echo Error: no hostname given
            usage
        fi
        xdmcp="$1"
        shift;
        ;;
    --agent)
        use_agent=1
        shift;
        ;;
    --help)
        usage;
        ;;
    *)  args="$args $1"
        shift;
        ;;
    esac
done

if [ -z "$args" ] ; then
    # Generate a random password
    password=`hexdump -n2 -e '2/1 "%02x"' /dev/urandom`
    args="--password=$password --allow-control"
fi

zerodir=`dirname $0`
dummydir=`(cd "$zerodir"; pwd)`
if [ -x /usr/libexec/Xorg ] ; then
    xorg=/usr/libexec/Xorg
elif [ -x /usr/lib/xorg/Xorg ]; then
    xorg=/usr/lib/xorg/Xorg
else
    xorg=`which Xorg`
fi
xmodules=`"$xorg" -showDefaultModulePath 2>&1`
x11spice="$dummydir/../src/x11spice"
if [ ! -x "$x11spice" ] ; then
    x11spice=`which x11spice`
fi

if [ ! -x "$x11spice" ] ; then
    echo "Error: x11spice not found in the path."
    echo "Cowardly refusing to start"
    exit 1
fi

if [ $use_agent -gt 0 ] ; then
    vdagent=`which spice-vdagent`
    if [ -z "$vdagent" ] ; then
        echo "Error: spice-vdagent not found in path."
        exit 1
    fi

    vdagentd=`which spice-vdagentd 2>/dev/null`
    if [ -z "$vdagentd" ] ; then
        vdagentd=/usr/sbin/spice-vdagentd
    fi
    if [ ! -x "$vdagentd" ] ; then
        echo Error: spice-vdagentd not found in path or /usr/sbin.
        exit 1
    fi
    vdadir=`mktemp -d`
    args="$args --virtio-path=$vdadir/virtio --uinput-path=$vdadir/uinput"
fi

# Make sure xinit is likely to work
if [ -z "$xdmcp" -a ! -f ~/.xinitrc ] ; then
    type xterm >/dev/null 2>&1
    if [ $? -ne 0 ] ; then
        echo "Error: You do not have either ~/.xinitrc or xterm available."
        echo "Cowardly refusing to start."
        exit 1
    fi
fi

# Build up an Xorg command line
xcmd="\"$xorg\" $DISPLAY -novtswitch vt00"
if [ "$zerodir" = "/usr/bin" ] ; then
    xcmd+=" -config spicedummy.conf"
else
    xcmd+=" -modulepath \"$dummydir/src/.libs,$xmodules\""
    xcmd+=" -config \"$dummydir/X11/spicedummy.conf\""
fi

# Start Xorg
if [ -n "$xdmcp" ] ; then
    export XAUTHORITY=`mktemp`
    chmod og-rwx $XAUTHORITY
    xauth -f $XAUTHORITY add $DISPLAY . $(hexdump -n16 -e '16/1 "%02x"' /dev/urandom)
    eval $xcmd -auth $XAUTHORITY -query "$xdmcp" &
else
    eval xinit -- $xcmd &
fi

# The child of eval will be our Xinit or Xorg session.
#  eval was used to enable spaces in directory paths to work as expected.
xpid=`ps -o pid= --ppid $!`

# Wait for Xorg to be usable
let t=0
while [ $t -lt 300 ] ; do
    sleep 0.1
    t=$((t+1))
    xdpyinfo >/dev/null 2>&1
    if [ $? -eq 0 ] ; then
        break
    fi

    ps $xpid >/dev/null 2>&1
    if [ $? -ne 0 ] ; then
        echo "X server exited before we could connect"
        exit 1
    fi
done

if [ $t -ge 300 ] ; then
    echo "We did not successfully start the X server"
    kill $xpid
    exit 1
fi

# We have a working X server, now let's start x11spice
"$x11spice" $args &
spicepid=$!

echo Xorg server started as pid $xpid
echo Spice server started as pid $spicepid

if [ $use_agent -gt 0 ] ; then
    sleep 0.5
    $vdagentd -d -d -x -X -o -f -u $vdadir/uinput -s $vdadir/virtio -S $vdadir/vda &
    agentdpid=$!
    $vdagent -x -d -d -s $vdadir/virtio -S $vdadir/vda &
    agentpid=$!
    if [ $? -ne 0 ] ; then
        echo "Error: spice-vdagent did not start up.  Expect agent problems."
        kill $agentdpid
    else
        echo "spice-vdagentd started as $agentdpid; spice-vdagent as $agentpid"
    fi
fi



if [ -n "$password" ] ; then
    echo You should be able to connect with a spice client now to port 5900,
    echo  using a password of $password
fi

wait $spicepid

if [ -n "$xdmcp" ] ; then
    rm -f $XAUTHORITY
fi

kill $xpid

if [ $use_agent -gt 0 ] ; then
    kill $agentdpid $agentpid
    sleep 0.1
    rm -rf $vdadir
fi
