#!/bin/sh
peek="rethread tabstrip peek 750"

# export command, to target this same profile
E="export RETHREAD_USER_DATA_DIR=$RETHREAD_USER_DATA_DIR ;"

config_root="${XDG_CONFIG_HOME:-$HOME/.config}"
right_click_handler="$config_root/rethread/right-click-handler.sh"
if [ -x "$right_click_handler" ]; then
  # Route right-click events through the local handler script.
  rethread bind --context-menu "$E \"$right_click_handler\""
fi

rethread bind --ctrl --key t "$E rethread tabs open 'https://veilm.github.io/rethread/' ; $peek"
rethread bind --alt --shift --key o "$E rethread tabs open 'https://veilm.github.io/rethread/' ; $peek"

rethread bind --ctrl --key w "$E rethread tabs close ; $peek"
rethread bind --alt --key d "$E rethread tabs close ; $peek"

rethread bind --ctrl --key tab "$E rethread tabs cycle 1 ; $peek"
rethread bind --alt --key j "$E rethread tabs cycle 1 ; $peek"
rethread bind --ctrl --shift --key tab "$E rethread tabs cycle -1 ; $peek"
rethread bind --alt --key k "$E rethread tabs cycle -1 ; $peek"

rethread bind --alt --key left "$E rethread tabs history-back ; $peek"
rethread bind --alt --key h "$E rethread tabs history-back ; $peek"
rethread bind --alt --key right "$E rethread tabs history-forward ; $peek"
rethread bind --alt --key l "$E rethread tabs history-forward ; $peek"

# navigate to url from wl-paste
rethread bind --alt --key p "$E echo 'window.location.href = \"$(wl-paste)\"' | rethread eval --stdin ; $peek"
rethread bind --alt --shift --key p "$E rethread tabs open \"\$(wl-paste)\" ; $peek"
