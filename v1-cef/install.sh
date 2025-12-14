#!/bin/sh
conf=$XDG_CONFIG_HOME/rethread
mkdir -p "$conf"
cp ./util/startup.sh "$conf"
