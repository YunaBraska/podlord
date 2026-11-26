# Contributing

PodLord is a C++ and Qt Quick application. See [native/README.md](native/README.md)
for supported toolchains, platform builds and local launch instructions.

## Test

From the repository root:

```sh
sh scripts/test-native.sh
sh scripts/test-native-kubernetes.sh
```

The Kubernetes suite uses an owned, disposable local cluster. Check its cleanup
result before relying on the evidence. For a smaller change, run the relevant
CTest scenario in your configured native build directory.

## Change rules

- Keep Kubernetes requests inside the native client and its request queue.
- Never log kubeconfig content, tokens, certificates or Secret values.
- Add a behavior test for reproducible fixes and run the affected UI journey.
- Keep UI controls keyboard accessible and translated.
- Update the applicable specification or decision when behavior changes.
- Keep generated packages, profiles and local credentials out of git.

Use concise Conventional Commits such as `fix: preserve selection after refresh`.
