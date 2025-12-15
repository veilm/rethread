#!/bin/sh
set -eu

config_root="${XDG_CONFIG_HOME:-$HOME/.config}"
config_dir="$config_root/rethread"
mkdir -p "$config_dir"

cp util/startup.sh "$config_dir/startup.sh"
cp util/right-click-handler.py "$config_dir/right-click-handler.py"
