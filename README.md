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
sudo cp tools/rethread_key_handler.py /usr/local/bin/rethread-key-handler

./out/Release/rethread --help
```

## key handler

Rethread looks for an executable named `rethread-key-handler` on `PATH`. Every
non-repeat `KEYEVENT_RAWKEYDOWN` event launches that handler with CLI flags
describing the key, for example:

```
rethread-key-handler key \
  --type=rawkeydown \
  --windows-key-code=69 \
  --native-key-code=38 \
  --modifiers=2 \
  --character=101 \
  --unmodified-character=101 \
  --key-label=e \
  --ctrl
```

The handler should exit with status `2` if it consumed the shortcut (preventing
the default browser action). Exit codes `0` or `1` tell the browser to pass the
event through unchanged.

An example handler script lives at `tools/rethread_key_handler.py`. It normalizes
Ctrl combinations (ASCII control codes 1-26) back to printable labels so
`Ctrl+E` still shows up as `e`, and returns exit code `2` only for shortcuts it
wants to consume. Symlink or
copy it somewhere on your `PATH` and customize it to run whichever commands you
need.

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
