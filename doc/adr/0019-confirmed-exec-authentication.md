# 0019 - How Does Native Authentication Obtain Execution Authority?

## Status

Accepted implementation boundary for ERR-004-006, ERR-011-014 and STR-005. Full authentication compatibility and conformance remain pending.

## Decision

Import, listing, context resolution and restored workspaces never execute credential commands. Resolution produces typed configuration from an owned source snapshot. A visible confirmation identifies the session, cluster and resolved executable, warns that imported arguments run with the user's permissions and that login may open a browser. Untrusted text uses plain-text presentation. Environment values, arguments and credential output are not diagnostic messages.

One process owner executes the confirmed command using Qt's process API, without a shell. It passes literal arguments, configured environment and Kubernetes exec information, closes stdin and resolves relative executable paths against the original source directory. API versions v1 and v1beta1 are accepted with their respective interactiveMode rules. Never and IfAvailable use interactive=false; Always fails explicitly because this desktop lane has no terminal input. Reserved cluster configuration extensions remain an explicit unsupported path rather than silently omitted input. See the [Kubernetes credential plugin contract](https://kubernetes.io/docs/reference/access-authn-authz/authentication/#client-go-credential-plugins) and [kubeconfig API](https://kubernetes.io/docs/reference/config-api/kubeconfig.v1/).

Credential identity is the owned source snapshot plus cluster and user references, not the context name, namespace or tab. Tabs sharing it share one running process and its approved in-memory result. Approval is not persisted across application restarts. Unrelated credential identities do not share authority. A returned bearer token or complete PEM certificate/key pair is validated before any Kubernetes request. Output must match the configured ExecCredential version; optional expiry must be a valid future RFC3339 timestamp. Static and exec credentials cannot be combined.

Explicitly imported OCI API keys and GKE service-account keys are the exception: their credential identity is the owning context, even when two contexts share cluster and user references. Successful replacement or context removal revokes the approved result and pending login for that context and closes its existing container terminals and port-forwards. Other contexts remain connected. Already-dispatched finite HTTP requests finish under their captured authority. Late completion cannot restore approval. Retained resource caches keep their rows, not connection credentials. A new import or sign-in needs explicit confirmation. Explicitly imported GKE service-account keys use the native implementation on desktop and mobile. Without an imported key, desktop GKE retains confirmed exec behavior. Ordinary exec identities retain the sharing rule above. These stream and desktop-import decisions were confirmed on 2026-10-27.

Detail authority is bound to the existing connection revision. Starting an edit and revealing a loaded Secret require a successful fresh detail read under the current credentials. Existing local drafts remain editable after cache expiry or a failed request. Their text and discard controls are preserved. Applying a draft requires its captured authority to match the current usable credentials. Replacement credentials cannot acquire that authority from an old read or preview: a successful detail read under the replacement credentials is required first. Resource-cache expiry is not a new deadline for an already started local draft. Already dispatched finite reads and writes finish under their captured authority. Queued writes recheck authority before dispatch.

Native OIDC uses the same existing OpenSSL RS256 verifier on macOS, Linux, Windows and Android. iOS retains its Apple Security verifier. A recognized OIDC context must not lose ID-token or PKCE support solely because it runs on Linux or Windows. Both paths retain the same signature, issuer, audience, nonce, expiry and callback checks. This does not register desktop callback schemes or substitute simulated-provider tests for installed browser-return evidence.

HTTP 401 and credential expiry invalidate approved credentials, suspend matching contexts and remove unsent work. Neither ordinary refresh nor tab switching invokes another credential process. Sent reads finish in their original session. A revision prevents an old in-flight authentication failure from suspending subsequently installed credentials. A single expiry timer belongs to the read client; it never launches login. A failed or cancelled attempt requires renewed confirmation. The existing loading indicator shows known authentication activity while cached UI remains usable, without invented percentages.

Authentication process completion is not proof of recovery: the affected API failure remains until an actual successful check. Completed shared authentication prioritizes the currently visible matching session. Closed sessions are not reopened. Changed credentials flush idle network connections before a new dispatch so TLS certificates are not trapped in a pooled old connection. See [Qt connection cache semantics](https://doc.qt.io/qt-6/qnetworkaccessmanager.html#clearConnectionCache).

The process owner rejects stdout above 1 MiB, drains and discards stderr, and never puts either payload into error messages. There is no automatic login deadline. User cancellation kills and reaps the owned credential process; application shutdown also does so. A browser or detached descendant opened independently by a provider is not claimed to be owned or closed. The implementation uses [Qt process APIs](https://doc.qt.io/qt-6/qprocess.html), not a second auth framework.

On iOS and Android, Qt does not support the desktop exec-process path. Import and listing retain exec-backed contexts. Recognized OIDC and AKS personal configurations use an explicitly confirmed in-process Authorization Code/PKCE flow through the system browser for `auto` or `interactive`; an explicitly selected device-code grant remains device-code. Provider discovery, token exchange and polling use Qt Network, and tokens remain in memory. No imported command or `kubectl` runs for these flows. Unsupported OIDC options and other external or environment-bound commands fail explicitly before any Kubernetes request. VM and CI identities are not copied to a phone and no remote credential broker is deployed. Token and client-certificate contexts remain separate supported paths. These implementations alone do not establish mobile release readiness: iOS personal GKE registration, live provider consent and device/simulator evidence remain open.

## How does personal GKE login return to iOS?

Use the existing native OIDC Authorization Code/PKCE implementation through the
system browser. The registered public iOS client ID is supplied once through
`PODLORD_GKE_IOS_CLIENT_ID` at configure time. Derive its reverse-client-ID scheme
for both the compiled login configuration and the application Info.plist. Keep
the generic `berlin.yuna.podlord:/oauth2redirect` registration for other supported
OIDC providers. No client secret, provider CLI, embedded browser or new OAuth
runtime dependency is introduced.

The Google path requests cloud-platform and userinfo.email scopes in addition
to OIDC openid. State, nonce, S256 verifier, exact callback URI, signature,
issuer, audience and expiry checks remain owned by the shared OIDC flow. Only
the resulting access token is passed to Kubernetes. Cancellation completes once,
stops the owned flow and ignores late callbacks. Credentials remain in memory.

A missing client registration is not a working personal login. Report that
configuration explicitly, or use an already explicitly imported service-account
credential. The existing service-account-only path remains independent. Live
Google consent, the registered bundle identity and browser return on installed
iOS must pass before the personal-login release gate can be approved. See
[Google's iOS registration requirements](https://developers.google.com/identity/sign-in/ios/start-integrating)
and its [native OAuth redirect contract](https://developers.google.com/identity/protocols/oauth2/native-app).

## Verification And Rollback

The existing test map owns actual QML confirmation, a real external credential process, actual stores/HTTP/queue/cache behavior, expiry, failed reauthentication, shared tabs, shutdown and real local Kubernetes TLS/client-certificate evidence. The identity provider is the explicit external simulated boundary; no owned authentication implementation is replaced. Terminal interaction, cluster configuration extensions, legacy providers, unrelated-credential isolation, provider descendants, real browser OAuth, certificate rotation races and broader failure/validation/platform checks remain open evidence, not removed requirements.

No legacy code, profile or launcher is changed. Removing this native increment leaves the isolated legacy lane usable. No disk resource cache or persisted credential cache is introduced.
