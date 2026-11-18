# Incident analysis providers

The native desktop PodLord app explains recognized Kubernetes problems using Ollama, a ChatGPT subscription, Claude API or OpenRouter API. In **Settings → AI**, choose a provider, connect its account or key, refresh the model list and select a model. In a resource's **Overview**, enable automatic analysis for that imported context and provider. **Analyze again** forces a fresh answer for the current problem.

PodLord never selects a model automatically. Switching providers preserves each provider's model. A different provider needs its own context opt-in before automatic analysis. PodLord sends only resource kind, recognized code, UI language and status labels of related Node, claim and volume resources. It does not send logs, arbitrary Event messages, resource names, key names, Secret values or Base64 data. The answer cache lasts up to 90 days. Repeated Pods, Events and retries of the same problem reuse an answer; changed related statuses trigger a new one.

## Ollama

Start Ollama locally, install a chat model with `ollama pull <model>`, then select it. PodLord connects only to `127.0.0.1:11434` and excludes models tagged `:cloud`. For strict local operation, set `disable_ollama_cloud` to `true` in `~/.ollama/server.json` and restart Ollama. See the [Ollama FAQ](https://github.com/ollama/ollama/blob/main/docs/faq.mdx#how-do-i-disable-ollama-cloud-features).

## ChatGPT subscription

Choose **ChatGPT** and press **Continue with ChatGPT**. Select an account and approve PodLord in the system browser. Return to PodLord and choose a model from that account's catalog. **Add account** registers another account; the account picker switches between saved registrations. If plan permission was denied, **Continue with ChatGPT** asks for consent again. PodLord does not read Zed, Codex or another application's tokens. Direct app use must be allowed by the ChatGPT plan. Review usage and app access in [ChatGPT Settings → Usage](https://chatgpt.com/settings/usage). See OpenAI's [sign-in](https://developers.openai.com/siwc/token-sharing-open-source/sign-in), [models](https://developers.openai.com/siwc/token-sharing-open-source/models-and-inference) and [account management](https://developers.openai.com/siwc/token-sharing-open-source/profiles-and-sessions) documentation.

## Claude API

Create a workspace-scoped API key in the Claude Console, choose **Claude API**, paste it and press **Save**. Refresh models and choose one. A Claude web or Claude Code subscription does not supply this key or pay for API usage. If the catalog is unavailable, check key scope and access in the [Claude API overview](https://platform.claude.com/docs/en/api/overview) and [model-list reference](https://platform.claude.com/docs/en/api/models/list). PodLord calls the [Messages API](https://platform.claude.com/docs/en/api/messages/create) with version `2026-10-21`.

## OpenRouter API

Create an API key in your OpenRouter account, choose **OpenRouter**, paste it and press **Save**. Refresh models and select a text model. OpenRouter uses its own credits and billing. PodLord reads its current [model catalog](https://openrouter.ai/docs/api/api-reference/models/list-all-models-and-their-properties) and calls [Chat Completions](https://openrouter.ai/docs/api/api-reference/chat/send-chat-completion-request). Check the [OpenRouter developer setup](https://openrouter.ai/developers) if the catalog is unavailable. PodLord does not invoke an automatic model selector unless you explicitly choose that catalog entry.

## Credentials and cache

The selected profile contains `incident-ai.json` for provider, models, consent and cached answers; `chatgpt-auth.json` for ChatGPT registrations; `claude-auth.json` and `openrouter-auth.json` for API keys. All are written with owner-only permissions. **Sign out** and **Remove key** clear local credentials. ChatGPT sign-out also attempts remote revocation. If remote revocation cannot be confirmed, disconnect PodLord in ChatGPT settings. Remove `incident-ai.json` while PodLord is closed to erase cached answers and automatic opt-ins. The cache stores model answers and opaque hashes, not Kubernetes free text.

PodLord offers a copied read-only `kubectl describe` command but never runs it. Check the observed code and Kubernetes documentation before changing the cluster.
