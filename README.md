# rethread
qutebrowser code is too complex and takes too long to compile. the people need a
solution

## running
You need Qt 6 with WebEngine (on Arch: `sudo pacman -S qt6-base qt6-webengine`).

```
git clone --depth 1 https://github.com/veilm/rethread
cd rethread/v1-cef

# configure + build (Makefile wraps CMake)
make browser
make cli

# launch the browser UI (or run ./build/rethread-browser directly)
./build/rethread browser --help
```

The `rethread` binary is a light CLI wrapper. Use `rethread browser ...` to
launch the UI and `rethread tabs ...` to talk to a running instance without
reloading the Qt stack each time. After startup, the browser automatically runs
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
Use `rethread unbind [mods] --key=...` to clear a binding and fall back to the
browser's default behavior for that key combo.

## tab strip overlay

The tab strip overlay starts hidden. Use the CLI to control it at runtime:

```
# show / hide / toggle visibility
rethread tabstrip show
rethread tabstrip hide
rethread tabstrip toggle

# briefly show it for 400ms, then auto-hide
rethread tabstrip peek 400

# switch to next/previous tab, then peek for 750ms
rethread tabs cycle 1 && rethread tabstrip peek 750
rethread tabs cycle -1 && rethread tabstrip peek 750
```

`peek` always shows the overlay immediately and schedules a hide after the given
duration (milliseconds). Any manual show/hide/toggle commands cancel pending
peek hides.

## evaluating JavaScript

Use `rethread eval` to run JavaScript inside a tab without leaving the terminal.
By default the active tab receives the snippet:

```
rethread eval "window.location.href"
```

Read scripts from stdin with `--stdin` (handy for multi-line snippets) and
target specific tabs with either an id or 1-based index:

```
cat snippet.js | rethread eval --stdin --tab-index=2
rethread eval --tab-id=7 "({title: document.title, url: location.href})"
```

The command prints the JSON-encoded return value (strings stay quoted, objects
and arrays render as expected). Errors bubble up as `ERR ...` lines.
