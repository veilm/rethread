#!/bin/sh
config_root="${XDG_CONFIG_HOME:-$HOME/.config}"
config_dir="$config_root/rethread"
mkdir -p "$config_dir"

curl "https://raw.githubusercontent.com/StevenBlack/hosts/master/hosts" -o "$config_dir/hosts-blacklist.txt"
