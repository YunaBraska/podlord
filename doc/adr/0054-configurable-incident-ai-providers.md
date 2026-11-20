# 0054 Configurable Incident AI Providers

## Status

Accepted on 2026-11-18.

## Context

ADR 0053 introduced an Ollama-only analyzer. Users want a ChatGPT subscription, Claude and OpenRouter, with an explicit model choice for each provider. Consent for local Ollama must not authorize a cloud request.

## Decision

The shared analyzer continues to own redaction, deduplication, throttling, 90-day retention, cancellation and presentation. Each provider supplies a model catalog and inference transport. The provider and account or credential identity enter the cache key. Each provider has a separate selected model and automatic-analysis opt-in per imported context. Earlier consent migrated only to Ollama. Request version 4 clears all older automatic opt-ins because names and images are newly sent. No model is selected automatically. Connecting another provider leaves the active model and analysis untouched. Signing out the selected provider clears its model and requires an explicit new choice.

ChatGPT uses the public browser OAuth flow with a loopback callback, state, nonce and PKCE. One active registration retains its issued client ID, verified identity and refresh token in the OS credential store. Access and ID tokens stay in memory. ID-token signature, issuer, audience, expiry and nonce are checked against OpenAI's JWKS. Inference requires the `chatgpt.tokens.use.direct` grant. The account-specific catalog supplies model choices. Responses requests use `store: false` and `stream: true` and succeed only after `response.completed`. Refresh-token replacement is serialized across processes. Sign-out clears local credentials and attempts remote revocation.

Claude uses a user-supplied API key. OpenRouter accepts a user-supplied API key or a browser OAuth sign-in with a loopback callback, state and PKCE. The browser flow exchanges a one-time code for an API key. Neither provider reuses a web subscription as an API credential. Claude calls Messages; OpenRouter calls Chat Completions. The UI never displays a saved key. Removing it cancels active analysis. macOS uses Keychain, Windows uses Credential Manager and Linux uses Secret Service. Old private credential files are migrated and removed after a successful transfer. An unavailable OS store disables cloud credentials without a plaintext fallback.

All providers use ADR 0053's redacted JSON request: UI language, resource kind, recognized problem code or manual-review marker, bounded related signals and separately identified linked resources. Real resource names, namespaces, container names, image references and valid Kubernetes UUIDs may be sent. Resource API paths stay local. Pod evidence may include readiness, per-container states and image-pull policy, conditions, QoS, requests and limits, observed restart count, screened last termination reasons and exit codes. Available Pod and Node CPU and memory usage includes measurement time, completeness and staleness. A verified comparison to a known limit or capacity is derived locally. Creation and Event times, Event action, rollout counts, storage size and access modes provide chronology and dependency context. Secret type and registry auth key presence are sent without Secret data. Restart and Event retry counts alone do not invalidate the answer cache. Relevant Secret or ConfigMap key names pass token and encoded-value checks first. Warning Event, container-state and condition messages are sent after best-effort redaction. Known resource names, image addresses, URLs and common credential patterns are masked in free-text messages, but arbitrary text may still contain private data. Full logs, YAML, kubeconfigs, credentials and Secret values are not deliberately included. Answers render as escaped text with local links for known resource names; older two-field answers remain readable. The inspector can show current model instructions and user JSON through Request, with Response as default. PodLord supplies documentation links and builds a copyable read-only command locally. Models never execute commands. The supported kubeconfig `exec.env` diagnostic sends a fixed code and field, not the raw error. Unknown diagnostic text stays local.

## Consequences

Cloud inference may consume a ChatGPT plan allowance or incur Claude/OpenRouter API charges. Missing credentials or a selected available model stop analysis. One combined model list groups actual models under connected-provider headings; selecting a model chooses its provider. AI settings list every connected account, independent of model choice. A rolling 24-hour count includes actual inference requests only. The profile stores timestamped, hashed account identifiers for this count and discards expired entries. OAuth callback pages use the selected UI language and contain no external assets. Catalogs load on startup, after connection and when AI settings or analysis reopens after 15 minutes. A future provider must reuse the shared analyzer and prove its credential boundary, catalog and inference through tests.

## References

- [OpenAI sign-in](https://developers.openai.com/siwc/token-sharing-open-source/sign-in)
- [OpenAI models and inference](https://developers.openai.com/siwc/token-sharing-open-source/models-and-inference)
- [OpenAI accounts and sessions](https://developers.openai.com/siwc/token-sharing-open-source/profiles-and-sessions)
- [Claude API overview](https://platform.claude.com/docs/en/api/overview)
- [Claude model list](https://platform.claude.com/docs/en/api/models/list)
- [Claude Messages](https://platform.claude.com/docs/en/api/messages/create)
- [OpenRouter model list](https://openrouter.ai/docs/api/api-reference/models/list-all-models-and-their-properties)
- [OpenRouter Chat Completions](https://openrouter.ai/docs/api/api-reference/chat/send-chat-completion-request)
- [OpenRouter OAuth](https://openrouter.ai/docs/guides/overview/auth/oauth)
