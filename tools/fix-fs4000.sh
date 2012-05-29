#!/bin/dash
#
# (Run with sudo) to fix scanner dev permissions
#
USBDEV=`sane-find-scanner |sed -n -e '/0x04a9/ s/.*at \(libusb.*\)/\1/p'`
if [ -n $USBDEV ] ; then
  echo $USBDEV | { IFS=: read dummy bus dev ; echo bus=$bus dev=$dev ; set -x ; setfacl -m g:scanner:rw /dev/bus/usb/$bus/$dev ; }
fi
