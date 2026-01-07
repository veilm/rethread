#!/bin/sh
set -eu

config_root="${XDG_CONFIG_HOME:-$HOME/.config}"
config_dir="$config_root/rethread"
mkdir -p "$config_dir"

rm -rf util/__pycache__
# Copy the contents of util/ recursively (including directories like rules/).
# Don't overwrite existing user files in the config directory.
cp -a -n util/. "$config_dir/"
