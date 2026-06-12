# k3d Test Map

Podlord's Kubernetes integration tests use a disposable k3d cluster. The suite is intentionally slow and proof-oriented.

## Cluster

- One k3d server node
- Disposable kubeconfig under the test temp directory
- Admin context from k3d
- Limited RBAC context generated from a ServiceAccount token

## Scenarios

| Scenario | Resources | Proof |
|---|---|---|
| Flat resource explorer | Namespace, Node, Pod, Deployment, Service, EndpointSlice, ConfigMap, Secret, Job, CronJob, PVC, NetworkPolicy, Event | Podlord can list a real cluster across namespaces and API groups. |
| Filters | Pod and Deployment queries by kind, namespace, status, and search | Filtering is applied after real API reads. |
| Secret redaction | `Secret/podlord-secret` | Secret data and managed fields do not reach YAML output. |
| Broken workload | `Deployment/podlord-broken` and its pod events | Image-pull failures surface as status/events. |
| Logs | `Pod/podlord-log` | Pod log tail uses the bound kubeconfig context. |
| Jobs | `Job/podlord-success`, `Job/podlord-fail`, `CronJob/podlord-cron` | Batch statuses are visible. |
| Networking | `Service/podlord-healthy`, generated EndpointSlice, NetworkPolicy | Network resources list and inspect. |
| Cluster-scoped detail | Node and Namespace | Cluster-scoped detail paths work without namespaces. |
| RBAC | `ServiceAccount/podlord-limited` | Forbidden API responses become explicit freshness failures. |
| Boundary validation | Pod detail without namespace | Invalid input fails before network calls. |

Run:

```sh
scripts/test.sh
```

The script starts Colima when available, ensures k3d/kubectl exist, creates the cluster during tests, and deletes it afterward.
