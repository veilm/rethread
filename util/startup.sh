#!/bin/sh
peek_cmd="rethread tabstrip peek 750"

rethread bind --alt --shift --key o "rethread tabs open 'https://veilm.github.io/rethread/' ; $peek_cmd"
rethread bind --ctrl --key t "rethread tabs open 'https://veilm.github.io/rethread/' ; $peek_cmd"

# TODO tab? how
rethread bind --alt --key j "rethread tabs cycle 1 ; $peek_cmd"
rethread bind --alt --key k "rethread tabs cycle -1 ; $peek_cmd"
