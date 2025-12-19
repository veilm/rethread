#!/bin/sh
set -eu

config_root="${XDG_CONFIG_HOME:-$HOME/.config}"
config_dir="$config_root/rethread"
mkdir -p "$config_dir"

rm -rf util/__pycache__
# Copy the contents of util/ recursively (including directories like rules/).
cp -a util/. "$config_dir/"
