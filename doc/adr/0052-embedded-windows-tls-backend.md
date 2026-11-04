# 0052 Embedded Windows TLS Backend

## Status

Accepted on 2026-11-04. Native Windows verification remains required.

## Why Change The Windows TLS Backend?

The extracted Windows package reaches the real mutual-TLS boundary, but Qt
6.11.2's SChannel backend cannot import the fixture's valid P-256 client key.
The trusted request fails before populating the session cache. Rejected peers
still fail, which does not establish successful client authentication.

The user approved embedding Qt's OpenSSL backend instead of adding a separate
Windows private-key adapter. OpenSSL Crypto already supports native cloud
signing. Its matching SSL library and Qt plugin must accompany it. No Qt fork,
external CLI, user-installed runtime or credential service is introduced.

## What Must Remain True?

The Windows package includes one TLS backend, `qopensslbackend`, and matching
OpenSSL 3 DLLs from the actual build toolchain. The extracted dependency graph
and loaded modules must resolve application libraries inside that package.
Updates to these embedded libraries require rebuilt application packages.

Certificate verification is not bypassed. Explicit Kubernetes CAs, EC client
certificates, default Windows trust anchors and rejection of untrusted peers
must work through the real application ingress. A successful build or rejection
test alone does not establish this contract.

System-store fixtures may run only in the disposable Windows CI account. Each
uses a newly generated CA, refuses to replace an existing certificate, removes
only its own thumbprint and verifies removal. Normal application execution and
local developer checks never install these test roots.

## Alternatives And Consequences

Keeping SChannel would require investigating or maintaining an additional
Windows-specific private-key integration. Patching Qt would add a permanent
supplier fork. Both were rejected in favor of the already used cross-platform
OpenSSL implementation. This choice does not waive Windows trust tests,
package-size limits, rendering checks or startup requirements.

Qt documents its [TLS backend deployment choices](https://doc.qt.io/qt-6.11/ssl.html)
and [default system CA database](https://doc.qt.io/qt-6.11/qsslconfiguration.html).
These document the intended integration, not proof that a particular package
passes it. See the [test map](../spec/k3d-test-map.md) for executable evidence.
