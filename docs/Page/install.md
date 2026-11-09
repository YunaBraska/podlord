---
layout: default
title: Get PodLord
description: Install PodLord with Homebrew on macOS or download a platform archive.
---

<header class="page-heading">
  <p class="eyebrow">Get PodLord</p>
  <h1>Choose the build for your desktop.</h1>
  <p>PodLord offers archives for macOS, Linux and Windows. The Homebrew cask is available for macOS.</p>
</header>

<section id="homebrew" class="install-step">
  <h2>Homebrew for macOS</h2>
  <p>Add and trust the tap once, then install the cask.</p>
  <pre><code>brew tap yunabraska/tap
brew trust yunabraska/tap
brew install --cask podlord</code></pre>
</section>

<section class="install-step">
  <h2>Native snapshots and stable releases</h2>
  <p>The landing-page images show C++ PodLord. For that edition, choose a GitHub prerelease containing <code>podlord-native-</code> archives after a qualified merge to main. The latest stable release and Homebrew may still contain C# PodLord. Select the archive for your operating system and processor. On macOS, move the extracted app to Applications.</p>
  <p>Snapshots are prereleases, not Store releases or a notarization claim. Regular releases remain manual. Homebrew installations update with <code>brew upgrade</code>.</p>
  <p><a class="button" href="https://github.com/YunaBraska/podlord/releases">Browse releases and snapshots</a></p>
</section>

<section class="install-step">
  <h2>Mac App Store</h2>
  <p>Coming soon for the macOS build.</p>
</section>
