#!/bin/sh

sudo rmmod fs_monitor
make clean
make
sudo insmod fs_monitor.ko
make clean

exit 0
