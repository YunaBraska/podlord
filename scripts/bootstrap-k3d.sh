#!/bin/sh
set -eu

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
  if command -v brew >/dev/null 2>&1; then
    brew install k3d
  else
    echo "k3d is required. Install it from https://k3d.io/ or via your package manager." >&2
    exit 1
  fi
fi

if ! command -v kubectl >/dev/null 2>&1; then
  if command -v brew >/dev/null 2>&1; then
    brew install kubectl
  else
    echo "kubectl is required for k3d scenario setup." >&2
    exit 1
  fi
fi

k3d version
kubectl version --client=true
