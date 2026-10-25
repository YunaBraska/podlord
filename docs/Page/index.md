---
layout: default
title: Kubernetes without the tree-walk
description: A cache-first native desktop console for everyday Kubernetes work.
---

<section class="hero">
  <div class="hero-copy">
    <p class="eyebrow">Native Kubernetes operations</p>
    <h1>Work the cluster, not the tree.</h1>
    <p class="lead">PodLord is a cache-first desktop console for looking through Kubernetes resources, inspecting the detail and taking the next useful action without losing your place.</p>
    <p class="actions"><a class="button" href="install.html">Get PodLord</a><a class="button secondary" href="documentation.html">Read the documentation</a></p>
  </div>
  <figure class="hero-art">
    <img src="https://raw.githubusercontent.com/YunaBraska/podlord/main/doc/screenshots/resource-explorer.png" alt="PodLord resource explorer showing Kubernetes resources">
    <figcaption>Explore what is running across namespaces from one native desktop view.</figcaption>
  </figure>
</section>

<section class="section">
  <h2>Designed for the work between kubectl commands.</h2>
  <p class="section-intro">PodLord keeps the current resource set close, lets you investigate from a flat explorer and protects the details that should not be shown by accident.</p>
  <div class="feature-grid">
    <article class="feature"><h3>Own the kubeconfig boundary</h3><p>Import kubeconfigs into explicit, app-owned snapshots rather than letting a desktop tool quietly rewrite your working files.</p></article>
    <article class="feature"><h3>Inspect without hunting</h3><p>Move from a resource list to YAML, details, logs, metrics and native port forwarding without treating every task as a new terminal session.</p></article>
    <article class="feature"><h3>Keep sensitive values quiet</h3><p>Secret values are hidden and redacted by default so the information you need does not become the information you expose.</p></article>
  </div>
</section>

<section class="section">
  <h2>Fast where it matters, deliberate where it counts.</h2>
  <p class="section-intro">The interface is built around keeping context while you check resource state, choose a different source or prepare a next action.</p>
  <div class="screenshot-grid">
    <figure><img src="https://raw.githubusercontent.com/YunaBraska/podlord/main/doc/screenshots/resource-explorer.png" alt="PodLord explorer with resource state and navigation"><figcaption>Search and inspect resources across the cluster without a namespace-first detour.</figcaption></figure>
    <figure><img src="https://raw.githubusercontent.com/YunaBraska/podlord/main/doc/screenshots/inspector-settings.png" alt="PodLord resource inspector settings"><figcaption>Keep inspector and connection behavior visible when the work gets specific.</figcaption></figure>
  </div>
</section>

<section class="section">
  <h2>Get PodLord your way.</h2>
  <p class="section-intro">PodLord is available for macOS, Linux and Windows. The Homebrew cask is for macOS. The Mac App Store channel is planned.</p>
  <div class="install-options">
    <article class="install-option"><h3>Homebrew</h3><p>Install the macOS cask with the YunaBraska tap.</p><a href="install.html#homebrew">Install with Homebrew</a></article>
    <article class="install-option"><h3>GitHub download</h3><p>Choose the current archive for macOS, Linux or Windows.</p><a href="https://github.com/YunaBraska/podlord/releases/latest">Download PodLord</a></article>
    <article class="install-option"><h3>Mac App Store</h3><p>Coming soon. The App Store channel will be available for the macOS build.</p></article>
  </div>
</section>
