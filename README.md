# rethread
qutebrowser code is too complex and takes too long to compile. the people need a
solution

## running
```
git clone --depth 1 https://github.com/veilm/rethread
cd rethread

python3 bootstrap.py # pulls a binary for the cef lib from Spotify yes

# sandboxing setup, hopefully works
sudo chown root:root $(find . -type f -name chrome-sandbox)
sudo chmod 4755 $(find . -type f -name chrome-sandbox)

make -j$(nproc)

# launch the browser UI (for direct access you can also run rethread-browser)
./out/Release/rethread browser --help
```

The `rethread` binary is a light CLI wrapper. Use `rethread browser ...` to
launch the UI and `rethread tabs ...` to talk to a running instance without
reloading CEF each time. After startup, the browser automatically runs
`$XDG_CONFIG_HOME/rethread/startup.sh` (override with `--startup-script=PATH`)
so you can pre-register keybindings or tweak state declaratively.

## key bindings

Bindings now live inside the browser—no external handler required. Register one
with the CLI and it stays in memory until you quit:

```
# Alt+J / Alt+K cycle tabs without touching Python
rethread bind --alt --key=j -- rethread tabs cycle 1
rethread bind --alt --key=k -- rethread tabs cycle -1

rethread bind --ctrl --key=t --no-consume -- "notify-send 'new tab'"
```

Each binding accepts modifier flags (`--alt`, `--ctrl`, `--shift`,
`--command`/`--meta`), a `--key=<value>`, optional `--no-consume`, and the shell
command to run after `--`. Commands execute via `/bin/sh -c ...`, so any shell
snippet works. Drop the same lines into
`$XDG_CONFIG_HOME/rethread/startup.sh` to have them applied automatically on launch.

## tab strip overlay

The tab strip overlay starts hidden. Use the CLI to control it at runtime:

```
# show / hide / toggle visibility
rethread tabs tabstrip show
rethread tabs tabstrip hide
rethread tabs tabstrip toggle

# briefly show it for 400ms, then auto-hide
rethread tabs tabstrip peek 400

# switch to next/previous tab, then peek for 750ms
rethread tabs cycle 1 && rethread tabs tabstrip peek 750
rethread tabs cycle -1 && rethread tabs tabstrip peek 750
```

`peek` always shows the overlay immediately and schedules a hide after the given
duration (milliseconds). Any manual show/hide/toggle commands cancel pending
peek hides.
