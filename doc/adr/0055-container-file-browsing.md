# 0055 Container File Browsing

## Status

Accepted on 2026-11-19.

## Context

The inspector has a direct Kubernetes terminal, but users need to inspect and download files from running Pod containers without installing `kubectl`. Kubernetes has no general file listing or download endpoint. Its exec stream can read files with tools present in the container.

## Decision

The Files inspector tab is available on a Pod or workload with a linked running Pod. The user chooses a Pod and container. A directory listing starts when the tab opens. Download starts only when the user selects a listed regular file, then the platform save dialog chooses its local destination. Podlord uses the existing Kubernetes request scheduler, credentials, TLS rules and rate limit to verify the Pod identity and declared container immediately before each exec. The WebSocket negotiates `v5.channel.k8s.io`. Fixed read-only shell commands list directory entries or stream one file. Paths are arguments, not command text. No `kubectl` process or server-side archive is used.

Listings are limited to 1 MiB and 1,000 entries. Files are limited to 32 MiB in memory and saved atomically where the platform supports local files. Symlinks are listed but cannot be followed or downloaded. Exec stderr is discarded. File bytes, names and paths are not sent to AI or written to application logs. Closing the tab cancels its pending operation. Missing exec permission, Pod replacement, missing container tools and failed streams produce a short error without retries. The tab has no write, edit or upload action.

## Consequences

Listing requires `/bin/sh` and its `printf` and test commands. Download also requires `cat`. Distroless containers may not support browsing. A running Pod can still change after verification, so the UI never claims the saved copy is a stable cluster snapshot. The selected Pod and container remain visible above the listing.

## Reference

- [Kubernetes exec streaming](https://kubernetes.io/blog/2024/08/20/websockets-transition/)
