#!/bin/sh
# Desktop comparison shares window focus; run this lane on its own.
set -eu
PATH="/opt/homebrew/bin:$PATH"
export PATH
umask 077
for tool in docker kubectl node rg; do
    command -v "$tool" >/dev/null 2>&1 || { printf 'Missing tool: %s\n' "$tool" >&2; exit 1; }
done
BUILD=${PODLORD_NATIVE_BUILD_DIR:-${XDG_CACHE_HOME:-$HOME/.cache}/podlord/native-build}
if [ "$(uname -s)" = Darwin ]; then
    # Standalone drivers need the configured SSL runtime after the shell has started.
    ssl=$(awk '/^OPENSSL_SSL_LIBRARY:FILEPATH=/ {sub(/^[^=]*=/, ""); print; exit}' "$BUILD/CMakeCache.txt")
    [ -n "$ssl" ] && [ -f "$ssl" ] || { printf 'The built native OpenSSL runtime is unavailable.\n' >&2; exit 1; }
    DYLD_LIBRARY_PATH="$(dirname -- "$ssl")${DYLD_LIBRARY_PATH:+:$DYLD_LIBRARY_PATH}"
    export DYLD_LIBRARY_PATH
fi
NATIVE=${PODLORD_NATIVE_APP:-$BUILD/podlord-native.app/Contents/MacOS/podlord-native}
MODE=${1:-desktop}
[ "$#" -le 1 ] || { printf 'Expected one mode: desktop, native-e2e, native-terminal-e2e, native-window-e2e, native-forward-e2e, native-search-e2e, native-fields-e2e, native-health-e2e, radar-parity or release-review.\n' >&2; exit 1; }
case "$MODE" in desktop|native-e2e|native-terminal-e2e|native-window-e2e|native-forward-e2e|native-search-e2e|native-fields-e2e|native-health-e2e|radar-parity|release-review) ;; *) printf 'Unknown mode: %s\n' "$MODE" >&2; exit 1 ;; esac
if [ "$MODE" = native-e2e ] || [ "$MODE" = release-review ] || [ "$MODE" = native-terminal-e2e ] || [ "$MODE" = native-window-e2e ] || [ "$MODE" = native-forward-e2e ]; then
    command -v go >/dev/null 2>&1 || { printf 'Missing tool: go\n' >&2; exit 1; }
    if [ "$MODE" != native-forward-e2e ]; then
        [ -x "$BUILD/window-host-test" ] || { printf 'Required native multiwindow test executable is missing.\n' >&2; exit 1; }
    fi
fi
DESKTOP_TIMEOUT=${PODLORD_DESKTOP_TIMEOUT_SECONDS:-3600}
case "$DESKTOP_TIMEOUT" in ''|*[!0-9]*|0*) printf 'Desktop timeout must be a whole number from 60 to 21600 seconds.\n' >&2; exit 1 ;; esac
[ "${#DESKTOP_TIMEOUT}" -le 5 ] && [ "$DESKTOP_TIMEOUT" -ge 60 ] && [ "$DESKTOP_TIMEOUT" -le 21600 ] || { printf 'Desktop timeout must be a whole number from 60 to 21600 seconds.\n' >&2; exit 1; }
LEGACY=${PODLORD_LEGACY_APP:-}
if [ "$MODE" != desktop ] && [ "$MODE" != radar-parity ] && [ "$MODE" != native-terminal-e2e ] && [ "$MODE" != native-window-e2e ] && [ "$MODE" != native-forward-e2e ] && [ "$MODE" != native-search-e2e ] && [ "$MODE" != native-health-e2e ] && [ ! -x "$BUILD/metric-filter-ui-test" ]; then
    printf 'Required native field filter UI test executable is missing.\n' >&2
    exit 1
fi
if [ "$MODE" = native-terminal-e2e ] || [ "$MODE" = native-e2e ] || [ "$MODE" = release-review ]; then
    [ -x "$BUILD/container-terminal-ui-test" ] || { printf 'Required native terminal UI test executable is missing.\n' >&2; exit 1; }
fi
if [ "$MODE" = native-health-e2e ]; then
    [ -x "$BUILD/alert_ui_test" ] || { printf 'Required native health UI test executable is missing.\n' >&2; exit 1; }
fi
if [ "$MODE" = native-forward-e2e ]; then
    [ -x "$BUILD/port-forward-ui-test" ] || { printf 'Required native port-forward UI test executable is missing.\n' >&2; exit 1; }
fi
if [ "$MODE" = native-search-e2e ]; then
    [ -x "$BUILD/workspace_ui_test" ] || { printf 'Required native discovery/search UI test executable is missing.\n' >&2; exit 1; }
fi
if [ "$MODE" = radar-parity ]; then
    [ -x "$BUILD/radar_reference_test" ] && [ -f "${PODLORD_RADAR_REFERENCE_DLL:-}" ] && [ -x "${PODLORD_DOTNET:-}" ] || { printf 'Built native and C# radar reference tools are required.\n' >&2; exit 1; }
fi
if [ "$MODE" = desktop ] || [ "$MODE" = release-review ]; then
    [ -x "$NATIVE" ] && [ -n "$LEGACY" ] && [ -x "$LEGACY" ] || { printf 'Both application builds are required.\n' >&2; exit 1; }
    if [ "$(uname -s)" = Darwin ]; then
        [ -d "${NATIVE%/MacOS/*}/Frameworks/QtCore.framework" ] || { printf 'Desktop evidence requires a deployed native app; an unbundled build does not prove its runtime dependencies.\n' >&2; exit 1; }
        case "$LEGACY" in *.app/Contents/MacOS/*) ;; *) printf 'Desktop evidence requires the packaged reference app for isolated application identity.\n' >&2; exit 1 ;; esac
    fi
fi
if [ "$MODE" = native-e2e ] || [ "$MODE" = release-review ]; then
    for executable in "$BUILD/alert_ui_test" "$BUILD/workspace_ui_test" "$BUILD/resource-delete-ui-test" "$BUILD/inspector-navigation-ui-test" "$BUILD/port-forward-ui-test"; do
        [ -x "$executable" ] || { printf 'Required native UI test executable is missing: %s\n' "$executable" >&2; exit 1; }
    done
fi
if [ -n "${DOCKER_CONTEXT:-}" ]; then
    endpoint=$(docker context inspect "$DOCKER_CONTEXT" --format '{{.Endpoints.docker.Host}}')
elif [ -n "${DOCKER_HOST:-}" ]; then
    endpoint=$DOCKER_HOST
else
    endpoint=$(docker context inspect "$(docker context show)" --format '{{.Endpoints.docker.Host}}')
fi
case "$endpoint" in unix://*|npipe://*) ;; *) printf 'Only a local Docker daemon is permitted.\n' >&2; exit 1 ;; esac
export DOCKER_HOST="$endpoint"
unset DOCKER_CONTEXT
mkdir -p "$BUILD/e2e"
RUN=$(mktemp -d "$BUILD/e2e/visual-run.XXXXXX")
LLVM_PROFILE_FILE="$RUN/%p-%m.profraw"
export LLVM_PROFILE_FILE
NAME="podlord-$(basename "$RUN" | tr '[:upper:].' '[:lower:]-')"
EVIDENCE=${PODLORD_VISUAL_EVIDENCE_DIR:-$BUILD/e2e/visual-evidence}
mkdir -p "$EVIDENCE"
CREATED=0
NATIVE_PID=
LEGACY_PID=
cleanup() {
    status=$?
    trap - 0
    for pid in "$NATIVE_PID" "$LEGACY_PID"; do
        if [ -n "$pid" ]; then kill "$pid" 2>/dev/null || :; wait "$pid" 2>/dev/null || :; fi
    done
    if [ "$CREATED" = 1 ]; then
        owner=$(docker inspect --type container "$NAME" --format '{{index .Config.Labels "podlord.native.e2e"}}' 2>/dev/null) || owner=
        if [ "$owner" != "$NAME" ]; then
            printf 'Cleanup refused: container ownership could not be established for %s. No logs or resources were touched.\n' "$NAME" >&2
            status=1
        else
            if [ "$status" != 0 ]; then
                docker inspect "$NAME" --format '{{json .State}}' > "$EVIDENCE/$NAME-state.json" 2>/dev/null || :
                docker logs --tail 80 "$NAME" > "$EVIDENCE/$NAME-failure.log" 2>&1 || :
                tail -n 20 "$EVIDENCE/$NAME-failure.log" >&2 || :
            fi
            volumes=$(docker inspect "$NAME" --format '{{range .Mounts}}{{if eq .Type "volume"}}{{.Name}} {{end}}{{end}}') || status=1
            docker rm -fv "$NAME" || status=1
            remaining=$(docker container ls -a --filter "label=podlord.native.e2e=$NAME" --format '{{.ID}}') || status=1
            [ -z "${remaining:-}" ] || { printf 'Owned containers remain: %s\n' "$remaining" >&2; status=1; }
            for volume in ${volumes:-}; do
                remaining=$(docker volume ls --filter "name=^$volume$" --format '{{.Name}}') || status=1
                [ -z "${remaining:-}" ] || { printf 'Owned volume remains: %s\n' "$volume" >&2; status=1; }
            done
        fi
    fi
    for profile in "$RUN"/*.profraw; do
        [ -f "$profile" ] && [ ! -L "$profile" ] || continue
        if ! mkdir -p "$EVIDENCE/$NAME-profiles" || ! cp "$profile" "$EVIDENCE/$NAME-profiles/"; then
            printf 'Failed to preserve owned coverage evidence before cleanup.\n' >&2
            status=1
        fi
    done
    find "$RUN" -depth -delete || status=1
    exit "$status"
}
trap cleanup 0
trap 'exit 129' HUP
trap 'exit 130' INT
trap 'exit 143' TERM
mkdir -p "$RUN/tmp"
TMPDIR="$RUN/tmp"
export TMPDIR
run_native_test() {
    "$@" &
    NATIVE_PID=$!
    result=0
    wait "$NATIVE_PID" || result=$?
    NATIVE_PID=
    return "$result"
}
run_cached_field_test() {
    printf 'Native Kubernetes UI scenario: real cached field filters\n'
    cp "$BUILD/metric-filter-ui-test" "$RUN/metric-filter-ui-test"
    shasum -a 256 "$RUN/metric-filter-ui-test" > "$EVIDENCE/$NAME-metric-filter-binary.sha256"
    PODLORD_REAL_FIELD_FILTER_EVIDENCE="$EVIDENCE/$NAME-field-filters" \
        QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software QT_QUICK_CONTROLS_STYLE=Basic \
        run_native_test "$RUN/metric-filter-ui-test" real "$RUN/kubeconfig" > "$EVIDENCE/$NAME-field-filters.log" 2>&1
}
run_terminal_tests() {
    cp "$BUILD/container-terminal-ui-test" "$RUN/container-terminal-ui-test"
    shasum -a 256 "$RUN/container-terminal-ui-test" > "$EVIDENCE/$NAME-terminal-binary.sha256"
    for terminal_scenario in real_shell real_vi real_control_vi real_interrupt real_touch_interrupt; do
        printf 'Native Kubernetes interactive terminal: %s\n' "$terminal_scenario"
        PODLORD_TERMINAL_FRAME="$EVIDENCE/$NAME-terminal-$terminal_scenario.png" \
        QT_QPA_PLATFORM=${PODLORD_TERMINAL_QPA_PLATFORM:-offscreen} QT_QUICK_BACKEND=${PODLORD_TERMINAL_QUICK_BACKEND-software} QT_QUICK_CONTROLS_STYLE=Basic \
            run_native_test "$RUN/container-terminal-ui-test" "$terminal_scenario" "$RUN/kubeconfig" > "$EVIDENCE/$NAME-terminal-$terminal_scenario.log" 2>&1
    done
}
if [ "$MODE" = native-e2e ] || [ "$MODE" = release-review ] || [ "$MODE" = native-forward-e2e ]; then
    mkdir "$RUN/bin"
    executables='alert_ui_test workspace_ui_test resource-delete-ui-test inspector-navigation-ui-test port-forward-ui-test'
    if [ "$MODE" = native-forward-e2e ]; then executables=port-forward-ui-test; fi
    for executable in $executables; do
        cp "$BUILD/$executable" "$RUN/bin/$executable"
    done
    node - "$RUN/bin" > "$EVIDENCE/$NAME-test-builds.json" <<'JS'
const fs=require('node:fs'),path=require('node:path'),crypto=require('node:crypto');
const directory=process.argv[2];
const executables=fs.readdirSync(directory).sort().map(name=>{
  const bytes=fs.readFileSync(path.join(directory,name));
  return {name,bytes:bytes.length,sha256:crypto.createHash('sha256').update(bytes).digest('hex')};
});
process.stdout.write(JSON.stringify({boundary:'Native test executables with real product runtime and rendered QML, not an installed release',executables},null,2)+'\n');
JS
fi
existing=$(docker container ls -a --filter "name=^/$NAME$" --format '{{.ID}}')
[ -z "$existing" ] || { printf 'Existing cluster identity is not owned by this run.\n' >&2; exit 1; }
CREATED=1
docker create --name "$NAME" --label "podlord.native.e2e=$NAME" --privileged \
    --tmpfs /run --tmpfs /var/run -p 127.0.0.1::6443 rancher/k3s:v1.35.5-k3s1 \
    server --disable=traefik --disable=servicelb --write-kubeconfig-mode=600 \
    --kubelet-arg='eviction-hard=memory.available<100Mi,nodefs.available<512Mi,imagefs.available<512Mi,nodefs.inodesFree<5%,imagefs.inodesFree<5%'
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
    [ "$attempt" -lt 60 ] || { docker logs --tail 50 "$NAME" >&2; exit 1; }
    sleep 2
done
attempt=0
while [ -z "$(kubectl --kubeconfig "$RUN/kubeconfig" get nodes -o name)" ]; do
    attempt=$((attempt + 1))
    [ "$attempt" -lt 60 ] || { printf 'The local node did not register.\n' >&2; exit 1; }
    sleep 2
done
kubectl --kubeconfig "$RUN/kubeconfig" wait --for=condition=Ready node --all --timeout=120s
cat > "$RUN/radar-crd.yaml" <<'YAML'
apiVersion: apiextensions.k8s.io/v1
kind: CustomResourceDefinition
metadata:
  name: radarprobes.podlord.test
spec:
  group: podlord.test
  scope: Namespaced
  names:
    plural: radarprobes
    singular: radarprobe
    kind: RadarProbe
  versions:
    - name: v1
      served: true
      storage: true
      schema:
        openAPIV3Schema:
          type: object
          properties:
            spec:
              type: object
              properties:
                message:
                  type: string
YAML
kubectl --kubeconfig "$RUN/kubeconfig" create -f "$RUN/radar-crd.yaml" > "$RUN/crd-create.log"
kubectl --kubeconfig "$RUN/kubeconfig" wait --for=condition=Established --timeout=60s crd/radarprobes.podlord.test > "$RUN/crd-ready.log"
node - "$NAME" > "$RUN/resources.json" <<'JS'
const owner=process.argv[2], items=[];
const add=(apiVersion,kind,name,namespace,body={})=>items.push({apiVersion,kind,metadata:{name,...(namespace?{namespace}:{}),labels:{'podlord.visual.owner':owner}},...body});
const image='rancher/k3s:v1.35.5-k3s1';
const container={name:'worker',image,imagePullPolicy:'Never',command:['/bin/sh','-c','echo visual-worker-ready; sleep 3600'],resources:{requests:{cpu:'5m',memory:'8Mi'},limits:{cpu:'50m',memory:'64Mi'}}};
for(const letter of 'abcdefgh') {
  const ns=`visual-${letter}`;
  add('v1','Namespace',ns);
  for(let n=1;n<=64;n++) add('v1','ConfigMap',`visual-config-${String(n).padStart(4,'0')}`,ns,{data:{mode:letter,description:`Real local visual test ${n}`,longValue:'large-content-'.repeat(128),configuration:Array.from({length:30},(_,i)=>`setting-${i}=value-${n}-${i}`).join('\n')}});
  for(let n=1;n<=32;n++) add('v1','Secret',`visual-secret-${String(n).padStart(4,'0')}`,ns,{type:'Opaque',stringData:{token:`local-visual-token-${letter}-${n}`,username:`visual-user-${n}`}});
  for(let n=1;n<=8;n++) {
    const name=`visual-service-${n}`;
    add('v1','Service',name,ns,{spec:{selector:{app:`visual-worker-${n}`},ports:[{name:'http',port:8080,targetPort:8080}]}});
    add('apps/v1','Deployment',`visual-worker-${n}`,ns,{spec:{replicas:letter==='a'&&n===1?2:0,selector:{matchLabels:{app:`visual-worker-${n}`}},template:{metadata:{labels:{app:`visual-worker-${n}`}},spec:{automountServiceAccountToken:false,containers:[container]}}}});
  }
  for(let n=1;n<=4;n++) {
    add('apps/v1','StatefulSet',`visual-state-${n}`,ns,{spec:{replicas:0,serviceName:'visual-service-1',selector:{matchLabels:{app:`visual-state-${n}`}},template:{metadata:{labels:{app:`visual-state-${n}`}},spec:{automountServiceAccountToken:false,containers:[container]}}}});
    add('apps/v1','DaemonSet',`visual-daemon-${n}`,ns,{spec:{selector:{matchLabels:{app:`visual-daemon-${n}`}},template:{metadata:{labels:{app:`visual-daemon-${n}`}},spec:{nodeSelector:{'podlord.visual.unscheduled':'true'},automountServiceAccountToken:false,containers:[container]}}}});
    add('batch/v1','CronJob',`visual-scheduled-${n}`,ns,{spec:{schedule:'0 0 * * *',suspend:true,jobTemplate:{spec:{template:{spec:{automountServiceAccountToken:false,restartPolicy:'Never',containers:[container]}}}}}});
    add('v1','PersistentVolumeClaim',`visual-pending-storage-${n}`,ns,{spec:{accessModes:['ReadWriteOnce'],storageClassName:'visual-unavailable',resources:{requests:{storage:'1Gi'}}}});
  }
  for(let n=1;n<=2;n++) add('v1','ServiceAccount',`visual-account-${n}`,ns,{automountServiceAccountToken:false});
  add('batch/v1','Job','visual-suspended-job',ns,{spec:{suspend:true,template:{spec:{automountServiceAccountToken:false,restartPolicy:'Never',containers:[container]}}}});
}
add('v1','Pod','visual-multi-container','visual-a',{spec:{serviceAccountName:'visual-account-1',automountServiceAccountToken:false,containers:[{...container,name:'alpha',command:['/bin/sh','-c','while :; do echo visual-alpha-log; sleep 5; done']},{...container,name:'beta',command:['/bin/sh','-c','while :; do echo visual-beta-log; sleep 5; done']}]}});
for (const ns of ['visual-a','visual-b','visual-c','visual-d','visual-e','visual-f','visual-g','visual-h'])
    add('podlord.test/v1','RadarProbe','visual-radar-probe',ns,{spec:{message:'Actual custom-resource discovery and cached Radar projection'}});
add('v1','Pod','visual-image-error','visual-a',{spec:{serviceAccountName:'visual-account-1',automountServiceAccountToken:false,containers:[{name:'missing',image:'podlord-visual-absent:local',imagePullPolicy:'Never'}]}});
add('v1','Pod','visual-crash-loop','visual-b',{spec:{serviceAccountName:'visual-account-1',automountServiceAccountToken:false,containers:[{...container,command:['/bin/sh','-c','echo visual-expected-failure; exit 7']}]}});
process.stdout.write(JSON.stringify({apiVersion:'v1',kind:'List',items}));
JS
if ! kubectl --kubeconfig "$RUN/kubeconfig" create -f "$RUN/resources.json" > "$RUN/create.log" 2> "$RUN/create-errors.log"; then
    tail -n 8 "$RUN/create-errors.log" >&2
    exit 1
fi
if ! kubectl --kubeconfig "$RUN/kubeconfig" wait -n visual-a --for=condition=Ready pod/visual-multi-container --timeout=120s; then
    kubectl --kubeconfig "$RUN/kubeconfig" describe pod -n visual-a visual-multi-container >&2
    kubectl --kubeconfig "$RUN/kubeconfig" get nodes -o wide >&2
    docker logs --tail 30 "$NAME" >&2
    exit 1
fi
kubectl --kubeconfig "$RUN/kubeconfig" rollout status -n visual-a deployment/visual-worker-1 --timeout=120s
kubectl --kubeconfig "$RUN/kubeconfig" get configmap,secret,deployment,statefulset,daemonset,cronjob,pvc,service,serviceaccount,job,pod -A -l "podlord.visual.owner=$NAME" -o json | node -e '
let input="";process.stdin.on("data",b=>input+=b);process.stdin.on("end",()=>{const items=JSON.parse(input).items,counts={};for(const item of items)counts[item.kind]=(counts[item.kind]||0)+1;if(counts.ConfigMap!==512||counts.Secret!==256||counts.Deployment!==64)throw Error("Resource count mismatch");console.log(JSON.stringify({boundary:"real local Kubernetes API",total:items.length,counts},null,2));});'
if [ "$MODE" = radar-parity ]; then
    mkdir -p "$RUN/reference-config"
    PODLORD_CONFIG_HOME="$RUN/reference-config" PODLORD_DISABLE_AUDIO=1 PODLORD_DISABLE_UPDATE_CHECK=1 \
        "$PODLORD_DOTNET" "$PODLORD_RADAR_REFERENCE_DLL" "$RUN/kubeconfig" "$EVIDENCE"
    if [ -n "${PODLORD_RADAR_BEFORE_BINARY:-}" ]; then
        before_status=0
        QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software "$PODLORD_RADAR_BEFORE_BINARY" reference "$EVIDENCE/reference.json" > "$EVIDENCE/before.log" 2>&1 || before_status=$?
        [ "$before_status" = 1 ] && rg -q '^Radar mismatch ' "$EVIDENCE/before.log" || { printf 'Expected an explicit original-geometry mismatch, not a crash or setup failure.\n' >&2; exit 1; }
    fi
    for scenario in reference reordered filtered namespaces pose; do
        QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software QT_QUICK_CONTROLS_STYLE=Basic \
            run_native_test "$BUILD/radar_reference_test" "$scenario" "$EVIDENCE/reference.json"
    done
    QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software QT_QUICK_CONTROLS_STYLE=Basic \
        run_native_test "$BUILD/radar_reference_test" render "$EVIDENCE/reference.json" "$EVIDENCE/cpp-radar.png"
    printf 'Real-cache radar reference comparison passed; cleaning up owned cluster.\n'
    exit 0
fi
if [ "$MODE" = native-fields-e2e ]; then
    run_cached_field_test
    printf 'Native Kubernetes cached field scenario passed; cleaning up owned cluster and profiles.\n'
    exit 0
fi
if [ "$MODE" = native-health-e2e ]; then
    cp "$BUILD/alert_ui_test" "$RUN/alert_ui_test"
    shasum -a 256 "$RUN/alert_ui_test" > "$EVIDENCE/$NAME-health-binary.sha256"
    PODLORD_REAL_RADAR_HIGHLIGHTS="$EVIDENCE/$NAME-radar-health" QT_QPA_PLATFORM=${QT_QPA_PLATFORM:-offscreen} QT_QUICK_BACKEND=${QT_QUICK_BACKEND:-software} QT_QUICK_CONTROLS_STYLE=${QT_QUICK_CONTROLS_STYLE:-Basic} \
        run_native_test "$RUN/alert_ui_test" real_health "$RUN/kubeconfig" "$EVIDENCE/$NAME-health.png" > "$EVIDENCE/$NAME-real_health.log" 2>&1
    printf 'Native Kubernetes health and Radar scenario passed; cleaning up owned cluster and profiles.\n'
    exit 0
fi
if [ "$MODE" = native-search-e2e ]; then
    cp "$BUILD/workspace_ui_test" "$RUN/workspace_ui_test"
    shasum -a 256 "$RUN/workspace_ui_test" > "$EVIDENCE/$NAME-search-binary.sha256"
    PODLORD_E2E_SCREENSHOT="$EVIDENCE/$NAME-search-radar.png" QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software QT_QUICK_CONTROLS_STYLE=Basic \
        run_native_test "$RUN/workspace_ui_test" real_search "$RUN/kubeconfig" > "$EVIDENCE/$NAME-real_search.log" 2>&1
    printf 'Native Kubernetes discovery/search and Radar scenarios passed; cleaning up owned cluster and profiles.\n'
    exit 0
fi
if [ "$MODE" = native-e2e ] || [ "$MODE" = release-review ] || [ "$MODE" = native-terminal-e2e ] || [ "$MODE" = native-window-e2e ] || [ "$MODE" = native-forward-e2e ]; then
    if [ "$MODE" != native-window-e2e ] && [ "$MODE" != native-forward-e2e ]; then run_terminal_tests; fi
    if [ "$MODE" = native-e2e ] || [ "$MODE" = release-review ]; then
        kubectl --kubeconfig "$RUN/kubeconfig" create configmap podlord-delete-e2e -n visual-a --from-literal="owner=$NAME"
        kubectl --kubeconfig "$RUN/kubeconfig" create configmap podlord-delete-race-e2e -n visual-a --from-literal="owner=$NAME"
    fi
    cat > "$RUN/echo.go" <<'GO'
package main
import ("io"; "log"; "net/http")
func main() {
    http.HandleFunc("/", func(w http.ResponseWriter, r *http.Request) { _, _ = io.WriteString(w, "podlord-native-forward") })
    log.Fatal(http.ListenAndServe(":8080", nil))
}
GO
    architecture=$(docker image inspect rancher/k3s:v1.35.5-k3s1 --format '{{.Architecture}}')
    case "$architecture" in arm64|amd64) ;; *) printf 'Unsupported local echo-server architecture: %s\n' "$architecture" >&2; exit 1 ;; esac
    GOOS=linux GOARCH="$architecture" CGO_ENABLED=0 go build -trimpath -ldflags='-s -w' -o "$RUN/echo-http" "$RUN/echo.go"
    chmod 755 "$RUN/echo-http"
    docker exec "$NAME" mkdir -p /podlord-test-forward
    docker cp "$RUN/echo-http" "$NAME:/podlord-test-forward/server"
    cat > "$RUN/forward.yaml" <<YAML
apiVersion: v1
kind: Pod
metadata:
  name: podlord-forward-echo
  namespace: visual-a
  labels: {app: podlord-forward-echo, podlord.visual.owner: "$NAME"}
spec:
  automountServiceAccountToken: false
  securityContext: {runAsNonRoot: true, runAsUser: 65534}
  containers:
  - name: echo
    image: rancher/k3s:v1.35.5-k3s1
    imagePullPolicy: Never
    command: [/podlord-test-forward/server]
    ports: [{name: http, containerPort: 8080}]
    securityContext: {allowPrivilegeEscalation: false, capabilities: {drop: [ALL]}}
    volumeMounts: [{name: echo, mountPath: /podlord-test-forward/server, readOnly: true}]
  volumes:
  - name: echo
    hostPath: {path: /podlord-test-forward/server, type: File}
---
apiVersion: v1
kind: Service
metadata:
  name: podlord-forward-echo
  namespace: visual-a
  labels: {podlord.visual.owner: "$NAME"}
spec:
  selector: {app: podlord-forward-echo}
  ports: [{name: http, port: 80, targetPort: http}]
YAML
    kubectl --kubeconfig "$RUN/kubeconfig" create -f "$RUN/forward.yaml"
    if ! kubectl --kubeconfig "$RUN/kubeconfig" wait -n visual-a --for=condition=Ready pod/podlord-forward-echo --timeout=120s; then
        kubectl --kubeconfig "$RUN/kubeconfig" get pod -n visual-a podlord-forward-echo -o json > "$EVIDENCE/$NAME-forward-setup.json" 2>&1 || :
        kubectl --kubeconfig "$RUN/kubeconfig" describe pod -n visual-a podlord-forward-echo > "$EVIDENCE/$NAME-forward-setup.txt" 2>&1 || :
        kubectl --kubeconfig "$RUN/kubeconfig" logs -n visual-a podlord-forward-echo --all-containers > "$EVIDENCE/$NAME-forward-setup.log" 2>&1 || :
        tail -n 15 "$EVIDENCE/$NAME-forward-setup.txt" >&2
        exit 1
    fi
    if [ "$MODE" != native-forward-e2e ]; then
        cp "$BUILD/window-host-test" "$RUN/window-host-test"
        shasum -a 256 "$RUN/window-host-test" > "$EVIDENCE/$NAME-window-binary.sha256"
        printf 'Native Kubernetes window transfer: terminal and isolated forwards\n'
        PODLORD_WINDOW_EVIDENCE="$EVIDENCE/$NAME-windows" \
            QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software QT_QUICK_CONTROLS_STYLE=Fusion \
            run_native_test "$RUN/window-host-test" real_transfer "$RUN/kubeconfig" > "$EVIDENCE/$NAME-windows.log" 2>&1
    fi
    if [ "$MODE" = native-terminal-e2e ] || [ "$MODE" = native-window-e2e ]; then
        printf 'Native Kubernetes window transfer and selected terminal scenarios passed; cleaning up owned cluster and profiles.\n'
        exit 0
    fi
    for scenario in real_pod real_service real_context_removal real_context_draft; do
        printf 'Native Kubernetes UI port-forward scenario: %s\n' "$scenario"
        PODLORD_FORWARD_FRAME="$EVIDENCE/$NAME-forward-$scenario.png" \
        PODLORD_SOURCE_REMOVAL_FRAME="$EVIDENCE/$NAME-source-removal-$scenario.png" \
        QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software QT_QUICK_CONTROLS_STYLE=Basic \
            run_native_test "$RUN/bin/port-forward-ui-test" "$scenario" "$RUN/kubeconfig" > "$EVIDENCE/$NAME-forward-$scenario.log" 2>&1
    done
    if [ "$MODE" = native-forward-e2e ]; then
        printf 'Native Kubernetes port-forward UI scenarios passed; cleaning up owned cluster and profiles.\n'
        exit 0
    fi
    for scenario in real real_race; do
        printf 'Native Kubernetes UI deletion scenario: %s\n' "$scenario"
        PODLORD_DELETE_FRAME="$EVIDENCE/$NAME-delete-$scenario-confirmation.png" QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software QT_QUICK_CONTROLS_STYLE=Basic run_native_test "$RUN/bin/resource-delete-ui-test" "$scenario" "$RUN/kubeconfig" > "$EVIDENCE/$NAME-delete-$scenario.log" 2>&1
    done
    printf 'Native Kubernetes UI scenario: inspector_history\n'
    PODLORD_HISTORY_FRAME="$EVIDENCE/$NAME-inspector-history.png" QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software QT_QUICK_CONTROLS_STYLE=Basic run_native_test "$RUN/bin/inspector-navigation-ui-test" real "$RUN/kubeconfig" > "$EVIDENCE/$NAME-inspector-history.log" 2>&1
    printf 'Native Kubernetes UI scenario: real_search\n'
    PODLORD_E2E_SCREENSHOT="$EVIDENCE/$NAME-search-radar.png" QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software QT_QUICK_CONTROLS_STYLE=Basic run_native_test "$RUN/bin/workspace_ui_test" real_search "$RUN/kubeconfig" > "$EVIDENCE/$NAME-real_search.log" 2>&1
    for scenario in real real_metrics real_health; do
        printf 'Native Kubernetes UI scenario: %s\n' "$scenario"
        if [ "$scenario" = real_health ]; then
            PODLORD_REAL_RADAR_HIGHLIGHTS="$EVIDENCE/$NAME-radar-health" QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software QT_QUICK_CONTROLS_STYLE=Basic run_native_test "$RUN/bin/alert_ui_test" "$scenario" "$RUN/kubeconfig" "$EVIDENCE/$NAME-health.png" > "$EVIDENCE/$NAME-$scenario.log" 2>&1
        else
            PODLORD_RADAR_METRIC_FRAME="$EVIDENCE/$NAME-radar-metrics.png" PODLORD_RADAR_THEME_FRAMES="$EVIDENCE/$NAME-radar-themes" QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software QT_QUICK_CONTROLS_STYLE=Basic run_native_test "$RUN/bin/alert_ui_test" "$scenario" "$RUN/kubeconfig" > "$EVIDENCE/$NAME-$scenario.log" 2>&1
        fi
    done
    run_cached_field_test
    printf 'Native Kubernetes UI scenarios passed.\n'
    if [ "$MODE" = native-e2e ]; then exit 0; fi
fi
mkdir -p "$RUN/legacy/home/.kube" "$RUN/legacy/config/podlord"
cp "$RUN/kubeconfig" "$RUN/legacy/home/.kube/config"
if [ "$(uname -s)" = Darwin ]; then
    # A unique test identity prevents desktop controls from selecting a user's running app.
    cp -R "${LEGACY%/Contents/MacOS/*}" "$RUN/PodlordReference.app"
    /usr/bin/plutil -replace CFBundleIdentifier -string "dev.podlord.reference.$NAME" "$RUN/PodlordReference.app/Contents/Info.plist"
    /usr/bin/plutil -replace CFBundleName -string 'Podlord Reference' "$RUN/PodlordReference.app/Contents/Info.plist"
    /usr/bin/plutil -replace CFBundleDisplayName -string 'Podlord Reference' "$RUN/PodlordReference.app/Contents/Info.plist"
    /usr/bin/codesign --force --deep --sign - "$RUN/PodlordReference.app"
    LEGACY="$RUN/PodlordReference.app/Contents/MacOS/$(basename -- "$LEGACY")"
fi
(unset QT_QPA_PLATFORM QT_QUICK_BACKEND QT_QUICK_CONTROLS_STYLE QT_PLUGIN_PATH QT_QPA_PLATFORM_PLUGIN_PATH QML_IMPORT_PATH QML2_IMPORT_PATH DYLD_LIBRARY_PATH DYLD_FALLBACK_LIBRARY_PATH DYLD_FRAMEWORK_PATH KUBECONFIG; exec "$NATIVE" --profile "$RUN/native" --kubeconfig "$RUN/kubeconfig") > "$RUN/native.log" 2>&1 &
NATIVE_PID=$!
(export HOME="$RUN/legacy/home" PODLORD_HOME="$RUN/legacy/home" PODLORD_CONFIG_HOME="$RUN/legacy/config/podlord" PODLORD_DISABLE_UPDATE_CHECK=1 PODLORD_DISABLE_AUDIO=1 KUBECONFIG="$RUN/kubeconfig"; exec "$LEGACY") > "$RUN/legacy.log" 2>&1 &
LEGACY_PID=$!
printf 'VISUAL_RUN=%s\nKUBECONFIG_FILE=%s\nCLUSTER=%s\nNATIVE_PID=%s\nLEGACY_PID=%s\n' "$RUN" "$RUN/kubeconfig" "$NAME" "$NATIVE_PID" "$LEGACY_PID"
printf 'REFERENCE_APP=%s\n' "${LEGACY%/Contents/MacOS/*}"
printf 'Capture and assert public desktop behavior, then create %s/complete.\n' "$RUN"
attempt=0
while [ ! -f "$RUN/complete" ]; do
    attempt=$((attempt + 1))
    [ "$attempt" -lt "$((DESKTOP_TIMEOUT / 2))" ] || { printf 'Desktop review exceeded %s seconds; cleaning up.\n' "$DESKTOP_TIMEOUT" >&2; exit 1; }
    sleep 2
done
printf 'Desktop comparison completed; cleaning up owned apps, cluster and profiles.\n'
