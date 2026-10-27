# Native OKE API-key tokens

## Decision

Mobile recognizes the official `oci ce cluster generate-token` exec command only with explicit API-key credentials. The complete user, tenancy, fingerprint, region and unencrypted PEM key must be supplied together through the imported exec environment using Oracle's `OCI_CLI_*` names. Podlord signs the OKE cluster request locally and returns a four-minute bearer token; it does not run `oci` or `kubectl`, use a broker, or contact Oracle merely to mint the token. The key is kept in the immutable private kubeconfig snapshot and process memory, never in public source metadata or errors.

Sources can now explicitly import an OCI config profile and a separately selected unencrypted PEM key for one owned context. Podlord validates both, stores the resulting credential in its protected profile rather than the immutable kubeconfig snapshot, and removes it when that context is removed. An explicitly imported profile or key-file reference is resolved from these selected files; no arbitrary path is followed during sign-in. Encrypted keys, cloud-instance identities and other OCI auth modes are not reinterpreted. They fail before a Kubernetes request on mobile. Desktop retains external exec behavior when no key has been imported.

Imported credentials and their in-memory approvals are isolated per context. Successful replacement and removal revoke pending and cached approvals, including credentials retained by closed resource sessions. Ready OCI/GKE token connections no longer retain imported signing keys. Already dispatched requests still finish under their original authority. If catalog removal succeeds but a private credential file cannot be deleted, return an explicit cleanup failure and reconcile the UI with the committed catalog. Do not report successful private-key deletion or delete the user's selected original files.

## Rationale and verification

Oracle's generated kubeconfig contains the command and cluster ID but does not contain the region or API key. Guessing either from a phone's environment could select the wrong identity. Oracle documents `OCI_CLI_KEY_CONTENT` for PEM keys, and OCI CLI's OKE token is the base64url encoding of a signed cluster-request URL with a four-minute expiry. Public parser and independently verified RSA signature tests cover this path. Installed mobile sign-in and a live OCI tenancy remain release gates.
