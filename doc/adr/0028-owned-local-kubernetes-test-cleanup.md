# How are local Kubernetes test resources removed safely?

Status: Accepted

## Context

Test cleanup must not delete another local stack, modify the user's kubeconfig,
or prune a shared image cache. Name-prefix deletion cannot establish ownership.
The local development VM can also have less than ten percent free space while
still having enough absolute capacity for the isolated test workload.

## Decision

Each C# test run assigns a unique owner. Kubernetes node and load-balancer
containers carry `podlord.test.run`; cleanup first checks every container of the
exact owned cluster. An unknown or foreign owner prevents deletion. Cleanup uses
a private kubeconfig and verifies container and mounted-volume removal.

k3d creates its tools container separately, without custom runtime labels
([k3d 5.9.0 tools implementation](https://github.com/k3d-io/k3d/blob/v5.9.0/pkg/client/tools.go)).
The fixture checks that the cluster container namespace is empty before creation,
then records that tools container's complete immutable runtime ID. Cleanup may
accept this one recorded child only when its ID, exact name, cluster, application
label and `noRole` role all match, and no foreign owner is present. A name alone
never authorizes its deletion. Missing ownership evidence remains a reported
cleanup failure, not permission to delete unknown resources.

The isolated C# cluster uses the same pinned k3s image and absolute 512 MiB
node/image-filesystem eviction threshold as the native local test lane. Memory
and inode safeguards remain enabled. These settings affect only disposable test
nodes, not application behavior or shared/production cluster configuration. Node
conditions and taints are included in scenario diagnostics.

## Verification and remaining limits

Real local node evidence reproduced `DiskPressure=True` with a scheduling taint.
Public cleanup tests run the real shell, Docker CLI and k3d executable against a
minimal fake Docker Engine boundary. Real Kubernetes suites verify complete
owned-resource cleanup. Process or daemon failure before ownership capture may
require explicit recovery; cleanup must fail visibly rather than broaden scope.
Shared images are retained for reuse; no global prune is performed.
