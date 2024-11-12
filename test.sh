#!/bin/sh

sudo rmmod hello
make clean
make
sudo insmod hello.ko

exit 0
