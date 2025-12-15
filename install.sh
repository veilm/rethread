#!/bin/sh
set -eu

config_root="${XDG_CONFIG_HOME:-$HOME/.config}"
config_dir="$config_root/rethread"
mkdir -p "$config_dir"

cp util/* "$config_dir"
