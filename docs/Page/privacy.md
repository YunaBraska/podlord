---
layout: default
title: Privacy
description: How PodLord handles cluster data on your device.
---

<header class="page-heading">
  <p class="eyebrow">Privacy</p>
  <h1>Your cluster data is not a PodLord service.</h1>
  <p>PodLord has no account, advertising, analytics, crash reporting or YunaBraska-hosted Kubernetes backend.</p>
</header>

<section class="section">
  <h2>What PodLord handles</h2>
  <p>PodLord reads the kubeconfig and cluster data you explicitly import or connect. It stores app-owned connection snapshots and settings on your device so that it can restore your contexts and tabs. Kubernetes requests, logs, terminals and port-forwarded traffic go directly to the Kubernetes endpoint you chose. Explicitly confirmed native sign-in also contacts the configured identity or cloud provider, using the system browser where required.</p>
  <p>The native edition keeps resource and log caches in memory. It saves the latest map preview for each context locally, without resource names, YAML or Secret values. Imported connection snapshots can contain credentials. AI account tokens and API keys are stored in the operating system's password manager.</p>
  <p>Secret values are hidden and redacted by default. They remain sensitive data under your control.</p>
</section>

<section class="section">
  <h2>Network connections</h2>
  <p>PodLord does not upload cluster data to YunaBraska. The direct-download edition checks GitHub release metadata weekly and opens a compatible download only on request. It does not automatically install an update. GitHub receives normal connection metadata, such as your IP address. App Store builds do not contact GitHub for updates.</p>
  <p>When you request AI analysis or enable it automatically for a context, PodLord sends linked resource facts to the selected model. Cloud providers are ChatGPT, Claude and OpenRouter. Facts can include status, problems, conditions, container states, creation and Event times, configured limits, and relevant Secret or ConfigMap key names. Warning Event and status messages are sent after best-effort masking of known names, addresses and common credential patterns. Arbitrary messages can still contain private information that masking misses. PodLord does not deliberately include kubeconfigs, credentials, Secret values, full logs, file contents or YAML. PodLord connects to Ollama on your computer. Ollama's own configuration can route requests elsewhere. Answers are cached locally for up to 90 days and can be refreshed on request.</p>
  <p>The native Mac App Store build imports individual selected files or pasted kubeconfigs into app-owned copies. It does not scan folders, reload original files or run external credential commands. Native sign-in and explicitly imported keys remain available. Certificate and token file references must be replaced with embedded data or an explicit supported key import.</p>
</section>

<section class="section">
  <h2>Your choices</h2>
  <p>Remove unused imported connections and keys in PodLord. App-owned kubeconfig copies can retain connection data for other contexts. Uninstalling a desktop application does not necessarily remove its local profile. Remove that profile as well when you want to erase all local PodLord data. For help, use <a href="{{ site.support }}">PodLord support</a>. Never post kubeconfigs, tokens or private cluster details in a public issue.</p>
  <p>Last updated: 20 November 2026.</p>
</section>
