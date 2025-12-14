# 1765685209 rethread vs rethread-browser
because it's linked against CEF, the original rethread took a while to startup,
like 30 ms just for --help IIRC. that made it so any kind of CLI usage like
"rethread tabs list" was problematic especially if you chained a few

we split into two programs: one main bloated browser runtime (rethread-browser)
that is like a server listening for IPC messages, and executing them. then
another light CLI program (rethread) that parses CLI args and makes IPC requests
to the active rethread-browser process. this improves performance

# 1765686783 separate key handler
we currently have a system where there's a separate script/program
`rethread-key-handler`. whenever rethread detects any key it constructs some CLI
flags and calls the handler. then the handler does whatever it wants, like
seeing ctrl+t and making a new tab

one key other component, is that rethread looks at the handler's exit code, to
determine whether the handler wants rethread to consume the key, or to also pass
the key through to the page. that's not really a fully make-or-break feature but
it's nice to have

the problem then is that if the handler is doing something expensive like thta
would take 100 ms, it would wait that 100 ms before exiting, so the browser
would be forced to wait 100 ms before deciding whether to copy the key to the
page, or to suppress it

so what codex was doing was making Python rerun itself in the background, until
it catches up to the same place in the code, then do the actual task execution
in that background process. but the parent handler process that was originally
called by rethread, would exit immediately after that

but that made the code really jank, and would incur some kind of startup cost?
I'm not fully sure what codex was doing, it said it would be 2x the python
startup but theoretically if that's running in the background then it's not
blocking. oh well not blocking in terms of when rethread knows whether to
suppress the key, but still blocking in terms of waiting until the actual task
runs, yeah

so Python in general in that setup took a while to startup and that took like 50
ms overall or something, plus the time it takes to do the rethread commands.
right now still a tab cycle forward is like 20 ms, although getting the tabs is
fast like 2 ms. you could probably ease this problem by running your task
commands as a background job directly

the other issue is that with Python it's not even particularly clean, you'd have
to be doing subprocesses and stuff and nobody wants to write that. C is not too
much worse at that point. a shell case statement is better but here you're
matching across multiple things like ctrl, and at least codex's initial idea
/home/oboro/src/rethread/tools/rethread_key_handler.sh is horrible to read.
maybe you could normalie it as a case statement with mapping everything to one
string like C-A-f or something

overall it's close, that last solution + using background jobs is not that bad.
I'm very tired and can barely thing but I'm leaning towards the next thing:

# 1765687019 mirror hyprland's binding model
instead of having a script that receives the bindings, you use hyprctl to
creating them or rebind them at runtime.

e.g. some kind of "rethread bind --ctrl e 'alert hi'"

then rethread has some kind of shell script that always runs on startup, where
you put all of your config in. like qutebrowser's config.py I think. wait now we
just are mirroring qutebrowser more directly. but whatever this is probably
better

we already needed a startup script for our other settings, like the tabstrip
hide/show. so it'd be weird not to include the bindings there. also with this
model, the passthrough vs suppress for bindings is a minor detail of whether to
pass like a --pass-through to the bind command

it also means for a lot of simpleactions you can just put in 'alert hi ; msk_log
test' as the command and it will work, you don't need heavier python syntax. and
it should be very performant, the lookups are all in memory

hmm but what about our handler for right click on links and stuff? that
literally always warrnts a handler you're going to want complex flow based on
all the combinations of options

still the performance is a step size higher for this and it's more important
than right click, rihgt click you do like once per 10 minutes this is once per
10 seconds. let's keep it for now
