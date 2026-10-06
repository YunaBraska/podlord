#!/bin/sh
set -eu
PATH="/opt/homebrew/bin:$PATH"
export PATH
for tool in docker kubectl; do command -v "$tool" >/dev/null 2>&1 || { printf 'Missing tool: %s\n' "$tool" >&2; exit 1; }; done

# Only a local daemon may own the ephemeral Kubernetes test stack.
if [ -n "${DOCKER_CONTEXT:-}" ]; then
    DOCKER_ENDPOINT=$(docker context inspect "$DOCKER_CONTEXT" --format '{{.Endpoints.docker.Host}}')
elif [ -n "${DOCKER_HOST:-}" ]; then
    DOCKER_ENDPOINT=$DOCKER_HOST
else
    DOCKER_ENDPOINT=$(docker context inspect "$(docker context show)" --format '{{.Endpoints.docker.Host}}')
fi
case "$DOCKER_ENDPOINT" in
    unix://*|npipe://*) ;;
    *) printf '%s\n' 'Native Kubernetes tests require a local Docker socket; remote daemons are excluded.' >&2; exit 1 ;;
esac
DOCKER_HOST=$DOCKER_ENDPOINT
export DOCKER_HOST
unset DOCKER_CONTEXT
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
BUILD=${PODLORD_NATIVE_BUILD_DIR:-${XDG_CACHE_HOME:-$HOME/.cache}/podlord/native-build}
if [ "$(uname -s)" = Darwin ]; then
    # Standalone test drivers are not .app bundles; Qt cannot discover their private SSL libraries from Contents/Frameworks.
    ssl=$(awk '/^OPENSSL_SSL_LIBRARY:FILEPATH=/ {sub(/^[^=]*=/, ""); print; exit}' "$BUILD/CMakeCache.txt")
    [ -n "$ssl" ] && [ -f "$ssl" ] || { printf 'The built native OpenSSL runtime is unavailable.\n' >&2; exit 1; }
    DYLD_LIBRARY_PATH="$(dirname -- "$ssl")${DYLD_LIBRARY_PATH:+:$DYLD_LIBRARY_PATH}"
    export DYLD_LIBRARY_PATH
fi
[ -x "$BUILD/workspace_ui_test" ] || { printf 'Build the native UI tests first.\n' >&2; exit 1; }
[ -x "$BUILD/authentication_ui_test" ] || { printf 'Build the native authentication UI tests first.\n' >&2; exit 1; }
[ -x "$BUILD/pod_logs_ui_test" ] || { printf 'Build the native Pod log UI tests first.\n' >&2; exit 1; }
[ -x "$BUILD/yaml-draft-check-test" ] || { printf 'Build the native YAML draft UI tests first.\n' >&2; exit 1; }
[ -x "$BUILD/resource-delete-ui-test" ] || { printf 'Build the native deletion UI tests first.\n' >&2; exit 1; }
mkdir -p "$BUILD/e2e"
RUN=$(mktemp -d "$BUILD/e2e/run.XXXXXX")
LLVM_PROFILE_FILE="$RUN/%p-%m.profraw"
export LLVM_PROFILE_FILE
NAME=$(basename "$RUN" | tr '[:upper:]' '[:lower:]' | tr '.' '-')
NAME="podlord-native-$NAME"
CREATED=0
cleanup() {
    status=$?
    trap - 0
    if [ "$CREATED" = 1 ]; then
        if [ "$status" -ne 0 ]; then
            docker inspect "$NAME" --format 'Owned test cluster: exit={{.State.ExitCode}} oom={{.State.OOMKilled}} error={{.State.Error}}' >&2 || status=1
            (umask 077; docker logs --tail 80 "$NAME" > "$BUILD/e2e/$NAME-failure.log" 2>&1) || status=1
            printf 'Owned cluster failure log: %s/e2e/%s-failure.log\n' "$BUILD" "$NAME" >&2
        fi
        volumes=$(docker inspect "$NAME" --format '{{range .Mounts}}{{if eq .Type "volume"}}{{.Name}} {{end}}{{end}}') || status=1
        docker rm -fv "$NAME" || status=1
        remaining=$(docker container ls -a --filter "label=podlord.native.e2e=$NAME" --format '{{.ID}}') || status=1
        [ -z "${remaining:-}" ] || { printf 'Owned cluster containers remain: %s\n' "$remaining" >&2; status=1; }
        for volume in ${volumes:-}; do
            remaining=$(docker volume ls --filter "name=^$volume$" --format '{{.Name}}') || status=1
            [ -z "${remaining:-}" ] || { printf 'Owned cluster volume remains: %s\n' "$volume" >&2; status=1; }
        done
    fi
    rm -rf "$RUN"
    exit "$status"
}
trap cleanup 0
trap 'exit 129' HUP
trap 'exit 130' INT
trap 'exit 143' TERM
existing=$(docker container ls -a --filter "name=^/$NAME$" --format '{{.ID}}')
[ -z "$existing" ] || { printf 'Refusing an existing cluster identity: %s\n' "$NAME" >&2; exit 1; }
CREATED=1
docker create --name "$NAME" --label "podlord.native.e2e=$NAME" --privileged \
    --tmpfs /run --tmpfs /var/run -p 127.0.0.1::6443 rancher/k3s:v1.35.5-k3s1 \
    server --disable=traefik --disable=servicelb --write-kubeconfig-mode=600 \
    --kubelet-arg='eviction-hard=memory.available<100Mi,nodefs.available<512Mi,imagefs.available<512Mi,nodefs.inodesFree<5%,imagefs.inodesFree<5%'
# Seed before startup; this needs neither ctr nor an already active import watcher.
mkdir -p "$RUN/k3s-data/agent/images"
docker image save rancher/k3s:v1.35.5-k3s1 > "$RUN/k3s-data/agent/images/pod-image.tar"
docker cp "$RUN/k3s-data/." "$NAME:/var/lib/rancher/k3s/"
docker start "$NAME"
port=$(docker port "$NAME" 6443/tcp | sed 's/^127\.0\.0\.1://')
attempt=0
while :; do
    attempt=$((attempt + 1))
    if docker exec "$NAME" cat /etc/rancher/k3s/k3s.yaml > "$RUN/original" 2>/dev/null; then
        sed "s/127.0.0.1:6443/127.0.0.1:$port/" "$RUN/original" > "$RUN/kubeconfig"
        if kubectl --kubeconfig "$RUN/kubeconfig" get --raw=/readyz >/dev/null 2>&1; then break; fi
    fi
    [ "$attempt" -lt 60 ] || { docker logs --tail 60 "$NAME" >&2; printf 'Local Kubernetes did not become ready.\n' >&2; exit 1; }
    sleep 2
done
chmod 600 "$RUN/kubeconfig"
kubectl --kubeconfig "$RUN/kubeconfig" create configmap podlord-native-e2e --from-literal=check=native-api
kubectl --kubeconfig "$RUN/kubeconfig" create secret generic podlord-native-secret-e2e --from-literal=alpha=podlord-alpha-real-secret --from-literal=beta=podlord-beta-real-secret
kubectl --kubeconfig "$RUN/kubeconfig" apply -f - <<'EOF'
apiVersion: v1
kind: ServiceAccount
metadata:
  name: podlord-native-log-e2e
  namespace: default
automountServiceAccountToken: false
---
apiVersion: v1
kind: Pod
metadata:
  name: podlord-native-log-e2e
  namespace: default
spec:
  serviceAccountName: podlord-native-log-e2e
  automountServiceAccountToken: false
  containers:
  - name: alpha
    image: rancher/k3s:v1.35.5-k3s1
    imagePullPolicy: Never
    command: ["/bin/sh", "-c", "echo podlord-alpha-real-log; sleep 3600"]
  - name: beta
    image: rancher/k3s:v1.35.5-k3s1
    imagePullPolicy: Never
    command: ["/bin/sh", "-c", "echo podlord-beta-real-log; sleep 3600"]
EOF
if ! kubectl --kubeconfig "$RUN/kubeconfig" wait --for=condition=Ready pod/podlord-native-log-e2e --timeout=90s; then
    kubectl --kubeconfig "$RUN/kubeconfig" describe pod podlord-native-log-e2e >&2
    exit 1
fi
kubectl --kubeconfig "$RUN/kubeconfig" logs podlord-native-log-e2e -c alpha --timestamps=true --tail=100
kubectl --kubeconfig "$RUN/kubeconfig" logs podlord-native-log-e2e -c beta --timestamps=true --tail=100
QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software QT_QUICK_CONTROLS_STYLE=Basic \
    PODLORD_E2E_SCREENSHOT="$BUILD/e2e/native-inspector.png" \
    "$BUILD/workspace_ui_test" real "$RUN/kubeconfig"
printf 'Native Qt Quick / real Kubernetes TLS + client certificate + YAML/values/Secret E2E passed.\n'
kubectl --kubeconfig "$RUN/kubeconfig" get configmap podlord-native-e2e -o json > "$RUN/configmap-before-check.json"
kubectl --kubeconfig "$RUN/kubeconfig" get secret podlord-native-secret-e2e -o json > "$RUN/secret-before-check.json"
QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software QT_QUICK_CONTROLS_STYLE=Basic \
    PODLORD_E2E_SCREENSHOT="$BUILD/e2e/native-yaml-check.png" \
    "$BUILD/yaml-draft-check-test" real "$RUN/kubeconfig"
kubectl --kubeconfig "$RUN/kubeconfig" get configmap podlord-native-e2e -o json > "$RUN/configmap-after-check.json"
kubectl --kubeconfig "$RUN/kubeconfig" get secret podlord-native-secret-e2e -o json > "$RUN/secret-after-check.json"
cmp -s "$RUN/configmap-before-check.json" "$RUN/configmap-after-check.json" || { printf 'Local draft checking changed the real ConfigMap.\n' >&2; exit 1; }
cmp -s "$RUN/secret-before-check.json" "$RUN/secret-after-check.json" || { printf 'Local draft checking changed the real Secret.\n' >&2; exit 1; }
printf 'Native Qt Quick / local draft checks against real ConfigMap and masked Secret passed; server objects unchanged.\n'
QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software QT_QUICK_CONTROLS_STYLE=Basic \
    "$BUILD/authentication_ui_test" real "$RUN/kubeconfig"
printf 'Native Qt Quick / confirmed exec client certificate + real Kubernetes TLS E2E passed.\n'
QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software QT_QUICK_CONTROLS_STYLE=Basic \
    "$BUILD/pod_logs_ui_test" real "$RUN/kubeconfig"
printf 'Native Qt Quick / multi-container Pod logs + real Kubernetes TLS E2E passed.\n'
kubectl --kubeconfig "$RUN/kubeconfig" get secret podlord-native-secret-e2e -o=jsonpath='{.data}' > "$RUN/secret-data-before-apply.json"
QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software QT_QUICK_CONTROLS_STYLE=Basic \
    PODLORD_E2E_SCREENSHOT="$BUILD/e2e/native-yaml-apply.png" \
    "$BUILD/yaml-draft-check-test" real_apply "$RUN/kubeconfig"
[ "$(kubectl --kubeconfig "$RUN/kubeconfig" get configmap podlord-native-e2e -o=jsonpath='{.data.check}')" = native-reconciled ] || { printf 'Native apply did not change real ConfigMap data.\n' >&2; exit 1; }
[ "$(kubectl --kubeconfig "$RUN/kubeconfig" get configmap podlord-native-e2e -o=jsonpath='{.metadata.labels.foreign}')" = retained ] || { printf 'Native reconciliation lost a foreign field.\n' >&2; exit 1; }
kubectl --kubeconfig "$RUN/kubeconfig" get secret podlord-native-secret-e2e -o=jsonpath='{.data}' > "$RUN/secret-data-after-apply.json"
cmp -s "$RUN/secret-data-before-apply.json" "$RUN/secret-data-after-apply.json" || { printf 'Native unrelated Secret edit changed original data bytes.\n' >&2; exit 1; }
printf 'Native Qt Quick / confirmed real JSON Patch + concurrent-change reconciliation + entered Secret write/read-back/remasking + original Secret bytes passed.\n'
kubectl --kubeconfig "$RUN/kubeconfig" create namespace visual-a
for style in Basic Fusion; do
    for scenario in real real_race; do
        case "$scenario" in
            real) resource=podlord-delete-e2e ;;
            real_race) resource=podlord-delete-race-e2e ;;
        esac
        kubectl --kubeconfig "$RUN/kubeconfig" create configmap "$resource" -n visual-a --from-literal=identity=original
        QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software QT_QUICK_CONTROLS_STYLE="$style" \
            PODLORD_DELETE_FRAME="$BUILD/e2e/native-delete-$scenario-$style.png" \
            "$BUILD/resource-delete-ui-test" "$scenario" "$RUN/kubeconfig"
    done
done
printf 'Native Qt Quick / real Kubernetes deletion + UID replacement protection + explicit refreshed-target confirmation passed in Basic and Fusion.\n'
# Keep the pinned cluster image for repeatable runs; never globally prune shared Docker state.
