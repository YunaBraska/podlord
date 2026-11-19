# Incident analysis providers

The native desktop PodLord app explains recognized Kubernetes problems using Ollama, a ChatGPT subscription, Claude API or OpenRouter. In **Settings → AI**, use the green **+** beside **Model** to connect a provider. The list then shows actual models under each connected provider's heading. Choosing a model selects its provider. Model lists load at startup and after connection, then refresh in the background when AI settings or the analysis panel reopens after 15 minutes. In a resource's **Overview**, switch on **Auto** for that imported context and provider. The circular arrow requests a fresh answer for the current problem. Hover over or focus an icon for its action name.

PodLord never selects a model automatically. Switching providers preserves each provider's model. A different provider needs its own context opt-in before automatic analysis. PodLord sends only resource kind, recognized code, UI language, status labels of related Node, claim and volume resources, and bounded referenced Secret or ConfigMap key names for relevant Pod errors. It rejects suspicious encoded or token-like key names. It does not send logs, arbitrary Event messages, resource names, Secret values or Base64 data. The answer cache lasts up to 90 days. Repeated Pods, Events and retries of the same problem reuse an answer; changed related statuses or key references trigger a new one.

## Ollama

Start Ollama locally, install a chat model with `ollama pull <model>`, then select it. PodLord connects only to `127.0.0.1:11434` and excludes models tagged `:cloud`. For strict local operation, set `disable_ollama_cloud` to `true` in `~/.ollama/server.json` and restart Ollama. See the [Ollama FAQ](https://github.com/ollama/ollama/blob/main/docs/faq.mdx#how-do-i-disable-ollama-cloud-features).

## ChatGPT subscription

Press **+ → ChatGPT** and approve PodLord in the system browser, then choose a model from the loaded catalog. PodLord keeps one ChatGPT account at a time. The sign-out icon beside the account removes it before another account can connect. If plan permission was denied, choose **+ → ChatGPT** again. PodLord does not read Zed, Codex or another application's tokens. Direct app use must be allowed by the ChatGPT plan. Review app access in [ChatGPT Settings](https://chatgpt.com/settings). See OpenAI's [sign-in](https://developers.openai.com/siwc/token-sharing-open-source/sign-in), [models](https://developers.openai.com/siwc/token-sharing-open-source/models-and-inference) and [account management](https://developers.openai.com/siwc/token-sharing-open-source/profiles-and-sessions) documentation.

## Claude API

Create a workspace-scoped API key in the Claude Console, press **+ → Claude API**, paste the key and press Enter or the save icon. Choose a model when the catalog loads. A Claude web or Claude Code subscription does not supply this key or pay for API usage. If the catalog is unavailable, check key scope and access in the [Claude API overview](https://platform.claude.com/docs/en/api/overview) and [model-list reference](https://platform.claude.com/docs/en/api/models/list). PodLord calls the [Messages API](https://platform.claude.com/docs/en/api/messages/create) with version `2026-10-21`.

## OpenRouter

Press **+ → OpenRouter** and finish sign-in in the system browser. OpenRouter issues an API key to PodLord using a local callback and PKCE. Select a text model when the catalog loads. PodLord keeps one OpenRouter connection at a time; the sign-out icon removes its key. OpenRouter uses its own credits and billing. PodLord reads its current [model catalog](https://openrouter.ai/docs/api/api-reference/models/list-all-models-and-their-properties) and calls [Chat Completions](https://openrouter.ai/docs/api/api-reference/chat/send-chat-completion-request). Check the [OpenRouter OAuth guide](https://openrouter.ai/docs/guides/overview/auth/oauth) if browser sign-in fails. PodLord does not invoke an automatic model selector unless you explicitly choose that catalog entry.

## Credentials and cache

The selected profile contains `incident-ai.json` for provider, models, consent and cached answers. Credentials are stored in macOS Keychain, Windows Credential Manager or the Linux Secret Service. PodLord reads them automatically under your OS account. If the password store is unavailable or locked, cloud providers stop with an error; PodLord does not fall back to plaintext files. Existing owner-only `chatgpt-auth.json`, `claude-auth.json` and `openrouter-auth.json` files are migrated on first launch and deleted after a successful transfer. Migration keeps the active ChatGPT account. **Sign out** and **Remove key** clear local credentials. ChatGPT sign-out also attempts remote revocation. If remote revocation cannot be confirmed, disconnect PodLord in ChatGPT settings. Remove `incident-ai.json` while PodLord is closed to erase cached answers and automatic opt-ins. The cache stores model answers and opaque hashes, not Kubernetes free text.

If credential access fails, unlock your macOS login keychain or check that Windows Credential Manager is available under your signed-in account. On Linux, start and unlock a Secret Service implementation such as GNOME Keyring in your desktop session. If migration fails, close other PodLord instances and check that the old file is readable only by its owner. PodLord leaves that file in place until transfer succeeds.

PodLord offers a copied read-only `kubectl describe` command but never runs it. Check the observed code and Kubernetes documentation before changing the cluster.
