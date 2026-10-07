# How Does The Native Container Terminal Work?

## Decision

The inspector offers one explicit interactive container shell per open session.
Kubernetes exec uses native Qt WebSockets and `v5.channel.k8s.io`.
ResourceClient owns foreground admission, jitter, request limits, credentials,
TLS and authentication suspension. Keystrokes and resize messages reuse the
accepted stream. No kubectl, local shell process, external terminal window or
browser frontend is involved.

libvterm 0.3.3 owns VT/ANSI emulation, application-key modes, Unicode cells and
alternate screens. Its official archive is hash-pinned and statically linked
as portable C99; its MIT notice is bundled and readable in About. Qt Quick
renders the retained screen without animation or polling timers.

## Identity And Lifecycle

Start is an explicit action on a Running core Pod and a declared regular
container. The user chooses an absolute POSIX shell path; `/bin/sh` is the
initial choice, not an automatic fallback. A queued fresh GET checks UID,
name, namespace, container and termination state before exec admission.
Kubernetes exec has no UID precondition: this narrows but cannot atomically
eliminate a recreation race between verification and upgrade.

Session switching and inspector navigation retain the owning stream/screen.
Closing its session, removing its context or destroying the client closes it.
Queued work is removed before sending. Dispatched ordinary Pod reads finish,
but a removed terminal cannot open a stream from their results. Failure stays
readable until explicit disconnect/reconnect. Output/input is not persisted or
included in request audit metadata. No automatic retarget, retry or login.

## Safety And Bounds

Frames/messages and pending writes are limited to 1 MiB; VT size to 120 rows
by 320 columns; scrollback to 5 MiB of cells. Remote OSC clipboard requests,
titles and links cannot mutate local application state. Paste requires
confirmation of a captured clipboard snapshot bound to the original terminal.
Empty or over-64-KiB UTF-8 clipboard text is rejected visibly before confirmation;
it is never truncated into a partial command. Accepted paste uses one bounded
stdin frame, including terminal-negotiated bracketed-paste delimiters, rather
than one WebSocket message per character. The remote application still owns
the semantics of pasted commands.
Commands are user-directed remote actions, not diagnostic quick fixes.

## How Are Small Screens And Keyboard-Only Controls Handled?

The same session-owned stream and retained VT screen serve inline and expanded
views. Narrow inspector panes open the expanded native dialog; desktop users
can expand explicitly. Only the visible surface is loaded. Its toolbar is
scrollable and bounded by the available view height, with optional special
keys for Escape, Tab, interrupt, end of input and arrows. Opening, resizing or
closing an expanded view must not start another exec request or close the shell.

Unmodified Escape and Control shortcuts remain terminal input, including
inside the dialog. Shift+Escape is the documented local focus escape: it moves
to reachable controls without sending remote input. Native Tab navigation then
reaches the return button. GUI Copy without selection preserves the clipboard
and sends nothing to the container. The native input surface owns these key
decisions; no competing global shortcut handles them. Command on macOS and
Super elsewhere remain local GUI modifiers, never characters injected into
the shell. Native buttons activate with Space after keyboard focus; reaching
and leaving the expanded view is verified through those actual key actions.

Only v5 is negotiated. Unsupported servers fail explicitly, without SPDY or
protocol downgrade. Mobile input and assistive-technology behavior need
platform-specific evidence before those devices are claimed supported.

## Verification

`container-terminal-ui-test` drives actual product QML with an explicit external
Kubernetes HTTP/WebSocket test boundary. `native-terminal-e2e` uses real local
K3S and a multi-container Pod for shell output, PTY size, vi file read-back,
Control-C, exit, screenshots and owned-stack cleanup. Executed outcomes and
remaining gaps belong to the existing test map.

## What Happens While Reading Earlier Terminal Output?

Output follows the live tail only while the user is already at the tail.
Scrolling back anchors the displayed history while new lines arrive, including
when the bounded history discards its oldest cells. Growing the viewport keeps
the first retained line visible. VT history-clear removes the old viewport;
Follow explicitly returns to the tail.

The existing libvterm push/pop/clear callbacks report displacement with the
screen update. The surface adjusts its viewport from that displacement rather
than comparing capped history sizes or keeping a second history cache. Native
selection copying stays local, including Shift selection while the remote
application requests mouse tracking.
