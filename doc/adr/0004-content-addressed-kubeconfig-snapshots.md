# 0004 Content-addressed kubeconfig snapshots

## Status

Accepted

## Context

Podlord imports kubeconfig files from user-controlled paths such as `$HOME/.kube/config`.
Those files can change outside Podlord while existing sessions still depend on the previously imported content.
Keying imported contexts only by path causes changed files to overwrite previous app-owned kubeconfig copies.

## Decision

Podlord stores each imported kubeconfig snapshot by source path and content hash.
Re-importing the current path with identical content is a no-op and preserves its import time.
Changed content creates a new imported context snapshot and app-owned kubeconfig copy.

File, folder, drop and startup imports inspect all candidates before writing. If a context name matches an existing or another incoming context, one batch dialog lets the user select **Update** with a specific target or **Add**. A changed file cannot silently win by folder order. An Update preserves the target's display name and existing tab identity while resolving its connection through the selected new source. An Add gets a distinct display name. When two versions of the same source path are added, the older selected version keeps its own credentials.

Sources are displayed by their last actual import, newest first. The Map sorts contexts by last use when the overview is opened and keeps card positions stable while visible.

## Consequences

Older snapshots remain private history and are never overwritten.
Podlord does not watch source files. The explicit source refresh action updates same-path tabs with the new endpoint and credentials. A tab whose context name was removed stays visible but disconnects with an error. An explicit Update can also move a context to another source path without changing its open tabs. Later refreshes of the replaced old path do not silently add that context again.

The context catalog retains optional replacement and pinned-version identities. Older catalog files remain readable. An older binary may reject a profile once these fields are written, so rollback after a conflict decision requires a profile backup. Removing an updated context removes its replaced identities and affected tabs in the existing confirmed removal flow. Original kubeconfig files remain untouched.

Session selection and usage ordering are specified separately in [the operational specification](../spec/podlord-operational-spec.md#confirmed-source-and-session-requirements). Source import recency does not define session usage frequency.

## Ergänzung: Wie werden eingefügte Konfigurationen und Datei-Refresh behandelt?

2026-10-05: Paste und Dateiimport teilen Parser, Identitätsbildung, Sperre und atomare private Speicherung. Ein expliziter Origin-Pfad bleibt für relative Authentifizierungsdateien und Credential-Plugins maßgeblich; Paste schreibt diesen Origin nicht. Ein späterer Datei-Refresh kann deshalb ausdrücklich mit „nicht lesbar“ scheitern, ohne den privaten Snapshot zu verlieren.

2026-11-23: Ein ausdrücklich importierter neuer Stand desselben Pfads aktualisiert bestehende Tabs mit gleichnamigem Kontext. Alte Snapshots bleiben erhalten. Beim Wechsel werden alte Ressourcen, laufende Lesevorgänge, Terminals und Portweiterleitungen verworfen. Nicht importierte externe Dateiänderungen ändern keine aktive Verbindung. Ordnerimporte prüfen höchstens acht Unterordner-Ebenen und überspringen Dateien ohne Kubeconfig-Merkmale.

Um unbeschränktes Einlesen externer Konfigurationen zu vermeiden, gilt vor Parsing eine lokale Obergrenze von 16 MiB UTF-8. Der private Base64-/JSON-Umschlag ist auf 32 MiB begrenzt. Zu große oder beschädigte Daten werden nicht überschrieben. Die Ressourcen-YAML-Apply-Grenze bleibt ein separater, einstellbarer Vertrag.

2026-10-06: The same 16 MiB external-input budget applies to each referenced token, CA, client certificate and private-key file. The shared resolver rejects non-regular files before opening and reads at most the budget plus one byte, so a growing regular file cannot cause an unbounded read. Symlinks to regular files remain supported. This does not change the independent resource YAML apply setting or authorize credential providers. It is not a guarantee against a local process replacing filesystem entries between checking and opening.
