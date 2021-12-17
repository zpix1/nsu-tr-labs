#!/bin/bash

HOSTNAME=fit.ippolitov.me

set -ve;

A=proxy_$HOSTNAME
B=noproxy_$HOSTNAME

rm a b;

cd $A;
find . | xargs openssl sha256 | sort | tee -a ../a;
cd ..;

cd $B;
find . | xargs openssl sha256 | sort | tee -a ../b;
cd ..;

diff a b;