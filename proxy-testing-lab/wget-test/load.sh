#!/bin/bash

set -v;

HOSTNAME=fit.ippolitov.me
URL=http://$HOSTNAME/gallery
WGET_FLAGS="-q --show-progress --mirror --adjust-extension --page-requisites --no-parent --no-if-modified-since"

rm -rf noproxy_$HOSTNAME proxy_$HOSTNAME

export http_proxy=http://localhost:7777
wget $WGET_FLAGS $URL
mv $HOSTNAME proxy_$HOSTNAME

unset http_proxy
wget $WGET_FLAGS $URL
mv $HOSTNAME noproxy_$HOSTNAME

#tar -czvf noproxy_$HOSTNAME.tar.gz noproxy_$HOSTNAME
#tar -czvf proxy_$HOSTNAME.tar.gz   proxy_$HOSTNAME
