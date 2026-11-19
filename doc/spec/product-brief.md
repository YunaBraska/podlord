# Podlord product brief

PodLord is a standalone C++ and Qt Quick Kubernetes console. Its flat resource
view, map and inspector keep the current context visible while people investigate
cluster problems. Kubeconfigs are imported into app-owned snapshots. Normal use
needs no shell Kubernetes client.

The workspace presents resource health, links, events, logs, YAML and supported
actions. Reads use a shared cache and request queue. An optional selected AI
model explains observed problems using redacted cluster facts and linked
resources. The model must distinguish observations from possible causes.

Secrets and credentials stay protected. The app must not log their values or
include them in an AI request. Actions remain bound to the selected context.

The [operational specification](podlord-operational-spec.md) owns detailed
behavior. [Release channels](../../docs/release-channels.md) owns distribution.
