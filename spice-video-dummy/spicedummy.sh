#!/bin/bash
#  Example script that shows how to launch the spice video driver
#   and attach x11spice to it.  This is not meant for production
#   purposes.

export DISPLAY=:2

password=`hexdump -n2 -e '2/1 "%02x"' /dev/urandom`
zerodir=`dirname $0`
dummydir=`(cd $zerodir; pwd)`
xmodules=`Xorg -showDefaultModulePath 2>&1`

# Start Xorg
xinit -- `which Xorg` $DISPLAY \
    -modulepath "$dummydir/src/.libs,$xmodules" \
    -config "$dummydir/spicedummy.conf" &
xpid=$!
sleep 1

$dummydir/../src/x11spice --password=$password --allow-control &
spicepid=$!

echo Xorg server started as pid $xpid
echo Spice server started as pid $spicepid
echo You should be able to connect with a spice client now to port 5900,
echo  using a password of $password

wait $spicepid

kill $xpid
