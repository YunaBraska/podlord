# Security Policy

Podlord is a local desktop Kubernetes client. It handles kubeconfigs, credentials, cluster metadata, and secret references, so security issues matter.

## Supported Versions

Security fixes target the latest released version and `main`.

## Reporting A Vulnerability

Please report security issues privately instead of opening a public issue.

Use GitHub private vulnerability reporting when available:

<https://github.com/YunaBraska/podlord/security/advisories/new>

If that is unavailable, contact the maintainer through the GitHub profile listed on the repository.

## Handling Rules

- Do not include real kubeconfigs, tokens, certificates, or secret values in reports unless explicitly requested through a private channel.
- Reproduction steps with sanitized fixtures are strongly preferred.
- Public disclosure should wait until a fix is available.

## Security Expectations

Podlord should:

- avoid telemetry by default
- keep imported kubeconfig snapshots local
- avoid logging secret values and credentials
- redact Kubernetes Secret data in YAML views
- require explicit user action for sensitive value reveal/copy
- respect Kubernetes RBAC failures
- route Kubernetes calls through the request queue and configured session
