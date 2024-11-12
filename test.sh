#!/bin/sh

sudo rmmod hello
make clean
make
sudo insmod hello.ko
make clean

exit 0
