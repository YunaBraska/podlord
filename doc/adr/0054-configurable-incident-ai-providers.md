# 0054 Configurable Incident AI Providers

## Status

Accepted on 2026-11-18.

## Context

ADR 0053 introduced an Ollama-only analyzer. Users want a ChatGPT subscription, Claude and OpenRouter, with an explicit model choice for each provider. Consent for local Ollama must not authorize a cloud request.

## Decision

The shared analyzer continues to own redaction, deduplication, throttling, 90-day retention, cancellation and presentation. Each provider supplies a model catalog and inference transport. The provider and account or credential identity enter the cache key. Each provider has a separate selected model and automatic-analysis opt-in per imported context. Existing consent migrates only to Ollama. No model is selected automatically.

ChatGPT uses the public browser OAuth flow with a loopback callback, state, nonce and PKCE. One active registration retains its issued client ID, verified identity and refresh token in the OS credential store. Access and ID tokens stay in memory. ID-token signature, issuer, audience, expiry and nonce are checked against OpenAI's JWKS. Inference requires the `chatgpt.tokens.use.direct` grant. The account-specific catalog supplies model choices. Responses requests use `store: false` and `stream: true` and succeed only after `response.completed`. Refresh-token replacement is serialized across processes. Sign-out clears local credentials and attempts remote revocation.

Claude uses a user-supplied API key. OpenRouter accepts a user-supplied API key or a browser OAuth sign-in with a loopback callback, state and PKCE. The browser flow exchanges a one-time code for an API key. Neither provider reuses a web subscription as an API credential. Claude calls Messages; OpenRouter calls Chat Completions. The UI never displays a saved key. Removing it cancels active analysis. macOS uses Keychain, Windows uses Credential Manager and Linux uses Secret Service. Old private credential files are migrated and removed after a successful transfer. An unavailable OS store disables cloud credentials without a plaintext fallback.

All providers receive only resource kind, recognized problem code, UI language, related status labels, matched Warning Event reasons, bounded same-namespace problem categories, image pull Secret reference presence, and bounded referenced Secret or ConfigMap key names for relevant Pod errors. Key names are checked for token and encoded-value patterns before transmission. Same-namespace problems are labeled as potentially unrelated; cached Secret presence is not asserted as kubelet or registry access. No cluster names, resource names, image addresses, Secret names, logs, arbitrary status or Event messages, Secret values or Base64 data are sent. Model text is rendered as plain text. PodLord supplies observed codes, Kubernetes documentation and a copied read-only command locally. Models never execute commands or change the cluster.

## Consequences

Cloud inference may consume a ChatGPT plan allowance or incur Claude/OpenRouter API charges. Missing credentials or a selected available model stop analysis. One combined model list groups actual models under connected-provider headings; selecting a model chooses its provider. Catalogs load on startup, after connection and when AI settings or analysis reopens after 15 minutes. A future provider must reuse the shared analyzer and prove its credential boundary, catalog and inference through tests.

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
