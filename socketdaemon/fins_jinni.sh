#!/bin/bash
clear
cd /tmp/fins/
rm mainsocket_channel
mkfifo mainsocket_channel
mkfifo rtm_in
mkfifo rtm_out
cd /dev/shm/
rm sem*.*
cd ~/workspace2/socketdaemon/Debug
./socketdaemon
