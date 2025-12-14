# Architecture Snapshot

This repo now ships two closely-related executables. Keyboard shortcuts are
managed in-process; the optional helpers under `tools/` are just legacy examples.

- `rethread-browser`: the heavy Chromium/CEF runtime (built from
  `src/main_linux.cc`, `src/app/app.cc`, `src/browser/*`, etc.). It owns the UI,
  the tab manager, and the Unix socket server (`tabs.sock`) exposed by
  `TabIpcServer`.

- `rethread`: a lightweight CLI wrapper (`src/app/rethread_cli.cc`) that
  provides two entry points:
    * `rethread browser …` → execs `rethread-browser` with the provided flags.
    * `rethread tabs …` → talks to the running browser instance over the Unix
      socket using the helpers from `src/app/tab_cli.{cc,h}` without loading CEF.

  Because `tab_cli` is linked into both binaries, the browser still uses the
  shared helpers (e.g., `TabSocketPath`) while the CLI can run independently.

Key supporting pieces:

- `TabManager` and friends live under `src/browser/` and handle tab lifecycle,
  the overlay, and IPC actions (`switch`, `cycle`, `tabstrip …`).
- `KeyBindingManager` (`src/browser/key_binding_manager.*`) stores the active
  bindings, matches `CefKeyEvent`s inside `BrowserClient::OnPreKeyEvent`, and
  shells out to user commands asynchronously.
- `TabIpcServer` listens on `tabs.sock` inside the browser process and executes
  commands via `RunOnUiAndWait`, which now polls at 1 ms intervals to keep IPC
  latency low.
- `user_dirs.{cc,h}` centralize the default profile path so both binaries agree
  on where to look for `tabs.sock` and the default startup script location.
- Startup scripts (`~/.config/rethread/startup.sh` by default) are read by
  `RethreadApp` after initialization and each line is run through
  `TabIpcServer::ExecuteCommand`, so anything you can do via `rethread tabs …`
  can be scripted at launch.

This layout keeps browser UI work isolated in `rethread-browser` while letting
scripts/tests use the fast `rethread tabs` CLI for IPC. Future agents should
extend functionality by adding commands to `tab_cli`/`TabIpcServer` or by
teaching `KeyBindingManager` new tricks; the built-in binding table is now the
first stop for any keyboard customization work.
