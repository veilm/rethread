#!/bin/sh
peek="rethread tabstrip peek 750"

# export command, to target this same profile
E="export RETHREAD_USER_DATA_DIR=$RETHREAD_USER_DATA_DIR ;"

config="${XDG_CONFIG_HOME:-$HOME/.config}/rethread"
command_menu="$config/rethread-command-menu.py"
right_click_handler="$config/right-click-handler.py"
cosmetic_filter="$config/cosmetic-filters.py"
rethread bind --context-menu "$E $right_click_handler"

if [ -x "$cosmetic_filter" ]; then
  "$cosmetic_filter" sync
  rethread bind --alt --key x "$E \"$cosmetic_filter\""
fi

rethread bind --ctrl --key t "$E rethread tabs open 'https://veilm.github.io/rethread/' ; $peek"
rethread bind --alt --shift --key o "$E rethread tabs open 'https://veilm.github.io/rethread/' ; $peek"

rethread bind --ctrl --key w "$E rethread tabs close ; $peek"
rethread bind --alt --key d "$E rethread tabs close ; $peek"

rethread bind --ctrl --key tab "$E rethread tabs cycle 1 ; $peek"
rethread bind --alt --key j "$E rethread tabs cycle 1 ; $peek"
rethread bind --ctrl --shift --key tab "$E rethread tabs cycle -1 ; $peek"
rethread bind --alt --key k "$E rethread tabs cycle -1 ; $peek"

rethread bind --alt --shift --key k "$E rethread tabs swap -1 ; $peek"
rethread bind --alt --shift --key j "$E rethread tabs swap +1 ; $peek"

rethread bind --alt --key left "$E rethread tabs history-back ; $peek"
rethread bind --alt --key h "$E rethread tabs history-back ; $peek"
rethread bind --alt --key right "$E rethread tabs history-forward ; $peek"
rethread bind --alt --key l "$E rethread tabs history-forward ; $peek"

rethread bind --ctrl --key r "$E echo 'window.location.reload()' | rethread eval --stdin > /dev/null"
rethread bind --alt --key r "$E echo 'window.location.reload()' | rethread eval --stdin > /dev/null"

rethread bind --ctrl --shift --key i "$E rethread devtools open"
rethread bind --alt --shift --key i "$E rethread devtools open"

# navigate to url from wl-paste, copy url using wl-copy
rethread bind --alt --key p "$E echo \"window.location.href = '\$(wl-paste)'\" | rethread eval --stdin > /dev/null ; $peek"
rethread bind --alt --shift --key p "$E rethread tabs open \"\$(wl-paste)\" ; $peek"
rethread bind --alt --key y "$E wl-copy \$(rethread tabs list | jq -r '.tabs[] | select(.active == true) | .url') ; rethread tabstrip message --duration=500 'Copied URL'"
rethread bind --alt --key ';' "$E \"$command_menu\""

echo "archived.moe" | rethread rules js --blacklist

# default google etc + any bonus user-defined
cat $config/iframes-whitelist*.txt | rethread rules iframes --whitelist
