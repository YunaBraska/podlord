---
layout: default
title: Kubernetes without the tree-walk
description: Explore your clusters, inspect resources and open container shells in one app.
---

<section class="hero">
  <div class="hero-copy">
    <p class="eyebrow">Kubernetes, in view</p>
    <h1>Work the cluster, not the tree.</h1>
    <p class="lead">See your clusters, find problems and inspect resources without losing your place.</p>
    <p class="actions"><a class="button" href="install.html">Get PodLord</a><a class="button secondary" href="documentation.html">Read the documentation</a></p>
  </div>
  <figure class="hero-art">
    <a href="{{ '/assets/screenshots/native-map.png' | relative_url }}" aria-label="View the cluster map at full size"><img src="{{ '/assets/screenshots/native-map.png' | relative_url }}" width="1440" height="920" fetchpriority="high" alt="Cluster map with distinct namespace islands and highlighted problems"></a>
    <figcaption>Your cluster at a glance. Problems stand out.</figcaption>
  </figure>
</section>

<section class="section">
  <h2>From a problem to the next useful action.</h2>
  <p class="section-intro">Move between the map, resource table and inspector. Open a shell when you need one.</p>
  <div class="feature-grid">
    <article class="feature"><h3>Your configuration stays yours</h3><p>Import a kubeconfig without changing the original file.</p></article>
    <article class="feature"><h3>Inspect without hunting</h3><p>Open YAML, logs, metrics, a container shell or a port forward from the selected resource.</p></article>
    <article class="feature"><h3>Keep sensitive values quiet</h3><p>Secret values are hidden and redacted by default so the information you need does not become the information you expose.</p></article>
  </div>
</section>

<section class="section">
  <h2>Fast where it matters, deliberate where it counts.</h2>
  <p class="section-intro">The interface is built around keeping context while you check resource state, choose a different source or prepare a next action.</p>
  <div class="screenshot-grid">
    <figure><a href="{{ '/assets/screenshots/native-resources.png' | relative_url }}" aria-label="View the resource explorer at full size"><img src="{{ '/assets/screenshots/native-resources.png' | relative_url }}" width="1440" height="920" loading="lazy" alt="Resource table with status, kind, namespace and cluster columns beside the minimap and filters"></a><figcaption>Resources, filters and the minimap stay in step.</figcaption></figure>
    <figure><a href="{{ '/assets/screenshots/native-inspector-yaml.png' | relative_url }}" aria-label="View the YAML inspector at full size"><img src="{{ '/assets/screenshots/native-inspector-yaml.png' | relative_url }}" width="1440" height="920" loading="lazy" alt="YAML inspector opened for a ConfigMap, with syntax highlighting, search and an explicit Edit YAML action"></a><figcaption>Inspect fresh YAML without losing the selected resource or filters.</figcaption></figure>
    <figure class="terminal-shot"><a href="{{ '/assets/screenshots/native-terminal.png' | relative_url }}" aria-label="View the container terminal at full size"><img src="{{ '/assets/screenshots/native-terminal.png' | relative_url }}" width="1440" height="920" loading="lazy" alt="Interactive container terminal with ANSI-colored shell output, container selection and connection controls"></a><figcaption>A real container shell, with ANSI colors and keyboard input. No kubectl required.</figcaption></figure>
  </div>
</section>

<section class="section">
  <h2>Get PodLord your way.</h2>
  <p class="section-intro">These images show the new PodLord experience. Look for a snapshot to try it. Stable releases and Homebrew may still contain the previous version. The Mac App Store edition is coming later.</p>
  <div class="install-options">
    <article class="install-option"><h3>Homebrew</h3><p>Install the macOS cask with the YunaBraska tap.</p><a href="install.html#homebrew">Install with Homebrew</a></article>
    <article class="install-option"><h3>Native snapshots</h3><p>Look for a prerelease with <code>podlord-native-</code> archives for macOS, Linux or Windows.</p><a href="https://github.com/YunaBraska/podlord/releases">Browse native snapshots</a></article>
    <article class="install-option"><h3>Mac App Store</h3><p>Coming soon. The App Store channel will be available for the macOS build.</p></article>
  </div>
</section>
