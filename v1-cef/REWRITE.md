Here’s a clean Qt 6 + QtWebEngineWidgets rewrite “shape” that keeps your **UX + external API (CLI + socket protocol)** basically identical, while leaning into Qt’s strengths (signals/slots, QObject ownership, single GUI thread).

---

## 1) Map your current CEF concepts to QtWebEngine

**CEF message loop / UI thread**

* **Qt:** `QApplication` event loop (everything UI + tab ops happen on the GUI thread)

**CefRequestContext / cache_path**

* **Qt:** one `QWebEngineProfile` per “user-data-dir”, with

  * persistent storage path + cache path set *before* creating any pages ([Qt Documentation][1])
  * note: disk profiles should be destroyed by app exit to flush data ([Qt Documentation][1])

**CefBrowserView per tab**

* **Qt:** one `QWebEngineView` per tab, backed by a `QWebEnginePage` (optionally subclassed for hooks)

**Popup delegate**

* **Qt:** handle `QWebEnginePage::newWindowRequested` and fulfill the request by creating a new page/view/window and calling `request.openIn(page)` ([Qt Documentation][2])

**Load error page**

* **Qt:** connect to `loadingChanged(const QWebEngineLoadingInfo&)` and use `errorCode/errorString/status/url` for your custom HTML ([Qt Documentation][3])

**Context menu hook**

* **Qt:** override `QWebEngineView::contextMenuEvent`, read `lastContextMenuRequest()` (selection/link/media/etc) ([Qt Documentation][4]) and run your `menu_command` with a stdin payload (same as today).

---

## 2) Recommended high-level structure (clean + performant)

### Core idea

Keep **all browser state in a “controller/service” layer** (tabs, bindings, IPC commands), and keep **widgets dumb**.

A good module split:

### `core/`

* **`CommandDispatcher`** (the heart)

  * methods: `ListTabs()`, `Open(url)`, `Switch(id)`, `Cycle(delta)`, `Close(index|active)`, `Bind(...)`, `Unbind(...)`, `TabstripShow/Hide/Toggle/Peek(ms)`
  * returns strings (your existing JSON/errors) so IPC + startup script can share it

* **`TabManager`**

  * owns `std::vector<Tab>` with `{id, QWebEngineView*, url, title, active}`
  * emits signals like `tabsChanged()` for UI (overlay) updates

* **`KeyBindingManager`**

  * stores your bindings (same matching rules, “last wins”)
  * exposes `handleKey(QKeyEvent*) -> optional<bool>` (consume vs pass through)
  * runs shell commands async (prefer `QProcess` / detached worker; avoid blocking GUI)

* **`DebugLog` / `UserDirs`** (can stay almost identical)

### `ui/`

* **`MainWindow : QMainWindow`**

  * contains:

    * `QStackedWidget* tabStack` (active tab only visible)
    * `TabStripOverlay* overlay` (centered popup)
  * handles close → “close all tabs then quit” logic

* **`TabStripOverlay : QWidget`**

  * uses a `QAbstractListModel`-backed `QListView` (or a simple VBox with buttons)
  * show/hide/toggle/peek with a token + `QTimer::singleShot` (mirrors your token approach)

* **`WebView : QWebEngineView`**

  * overrides:

    * `contextMenuEvent(...)` → build payload from `lastContextMenuRequest()` ([Qt Documentation][4])
    * optionally `keyPressEvent(...)` / eventFilter logic (see below)

### `ipc/`

* **`IpcServer`**

  * use **`QLocalServer`** listening on your existing `.../tabs.sock` path (on Unix, `listen()` can take a filesystem path like `/tmp/foo`) ([Qt Documentation][5])
  * call `QLocalServer::removeServer(path)` on startup to recover from stale sockets after crashes ([Qt Documentation][5])
  * parse one line, dispatch to `CommandDispatcher`, write response, close

This removes your current custom thread + poll + wake-pipe complexity entirely (Qt will integrate it into the event loop).

---

## 3) Input/keybinding model that matches your current behavior

Your keybinding requirements:

* intercept **RAW keydown**
* ignore auto-repeat (to avoid stalls)
* decide consume vs pass-through

Qt equivalent:

* install a **global event filter** on `QApplication` (or on the active `WebView`)
* on `QEvent::KeyPress`:

  * if `event->isAutoRepeat()` → return false (don’t intercept repeats)
  * compute a normalized key label (similar rules to your `ExtractKeyLabel`)
  * match modifiers (Alt/Ctrl/Shift/Meta)
  * if match → launch command async; return `true` if consume else `false`

This tends to be more reliable than `QShortcut` for “let it pass through to the page” semantics, because you can precisely decide whether to eat the event.

---

## 4) Tabs + overlay: how to keep it fast

For performance, keep it simple:

* **One `QWebEngineProfile`** shared across tabs (unless you want isolation modes). Profiles share settings/scripts/cookies/cache across pages ([Qt Documentation][1])
* **One `QWebEngineView` per tab**, kept alive; switching tabs is just `tabStack->setCurrentWidget(view)`.
* Update tab title/url via Qt signals (title changes are easy to wire).
* Overlay updates should be “model driven”:

  * `TabManager` emits `tabsChanged()`
  * overlay reads `TabManager::snapshot()` and re-renders (or updates a model)

---

## 5) WebEngine hooks you’ll likely want (equivalents of your CEF client)

### Popups / new windows

Handle `QWebEnginePage::newWindowRequested` and decide:

* open in a new tab in the same window
* or open a new `MainWindow`
  Then call `request.openIn(targetPage)` ([Qt Documentation][2])

### Navigation policy (optional)

If you want to block/redirect certain navigations, use `navigationRequested` / `acceptNavigationRequest` ([Qt Documentation][1])

### Load failures

Connect `loadingChanged(QWebEngineLoadingInfo)` and on `LoadFailedStatus` you can render your own HTML using `errorCode/errorString/url` ([Qt Documentation][3])

### Request interception (only if you really need it)

`QWebEngineUrlRequestInterceptor::interceptRequest()` stalls the request until it returns, and calling into the profile from the main thread can block until the interceptor finishes ([Qt Documentation][6]). So:

* keep it **very** light (no logging to disk, no spawning processes)
* prefer `acceptNavigationRequest` for simpler block/allow decisions when possible ([Qt Documentation][6])

---

## 6) Migration plan (low-risk milestones)

1. **Boot skeleton**

* parse same CLI flags (`--user-data-dir`, `--url`, `--debug-log`, `--auto-exit`, `--startup-script`)
* create `QWebEngineProfile` with storage+cache under your user dir ([Qt Documentation][1])

2. **Single-tab browser**

* `MainWindow` + one `WebView` loading startup URL

3. **TabManager + switching**

* add `QStackedWidget` and tab vector; implement Open/Switch/Cycle/Close

4. **Title tracking + tab snapshots**

* wire `titleChanged` into tab metadata; implement `list/get` output JSON

5. **IPC server parity**

* bring up `QLocalServer` on `tabs.sock` ([Qt Documentation][5])
* implement: `list/switch/cycle/open/close/tabstrip` exactly like today

6. **Key bindings parity**

* event filter + bind/unbind commands + consume/no-consume

7. **Tab strip overlay**

* centered overlay widget, show/hide/toggle/peek token behavior

8. **Context menu command hook**

* override `contextMenuEvent`, read `lastContextMenuRequest()` ([Qt Documentation][4]), run `menu_command`

9. **Popups**

* implement `newWindowRequested` and create tabs/windows ([Qt Documentation][2])

At every step, you can keep your existing `rethread tabs/bind/unbind/tabstrip` CLI pointed at the same socket path, so you’re continuously verifying API compatibility.

---

If you want, I can also sketch a concrete *class-by-class* mapping from your current files (e.g. `TabIpcServer → IpcServer(QLocalServer)`, `BrowserClient → WebPage/WebView hooks`, `TabManager → TabManager + QStackedWidget`, `windowing.cc → MainWindow + popup handler`), but the structure above is the “clean/perf baseline” I’d build on for Qt6-WebEngine.

[1]: https://doc.qt.io/qt-6/qwebengineprofile.html "QWebEngineProfile Class | Qt WebEngine | Qt 6.10.1"
[2]: https://doc.qt.io/qt-6/qwebenginenewwindowrequest.html "QWebEngineNewWindowRequest Class | Qt WebEngine | Qt 6.10.1"
[3]: https://doc.qt.io/qt-6/qwebengineloadinginfo.html "QWebEngineLoadingInfo Class | Qt WebEngine | Qt 6.10.1"
[4]: https://doc.qt.io/qt-6/qwebenginecontextmenurequest.html "QWebEngineContextMenuRequest Class | Qt WebEngine | Qt 6.10.1"
[5]: https://doc.qt.io/qt-6/qlocalserver.html "QLocalServer Class | Qt Network | Qt 6.10.1"
[6]: https://doc.qt.io/qt-6/qwebengineurlrequestinterceptor.html?utm_source=chatgpt.com "QWebEngineUrlRequestIntercep..."
