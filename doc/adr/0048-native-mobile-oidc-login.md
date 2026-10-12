# 0048 Native Mobile OIDC Login

## Status

Accepted release boundary; implementation and device evidence pending.

## Decision

Mobile release requires native, user-confirmed OIDC browser login. The flow uses Authorization Code with PKCE S256, unique state and nonce, external system browser, a registered application redirect, and validated ID tokens. It never runs a kubeconfig `exec` command, opens a browser passively, or retries an authentication failure without renewed confirmation. Existing static tokens and client certificates remain supported independently; their availability does not waive the OIDC requirement.

The project remains MIT-licensed. Do not add Qt Network Authorization to the distributed application: its available open-source license is GPLv3, not the license chosen for this project. Use the existing Qt networking foundation and only platform callback integration that can be shipped under the current license. Desktop process authentication remains governed by ADR 0019. A vendor-specific `exec` provider is not presumed to be a standard OIDC client.

## Consequences And Verification

The mobile flow is a release blocker until discovery, redirect registration, token validation, cancellation, 401 behavior and secret handling pass public-boundary tests, and the installed iOS and Android applications complete a real browser return on devices. A simulator build or a cached-token read does not close this gate. Follow the [native-app OAuth guidance](https://www.rfc-editor.org/info/rfc8252/), [OpenID Connect Core](https://openid.net/specs/openid-connect-core-1_0-18.html), [Kubernetes authentication](https://kubernetes.io/docs/reference/access-authn-authz/authentication/) and [Qt module licensing](https://doc.qt.io/qt-6/qtnetworkauth-index.html).
