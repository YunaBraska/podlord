---
layout: default
title: Privacy
description: How PodLord handles cluster data on your computer.
---

<header class="page-heading">
  <p class="eyebrow">Privacy</p>
  <h1>Your cluster data is not a PodLord service.</h1>
  <p>PodLord has no account, advertising, analytics, crash reporting or YunaBraska-hosted Kubernetes backend.</p>
</header>

<section class="section">
  <h2>What PodLord handles</h2>
  <p>PodLord reads the kubeconfig and cluster data you explicitly import or connect. It stores app-owned connection snapshots and settings on your computer so that it can reopen your workspace. Cluster data, credentials, logs and port-forwarded traffic are sent directly between your computer and the Kubernetes endpoint you chose.</p>
  <p>Secret values are hidden and redacted by default. They remain sensitive data under your control.</p>
</section>

<section class="section">
  <h2>Network connections</h2>
  <p>PodLord does not upload cluster data to YunaBraska. The direct-download edition checks GitHub for a newer release. GitHub receives the normal connection metadata, such as your IP address, needed to serve that request. The App Store edition uses Apple's update service instead.</p>
  <p>The App Store edition does not run external credential commands. Use kubeconfigs with embedded credentials that are valid for the cluster you select.</p>
</section>

<section class="section">
  <h2>Your choices</h2>
  <p>Remove imported connections in PodLord or uninstall the app to remove its local data. For help, use <a href="{{ site.support }}">PodLord support</a>. Never post kubeconfigs, tokens or private cluster details in a public issue.</p>
  <p>Last updated: 29 October 2026.</p>
</section>
