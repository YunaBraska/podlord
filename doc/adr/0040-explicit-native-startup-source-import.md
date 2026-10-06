# How Does An Explicit Startup Source Enter The Native Application?

Status: accepted implementation of LEG-001 within the explicit-import contract.

## Context

The reference accepts explicit startup kubeconfig paths. The native application
previously rejected them. Import must not introduce ambient authentication or
delay the initial window behind source parsing. The existing workspace already
owns asynchronous file/folder import, partial-import reporting and catalog reload.

## Decision

Expose `--kubeconfig PATH` once, accepting the same file, local file URL or folder
input as the existing Import action. An empty or repeated option is rejected
before creating an application profile. Help and version remain side-effect-free,
including when the import option is present.

After loading the window, one single-shot timer waits for the existing workspace
to become available, disconnects its readiness subscription and submits to
`Workspace.importFile`. It does not parse configuration, persist snapshots or
perform authentication itself. Missing or malformed sources keep the application
open and use the existing import error presentation. Closing the application
destroys unsent startup work with its owning timer.

No implicit home/KUBECONFIG import, positional-path interpretation, automatic
session activation or second import queue is added. Importing several explicit
files together uses the supported folder workflow. Arbitrary repeated paths and
automatic source watchers remain separate parity gaps.

## Verification And Rollback

Thirteen public process scenarios failed against the previous application due
to the missing option. All 25 startup scenarios then passed on macOS arm64 using
the official Qt SDK. The real source CLI independently checks stored snapshots;
a local server stands only at the external Kubernetes boundary. No startup
scenario contacted that boundary or ran an exec credential provider. Original
files remain unchanged; unchanged re-import deduplicates, and changed content
creates a second immutable snapshot.

The change adds no profile format. Removing the option restores previous startup
behavior without a data migration. All 25 startup scenarios also passed against
the actual portable application with the Cocoa platform. An earlier installed
run timed out on unchanged re-import; the isolated retry and complete repeat
passed, but the initial failure remains an unexplained stability risk rather
than a claimed fix. Other-platform checks remain separate evidence gates.
