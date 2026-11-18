# 0053 Local Incident Analysis

## Status

Accepted on 2026-11-18 for the first provider, Ollama. ChatGPT and other providers require separate authentication and transport work.

## Context

PodLord already projects accepted Kubernetes responses into a shared, cache-first resource snapshot. ADR 0036 covers deterministic hints for an inspected detail. The user also wants automatic, concise explanations of changed problems, without asking an AI again for every Pod retry, Event repetition or replica replacement. This adds an outbound model request and a local result cache, so it has a distinct consent and retention boundary.

## Decision

The desktop inspector offers automatic Ollama analysis per imported context. It starts disabled. The user chooses one installed model and a reasoning level. PodLord discovers local models through `/api/tags` and the selected model's chat and thinking capabilities through `/api/show`. Automatic reasoning chooses `medium` only when the model advertises it; otherwise the model decides. The endpoint is fixed to local loopback in the application and cloud-tagged models are excluded. Ollama itself can route cloud models, so operators who require a strict local-only guarantee must also disable Ollama cloud features. Mobile does not expose this local provider.

The runtime, shared by all windows, owns the request queue, throttling, cancellation and cache. It observes accepted resource snapshots and analyzes only recognized status codes or recognized Warning Event reasons associated with a resource UID. It sends the resource kind and code with the requested UI language. It does not send free status or Event messages, logs, YAML, resource names, credentials or Secret data. Unknown codes yield no AI result and no health assertion. The existing Kubernetes read schedule is unchanged.

The cache key hashes context, namespace, topmost known workload owner, problem code, model, effective reasoning, language and prompt version. It excludes Pod UID, replica UID, restart count, Event count and timestamps. Successful answers are retained in the private profile's `incident-ai.json`, at most 500 entries. The same file stores context consent and model choices. It contains model answers and opaque hashes, not request payloads or Kubernetes free text. Deleting this file resets consent and cached answers. Opting out hides results and cancels queued or active requests for that context.

One model request runs at a time, separated by at least 30 seconds. Each context is limited to five requests per hour. The queue holds at most 32 pending distinct incidents. A failure pauses new model requests for 15 minutes, with an explicit model refresh available to retry. The UI only reads in-memory results. Model output must parse as short JSON fields; invalid output is shown as a failure. Text is rendered as plain text. Evidence codes and official documentation URLs come from PodLord, not from model output. The only command PodLord offers is a copied, shell-quoted `kubectl describe`; it never executes a command or changes the cluster.

## Consequences

This first slice explains recognized incidents but cannot determine an unobserved root cause. The automatic payload intentionally excludes details such as registry errors in free Event messages, even when that limits precision. The model may still give a wrong suggestion, so the UI labels it as a suggestion and keeps the observed code visible. A later remote provider must reuse the consent, redaction, deduplication and presentation policy while adding its own account and transport boundary. No provider plugin framework is introduced before a second implementation exists.

`native.incident_ai.*` drives the public analysis API against a local HTTP model boundary, including opt-in, payload minimization, owner deduplication, persistence, reasoning, Event retries, cancellation and failure. A separate local live check exercises an installed Ollama model. Existing `native.guidance.*` UI tests verify that the inspector and deterministic hints still render.

## References

- [Ollama model list](https://docs.ollama.com/api/tags)
- [Ollama chat and thinking parameters](https://docs.ollama.com/api/chat)
- [Ollama local-only setting](https://github.com/ollama/ollama/blob/main/docs/faq.mdx#how-do-i-disable-ollama-cloud-features)
- [Kubernetes Pod debugging](https://kubernetes.io/docs/tasks/debug/debug-application/debug-pods/)
