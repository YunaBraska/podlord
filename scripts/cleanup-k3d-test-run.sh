#!/bin/sh
# Ownership comes from runtime labels, never a shared name prefix.
set -eu
[ "$#" -ge 1 ] && [ "$#" -le 3 ] || { printf 'Expected a test-run owner, optional cluster and recorded tools container ID.\n' >&2; exit 2; }
OWNER=$1
case "$OWNER" in ''|*[!a-zA-Z0-9_-]*) printf 'Invalid test-run owner.\n' >&2; exit 2 ;; esac
CLUSTER=${2:-}
case "$CLUSTER" in ''|podlord-it-*) ;; *) printf 'Invalid test-cluster identity.\n' >&2; exit 2 ;; esac
case "$CLUSTER" in *[!a-zA-Z0-9_-]*) printf 'Invalid test-cluster identity.\n' >&2; exit 2 ;; esac
TOOLS=${3:-}
if [ -n "$TOOLS" ]; then
    [ -n "$CLUSTER" ] && [ "${#TOOLS}" -eq 64 ] || { printf 'Invalid recorded tools container ID.\n' >&2; exit 2; }
    case "$TOOLS" in *[!a-f0-9]*) printf 'Invalid recorded tools container ID.\n' >&2; exit 2 ;; esac
fi
for tool in docker k3d; do command -v "$tool" >/dev/null 2>&1 || { printf 'Missing cleanup tool: %s\n' "$tool" >&2; exit 1; }; done
if [ -n "${DOCKER_CONTEXT:-}" ]; then
    endpoint=$(docker context inspect "$DOCKER_CONTEXT" --format '{{.Endpoints.docker.Host}}')
elif [ -n "${DOCKER_HOST:-}" ]; then endpoint=$DOCKER_HOST
else endpoint=$(docker context inspect "$(docker context show)" --format '{{.Endpoints.docker.Host}}'); fi
case "$endpoint" in unix://*|npipe://*) ;; *) printf 'Only a local Docker daemon is permitted.\n' >&2; exit 1 ;; esac
export DOCKER_HOST="$endpoint"
unset DOCKER_CONTEXT
if [ -n "$CLUSTER" ]; then
    owned=$(docker container ls -aq --filter "label=podlord.test.run=$OWNER" --filter "label=k3d.cluster=$CLUSTER")
else owned=$(docker container ls -aq --filter "label=podlord.test.run=$OWNER"); fi
clusters=
for id in $owned; do
    cluster=$(docker inspect --type container "$id" --format '{{index .Config.Labels "k3d.cluster"}}')
    case "$cluster" in podlord-it-*) ;; *) printf 'Cleanup refused: owned container has an unsupported cluster identity.\n' >&2; exit 1 ;; esac
    case "$cluster" in *[!a-zA-Z0-9_-]*) printf 'Cleanup refused: invalid cluster identity.\n' >&2; exit 1 ;; esac
    clusters="$clusters $cluster"
done
PRIVATE=$(mktemp -d "${TMPDIR:-/tmp}/podlord-k3d-cleanup.XXXXXX")
trap 'find "$PRIVATE" -depth -delete' 0
export KUBECONFIG="$PRIVATE/kubeconfig"
for cluster in $(printf '%s\n' $clusters | sort -u); do
    nodes=$(docker container ls -aq --filter "label=k3d.cluster=$cluster")
    volumes=
    for id in $nodes; do
        owner=$(docker inspect --type container "$id" --format '{{index .Config.Labels "podlord.test.run"}}')
        if [ "$owner" != "$OWNER" ]; then
            recorded=
            if [ -z "$owner" ] && [ -n "$TOOLS" ]; then
                recorded=$(docker inspect --type container "$id" --format '{{.Id}}|{{.Name}}|{{index .Config.Labels "app"}}|{{index .Config.Labels "k3d.role"}}|{{index .Config.Labels "k3d.cluster"}}')
            fi
            [ "$recorded" = "$TOOLS|/k3d-$cluster-tools|k3d|noRole|$cluster" ] || { printf 'Cleanup refused: cluster %s contains an unowned container.\n' "$cluster" >&2; exit 1; }
        fi
        mounted=$(docker inspect --type container "$id" --format '{{range .Mounts}}{{if eq .Type "volume"}}{{.Name}} {{end}}{{end}}')
        volumes="$volumes $mounted"
    done
    k3d cluster delete "$cluster"
    remaining=$(docker container ls -aq --filter "label=k3d.cluster=$cluster")
    [ -z "$remaining" ] || { printf 'Owned cluster containers remain: %s\n' "$cluster" >&2; exit 1; }
    for volume in $volumes; do
        case "$volume" in *[!a-zA-Z0-9_-]*) printf 'Cannot verify an owned volume with an unexpected name.\n' >&2; exit 1 ;; esac
        remaining=$(docker volume ls --filter "name=^$volume$" --format '{{.Name}}')
        [ -z "$remaining" ] || { printf 'Owned cluster volume remains: %s\n' "$volume" >&2; exit 1; }
    done
done
