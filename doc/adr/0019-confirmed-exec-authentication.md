# 0019 - How Does Native Authentication Obtain Execution Authority?

## Status

Accepted implementation boundary for ERR-004-006, ERR-011-014 and STR-005. Full authentication compatibility and conformance remain pending.

## Decision

Import, listing, context resolution and restored workspaces never execute credential commands. Resolution produces typed configuration from an owned source snapshot. A visible confirmation identifies the session, cluster and resolved executable, warns that imported arguments run with the user's permissions and that login may open a browser. Untrusted text uses plain-text presentation. Environment values, arguments and credential output are not diagnostic messages.

One process owner executes the confirmed command using Qt's process API, without a shell. It passes literal arguments, configured environment and Kubernetes exec information, closes stdin and resolves relative executable paths against the original source directory. API versions v1 and v1beta1 are accepted with their respective interactiveMode rules. Never and IfAvailable use interactive=false; Always fails explicitly because this desktop lane has no terminal input. Reserved cluster configuration extensions remain an explicit unsupported path rather than silently omitted input. See the [Kubernetes credential plugin contract](https://kubernetes.io/docs/reference/access-authn-authz/authentication/#client-go-credential-plugins) and [kubeconfig API](https://kubernetes.io/docs/reference/config-api/kubeconfig.v1/).

Credential identity is the owned source snapshot plus cluster and user references, not the context name, namespace or tab. Tabs sharing it share one running process and its approved in-memory result. Approval is not persisted across application restarts. Unrelated credential identities do not share authority. A returned bearer token or complete PEM certificate/key pair is validated before any Kubernetes request. Output must match the configured ExecCredential version; optional expiry must be a valid future RFC3339 timestamp. Static and exec credentials cannot be combined.

HTTP 401 and credential expiry invalidate approved credentials, suspend matching contexts and remove unsent work. Neither ordinary refresh nor tab switching invokes another credential process. Sent reads finish in their original session. A revision prevents an old in-flight authentication failure from suspending subsequently installed credentials. A single expiry timer belongs to the read client; it never launches login. A failed or cancelled attempt requires renewed confirmation. The existing loading indicator shows known authentication activity while cached UI remains usable, without invented percentages.

Authentication process completion is not proof of recovery: the affected API failure remains until an actual successful check. Completed shared authentication prioritizes the currently visible matching session. Closed sessions are not reopened. Changed credentials flush idle network connections before a new dispatch so TLS certificates are not trapped in a pooled old connection. See [Qt connection cache semantics](https://doc.qt.io/qt-6/qnetworkaccessmanager.html#clearConnectionCache).

The process owner rejects stdout above 1 MiB, drains and discards stderr, and never puts either payload into error messages. There is no automatic login deadline. User cancellation kills and reaps the owned credential process; application shutdown also does so. A browser or detached descendant opened independently by a provider is not claimed to be owned or closed. The implementation uses [Qt process APIs](https://doc.qt.io/qt-6/qprocess.html), not a second auth framework.

## Verification And Rollback

The existing test map owns actual QML confirmation, a real external credential process, actual stores/HTTP/queue/cache behavior, expiry, failed reauthentication, shared tabs, shutdown and real local Kubernetes TLS/client-certificate evidence. The identity provider is the explicit external simulated boundary; no owned authentication implementation is replaced. Terminal interaction, cluster configuration extensions, legacy providers, unrelated-credential isolation, provider descendants, real browser OAuth, certificate rotation races and broader failure/validation/platform checks remain open evidence, not removed requirements.

No legacy code, profile or launcher is changed. Removing this native increment leaves the isolated legacy lane usable. No disk resource cache or persisted credential cache is introduced.
