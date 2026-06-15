#!/bin/sh
set -eu

ROOT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
TOOL_DIR="$ROOT_DIR/.tools/bin"
K3D_VERSION=${K3D_VERSION:-v5.9.0}
KUBECTL_VERSION=${KUBECTL_VERSION:-v1.36.2}

mkdir -p "$TOOL_DIR"
PATH="$TOOL_DIR:$PATH"
export PATH

platform() {
  case "$(uname -s)" in
    Linux) printf 'linux' ;;
    Darwin) printf 'darwin' ;;
    *) echo "Unsupported operating system for automatic k3d test bootstrap: $(uname -s)" >&2; exit 1 ;;
  esac
}

architecture() {
  case "$(uname -m)" in
    x86_64|amd64) printf 'amd64' ;;
    arm64|aarch64) printf 'arm64' ;;
    *) echo "Unsupported architecture for automatic k3d test bootstrap: $(uname -m)" >&2; exit 1 ;;
  esac
}

calculate_sha256() {
  if command -v sha256sum >/dev/null 2>&1; then
    sha256sum "$1" | awk '{ print $1 }'
  elif command -v shasum >/dev/null 2>&1; then
    shasum -a 256 "$1" | awk '{ print $1 }'
  else
    echo "sha256sum or shasum is required to verify downloaded test tools" >&2
    exit 1
  fi
}

install_k3d() {
  os=$(platform)
  arch=$(architecture)
  binary="k3d-$os-$arch"
  url="https://github.com/k3d-io/k3d/releases/download/$K3D_VERSION/$binary"
  checksums_url="https://github.com/k3d-io/k3d/releases/download/$K3D_VERSION/checksums.txt"
  tmp_file=$(mktemp "${TMPDIR:-/tmp}/podlord-k3d.XXXXXX")
  checksums_file=$(mktemp "${TMPDIR:-/tmp}/podlord-k3d-checksums.XXXXXX")

  cleanup_k3d() {
    rm -f "$tmp_file" "$checksums_file"
  }
  trap cleanup_k3d EXIT INT TERM

  echo "Installing k3d $K3D_VERSION into $TOOL_DIR"
  curl -fsSL "$url" -o "$tmp_file"
  curl -fsSL "$checksums_url" -o "$checksums_file"
  expected=$(awk -v binary="$binary" '$2 == binary { print $1 }' "$checksums_file" | head -n 1)
  if [ -z "$expected" ]; then
    echo "No checksum found for $binary in $checksums_url" >&2
    exit 1
  fi

  actual=$(calculate_sha256 "$tmp_file")
  if [ "$actual" != "$expected" ]; then
    echo "Checksum mismatch for $binary" >&2
    exit 1
  fi

  install -m 0755 "$tmp_file" "$TOOL_DIR/k3d"
  trap - EXIT INT TERM
  cleanup_k3d
}

install_kubectl() {
  os=$(platform)
  arch=$(architecture)
  url="https://dl.k8s.io/release/$KUBECTL_VERSION/bin/$os/$arch/kubectl"
  checksum_url="$url.sha256"
  tmp_file=$(mktemp "${TMPDIR:-/tmp}/podlord-kubectl.XXXXXX")
  checksum_file=$(mktemp "${TMPDIR:-/tmp}/podlord-kubectl-sha.XXXXXX")

  cleanup_kubectl() {
    rm -f "$tmp_file" "$checksum_file"
  }
  trap cleanup_kubectl EXIT INT TERM

  echo "Installing kubectl $KUBECTL_VERSION into $TOOL_DIR"
  curl -fsSL "$url" -o "$tmp_file"
  curl -fsSL "$checksum_url" -o "$checksum_file"

  expected=$(tr -d '[:space:]' < "$checksum_file")
  actual=$(calculate_sha256 "$tmp_file")
  if [ "$actual" != "$expected" ]; then
    echo "Checksum mismatch for kubectl $KUBECTL_VERSION" >&2
    exit 1
  fi

  install -m 0755 "$tmp_file" "$TOOL_DIR/kubectl"
  trap - EXIT INT TERM
  cleanup_kubectl
}

if ! command -v docker >/dev/null 2>&1; then
  echo "docker is required for k3d tests" >&2
  exit 1
fi

if ! docker info >/dev/null 2>&1; then
  if command -v colima >/dev/null 2>&1; then
    colima start --cpu 2 --memory 4
  fi
fi

if ! docker info >/dev/null 2>&1; then
  echo "docker is not running; start Docker or Colima before running k3d tests" >&2
  exit 1
fi

if ! command -v k3d >/dev/null 2>&1; then
  install_k3d
fi

if ! command -v kubectl >/dev/null 2>&1; then
  install_kubectl
fi

k3d version
kubectl version --client=true
