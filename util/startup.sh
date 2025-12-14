#!/bin/sh
peek="rethread tabstrip peek 750"

# export command, to target this same profile
E="export RETHREAD_USER_DATA_DIR=$RETHREAD_USER_DATA_DIR ;"

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
