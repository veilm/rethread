#!/bin/sh
peek_cmd="rethread tabs tabstrip peek 750"

rethread tabs bind --alt --shift --key o "rethread tabs open 'https://veilm.github.io/rethread/' ; $peek_cmd"
rethread tabs bind --ctrl --key t "rethread tabs open 'https://veilm.github.io/rethread/' ; $peek_cmd"

# TODO tab? how
rethread tabs bind --alt --key j "rethread tabs cycle 1 ; $peek_cmd"
rethread tabs bind --alt --key k "rethread tabs cycle -1 ; $peek_cmd"
