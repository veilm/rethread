# rethread
qutebrowser code is too complex and takes too long to compile. the people need a
solution

## running
```
git clone --depth 1 https://github.com/veilm/rethread
cd rethread

python3 bootstrap.py # pulls a binary for the cef lib from Spotify yes
make -j$(nproc)
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
