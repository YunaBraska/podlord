---
layout: default
title: Kubernetes without the tree-walk
description: Find Kubernetes problems, follow related resources and inspect files in PodLord.
---

<section class="hero">
  <div class="hero-copy">
    <p class="eyebrow">Kubernetes, in view</p>
    <h1>Work the cluster, not the tree.</h1>
    <p class="lead">See the whole cluster, follow a problem across resources and check the next step in one place.</p>
    <p class="actions"><a class="button" href="install.html">Get PodLord</a><a class="button secondary" href="documentation.html">Read the documentation</a></p>
  </div>
  <figure class="hero-art">
    <a href="{{ '/assets/screenshots/native-map.png' | relative_url }}" aria-label="View the cluster map at full size"><img src="{{ '/assets/screenshots/native-map.png' | relative_url }}" width="1440" height="920" fetchpriority="high" alt="Cluster map with distinct namespace islands and highlighted problems"></a>
    <figcaption>Map from a disposable demo cluster with 1,845 generated resources. No user cluster data.</figcaption>
  </figure>
</section>

<section class="section">
  <h2>Find it. Understand it. Check it.</h2>
  <p class="section-intro">Map, filters and inspector share the same resource selection.</p>
  <div class="feature-grid">
    <article class="feature"><h3>Keep the cluster in view</h3><p>Spot warnings on the map. Search and filter the same resources in a table.</p></article>
    <article class="feature"><h3>Follow the connections</h3><p>Linked Pods, Nodes, volumes and Events stay visible as you inspect a problem.</p></article>
    <article class="feature"><h3>Inspect in place</h3><p>Read YAML, logs, metrics, Events and links. Open a shell or port forward where supported.</p></article>
    <article class="feature"><h3>Stay in control</h3><p>Imported kubeconfigs leave the original file unchanged. Writes and deletions require confirmation.</p></article>
  </div>
</section>

<section class="section showcase" id="ai">
  <div class="showcase-copy">
    <p class="eyebrow">AI analysis</p>
    <h2>A warning has more than one resource.</h2>
    <p>On desktop, PodLord sends the selected resource and relevant linked facts to your chosen model. The answer separates observation, possible cause and next check.</p>
    <ul class="feature-list">
      <li>Open named resources directly from the answer.</li>
      <li>Preview the request and ask again only when needed. Answers are cached for up to 90 days.</li>
      <li>Choose a ChatGPT subscription, Claude API, OpenRouter or local Ollama model.</li>
    </ul>
    <a href="{{ '/help/index.html#ai-analysis' | relative_url }}">How AI analysis works</a>
  </div>
  <figure class="showcase-art">
    <a href="{{ '/assets/screenshots/demo-ai.svg' | relative_url }}" aria-label="View the synthetic AI example at full size"><img src="{{ '/assets/screenshots/demo-ai.svg' | relative_url }}" width="840" height="580" loading="lazy" alt="Illustrative AI inspector: a Pending checkout Pod, Pending claim, Ready Node and FailedMount warning lead to a possible storage cause and a read-only next check"></a>
    <figcaption>Illustrative report with fictional names and values. The model's answer depends on the real observations.</figcaption>
  </figure>
</section>

<section class="section showcase showcase-reverse" id="files">
  <div class="showcase-copy">
    <p class="eyebrow">Container files</p>
    <h2>Open a file without leaving the Pod.</h2>
    <p>Choose a Pod and container, browse directories and download a file from the inspector. PodLord uses the Kubernetes connection directly.</p>
    <ul class="feature-list">
      <li>Works for supported Pods and workloads with Pod exec permission.</li>
      <li>No local <code>kubectl</code> installation is needed.</li>
    </ul>
    <a href="{{ '/help/index.html#container-files' | relative_url }}">File browser requirements</a>
  </div>
  <figure class="showcase-art">
    <a href="{{ '/assets/screenshots/demo-files.svg' | relative_url }}" aria-label="View the synthetic file browser example at full size"><img src="{{ '/assets/screenshots/demo-files.svg' | relative_url }}" width="840" height="580" loading="lazy" alt="Illustrative Files inspector with a fictional checkout Pod, app container, directories and downloadable files"></a>
    <figcaption>Illustrative file list. All paths and filenames are fictional.</figcaption>
  </figure>
</section>

<section class="section">
  <h2>Inspect the detail. Keep your place.</h2>
  <p class="section-intro">These app captures use disposable local test data. Open an image for its full view.</p>
  <div class="screenshot-grid">
    <figure class="resource-shot"><a href="{{ '/assets/screenshots/native-resources.png' | relative_url }}" aria-label="View the resource explorer at full size"><img src="{{ '/assets/screenshots/native-resources.png' | relative_url }}" width="1440" height="920" loading="lazy" alt="Resource table with status, kind and namespace beside a minimap and filters in a disposable test cluster"></a><figcaption>Filter a dense resource set and retain the minimap.</figcaption></figure>
    <figure class="yaml-shot"><a href="{{ '/assets/screenshots/native-inspector-yaml.png' | relative_url }}" aria-label="View the YAML inspector at full size"><img src="{{ '/assets/screenshots/native-inspector-yaml.png' | relative_url }}" width="1440" height="920" loading="lazy" alt="YAML inspector for a test ConfigMap below its selected resource row"></a><figcaption>Read YAML alongside the selected resource.</figcaption></figure>
    <figure class="terminal-shot"><a href="{{ '/assets/screenshots/native-terminal.png' | relative_url }}" aria-label="View the container terminal at full size"><img src="{{ '/assets/screenshots/native-terminal.png' | relative_url }}" width="1440" height="920" loading="lazy" alt="Container terminal in a disposable local test cluster"></a><figcaption>Work in a container shell without a local kubectl dependency.</figcaption></figure>
  </div>
</section>

<section class="section">
  <h2>More than a single cluster view.</h2>
  <p class="section-intro">Multiple contexts, saved filters, resource metrics, custom alerts and optional music stay within the same workspace. Secret values remain hidden until you choose to reveal them.</p>
  <p class="section-link"><a href="{{ '/documentation.html' | relative_url }}">Explore the documentation</a> · <a href="{{ '/privacy.html' | relative_url }}">See what AI can receive</a></p>
</section>

<section class="section">
  <h2>Get PodLord your way.</h2>
  <p class="section-intro">The native app is still being qualified for release. Stable releases and Homebrew may contain an earlier version. Check a release's build information before installing.</p>
  <div class="install-options">
    <article class="install-option"><h3>Homebrew</h3><p>Install the macOS cask with the YunaBraska tap.</p><a href="install.html#homebrew">Install with Homebrew</a></article>
    <article class="install-option"><h3>Native snapshots</h3><p>Look for a prerelease with <code>podlord-native-</code> archives for macOS, Linux or Windows.</p><a href="https://github.com/YunaBraska/podlord/releases">Browse native snapshots</a></article>
    <article class="install-option"><h3>Mac App Store</h3><p>Coming soon. The App Store channel will be available for the macOS build.</p></article>
  </div>
</section>
