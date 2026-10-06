#!/bin/sh
set -eu
umask 077
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
BUILD=${PODLORD_NATIVE_BUILD_DIR:-${XDG_CACHE_HOME:-$HOME/.cache}/podlord/native-build}
for tool in docker k3d node cmake; do
    command -v "$tool" >/dev/null 2>&1 || { printf 'Required local test tool is missing: %s\n' "$tool" >&2; exit 1; }
done
if [ -n "${DOCKER_CONTEXT:-}" ]; then
    endpoint=$(docker context inspect "$DOCKER_CONTEXT" --format '{{.Endpoints.docker.Host}}')
elif [ -n "${DOCKER_HOST:-}" ]; then endpoint=$DOCKER_HOST
else endpoint=$(docker context inspect --format '{{.Endpoints.docker.Host}}'); fi
case "$endpoint" in unix://*|npipe://*) ;; *) printf 'Only a local Docker socket is allowed.\n' >&2; exit 1 ;; esac
export DOCKER_HOST="$endpoint"
unset DOCKER_CONTEXT
cmake --build "$BUILD" --target k3d-import-test --parallel "${PODLORD_BUILD_JOBS:-4}"
mkdir -p "$BUILD/e2e"
RUN=$(mktemp -d "$BUILD/e2e/k3d-import.XXXXXX")
export KUBECONFIG="$RUN/private-kubeconfig"
TOKEN=$(basename "$RUN")
NAME=podlord-import-$(printf '%s' "$TOKEN" | tr '[:upper:].' '[:lower:]-')
owned=false
cleanup() {
    status=$?
    trap - 0
    if [ "$owned" = true ]; then
        safe=true
        tagged=false
        tools=false
        nodes=$(docker ps -aq --filter "label=k3d.cluster=$NAME") || safe=false
        for node in $nodes; do
            owner=$(docker inspect --format '{{ index .Config.Labels "podlord.test.owner" }}' "$node") || safe=false
            if [ "$owner" = "$TOKEN" ]; then tagged=true
            else
                helper=$(docker inspect --format '{{.Name}} {{ index .Config.Labels "k3d.role" }}' "$node") || safe=false
                if [ "$helper" = "/k3d-$NAME-tools noRole" ]; then tools=true
                else safe=false; fi
            fi
        done
        if [ "$tools" = true ] && [ "$tagged" = false ]; then safe=false; fi
        if [ "$safe" = true ]; then
            k3d cluster delete "$NAME" > "$RUN/cleanup.log" 2>&1 || { cat "$RUN/cleanup.log" >&2; status=1; }
        else printf 'Cleanup refused: unexpected ownership for %s.\n' "$NAME" >&2; status=1; fi
    fi
    rm -rf "$RUN"
    exit "$status"
}
trap cleanup 0
trap 'exit 129' HUP
trap 'exit 130' INT
trap 'exit 143' TERM
k3d cluster list -o json > "$RUN/before.json"
node - "$RUN/before.json" "$NAME" <<'NODE'
const fs = require('node:fs');
const clusters = JSON.parse(fs.readFileSync(process.argv[2], 'utf8'));
if (!Array.isArray(clusters) || clusters.some(cluster => cluster.name === process.argv[3])) process.exit(1);
NODE
owned=true
port=$(node -e 'const server=require("node:net").createServer(); server.on("error",()=>process.exit(1)); server.listen(0,"127.0.0.1",()=>{const port=server.address().port; server.close(()=>process.stdout.write(String(port)));});')
k3d cluster create "$NAME" --image rancher/k3s:v1.35.5-k3s1 --servers 1 --agents 0 --no-lb \
    --api-port "127.0.0.1:$port" --kubeconfig-update-default=false --kubeconfig-switch-context=false \
    --k3s-arg '--disable=traefik@server:*' --k3s-arg '--disable=servicelb@server:*' --k3s-arg '--disable=metrics-server@server:*' \
    --runtime-label "podlord.test.owner=$TOKEN" --wait --timeout 120s > "$RUN/create.log" 2>&1 \
    || { tail -n 30 "$RUN/create.log" >&2; exit 1; }
if [ "$(uname -s)" = Darwin ]; then
    ssl=$(sed -n 's/^OPENSSL_SSL_LIBRARY:FILEPATH=//p' "$BUILD/CMakeCache.txt")
    [ -f "$ssl" ] || { printf 'The configured test OpenSSL library is missing.\n' >&2; exit 1; }
    DYLD_LIBRARY_PATH="$(dirname "$ssl")${DYLD_LIBRARY_PATH:+:$DYLD_LIBRARY_PATH}"
    export DYLD_LIBRARY_PATH
fi
export LLVM_PROFILE_FILE="$RUN/%p-%m.profraw" QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software QT_QUICK_CONTROLS_STYLE=Fusion
"$BUILD/k3d-import-test" real "$NAME"
printf 'Real generated K3D import, trusted API loading and scoped cleanup: %s\n' "$NAME"
