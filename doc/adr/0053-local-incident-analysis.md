# 0053 Local Incident Analysis

## Status

Accepted on 2026-11-18 for the first provider, Ollama. ADR 0054 adds remote providers and separate provider consent.

## Context

PodLord already projects accepted Kubernetes responses into a shared, cache-first resource snapshot. ADR 0036 covers deterministic hints for an inspected detail. The user also wants automatic, concise explanations of changed problems, without asking an AI again for every Pod retry, Event repetition or replica replacement. This adds an outbound model request and a local result cache, so it has a distinct consent and retention boundary.

## Decision

The desktop inspector offers automatic Ollama analysis per imported context. It starts disabled. The user chooses an installed model and a reasoning level; PodLord does not infer model quality from file size or silently choose the only installed model. PodLord discovers local models through `/api/tags` and the selected model's chat and thinking capabilities through `/api/show`. Automatic reasoning chooses `medium` only when the model advertises it; otherwise the model decides. The endpoint is fixed to local loopback in the application and cloud-tagged models are excluded. Ollama itself can route cloud models, so operators who require a strict local-only guarantee must also disable Ollama cloud features. Mobile does not expose this local provider.

The runtime, shared by all windows, owns the request queue, throttling, cancellation and cache. It observes accepted resource snapshots and analyzes one recognized problem code per affected resource. A matching Warning Event reason can supply the code or add evidence to that resource. It sends the resource kind and code with the requested UI language. For a Pod, it correlates the assigned Node, referenced PersistentVolumeClaims and bound PersistentVolumes by local names. For image pull problems, it checks whether the Pod declares an image pull Secret and whether that Secret appears in cached resources. It also sends up to eight distinct problem categories from the same namespace, clearly labeled as potentially unrelated. The model sees only recognized codes, bounded status labels and these presence signals, never the correlated resource names, image addresses, Secret names or Secret values. Free status and Event messages, logs, YAML, credentials and Base64 data remain excluded. Bounded referenced Secret or ConfigMap key names for relevant Pod errors are covered by ADR 0054. Unknown codes yield no AI result and no health assertion. The existing Kubernetes read schedule is unchanged.

The cache key hashes context, namespace, topmost known workload owner, problem code, related status signals, provider and account or credential identity, model, effective reasoning, language and prompt version. It excludes Pod UID, replica UID, restart count, Event count and timestamps. A replica with the same problem category therefore reuses its workload's answer. A changed warning reason, related status or image pull Secret presence triggers a new answer. Successful answers are retained in the private profile's `incident-ai.json`, at most 500 entries and no longer than 90 days. The same file stores provider-specific context consent and model choices. It contains model answers and opaque hashes, not request payloads or Kubernetes free text. Deleting this file resets consent and cached answers. Opting out hides results and cancels queued or active requests for that context.

One model request runs at a time. Automatic requests are separated by at least 30 seconds and limited to five per hour per context. The queue holds at most 32 pending distinct incidents. A failure pauses automatic requests for 15 minutes. An explicit **Analyze again** action replaces the inspected answer and bypasses those passive limits, while retaining single-flight behavior. The UI only reads in-memory results. Model output must parse as short JSON fields; invalid output is shown as a failure. Text is rendered as plain text. Evidence codes and official documentation URLs come from PodLord, not from model output. The only command PodLord offers is a copied, shell-quoted `kubectl describe`; it never executes a command or changes the cluster.

## Consequences

This slice explains recognized incidents but cannot determine an unobserved root cause. A Secret seen in the cache does not prove the kubelet can use it. A Secret absent from the cache does not prove it is missing. An image pull failure alone does not identify registry reachability, image availability, network failure or authentication failure. The automatic payload intentionally excludes the free Event message even when it might explain the cause. The prompt requires an explicit uncertainty statement and an evidence gathering next step where necessary. The model may still give a wrong suggestion, so the UI labels it as a suggestion and keeps the observed code visible. ADR 0054 describes remote provider boundaries. No general plugin framework is introduced.

`native.incident_ai.*` drives the public analysis API against a local HTTP model boundary, including opt-in, payload minimization, owner deduplication, persistence, reasoning, Event retries, cancellation and failure. A separate local live check exercises an installed Ollama model. Existing `native.guidance.*` UI tests verify that the inspector and deterministic hints still render.

## References

- [Ollama model list](https://docs.ollama.com/api/tags)
- [Ollama chat and thinking parameters](https://docs.ollama.com/api/chat)
- [Ollama local-only setting](https://github.com/ollama/ollama/blob/main/docs/faq.mdx#how-do-i-disable-ollama-cloud-features)
- [Kubernetes Pod debugging](https://kubernetes.io/docs/tasks/debug/debug-application/debug-pods/)
