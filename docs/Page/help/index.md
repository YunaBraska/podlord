---
layout: default
help: true
title: Podlord help
description: Open contexts, find resources and work with container shells.
---

# Podlord help

## Open a context

Open or drop a kubeconfig into Podlord, or paste its contents. The original file stays unchanged. Each context gets its own card. Choose a card to open it and connect.

The overview uses saved map previews. It does not sign in to every cluster. Frequently used contexts appear first when you return to the overview. Use a card's menu to rename it locally, open another tab or window, or remove it.

Treat kubeconfigs as sensitive. Podlord does not send them to a hosted Podlord service.

### Mac App Store edition

Choose an individual file or paste a kubeconfig. The Store edition does not scan folders or run external login programs. Use a supported built-in sign-in flow, embedded credentials or an explicit supported key import.

## Explore resources

<a id="explore"></a>

Use Map for the cluster's namespace islands, or Resources for a sortable table. Namespace and cluster filters narrow the same resource set in the table and radar. Problems and Activity help focus the view. Save useful filters to reuse them for that context.

Click Map again to return to All Contexts. Context settings are shared when you open the same context elsewhere. Selection, camera position and running connections belong to each tab.

![Resource table, minimap and filters]({{ '/assets/screenshots/native-resources.png' | relative_url }})

## Inspect and act

<a id="inspect-and-act"></a>

Select a resource to inspect its details, events, YAML, logs and supported actions. ConfigMap and Secret keys are listed separately. Secret values stay hidden until you explicitly reveal or copy them.

Wait for current YAML before editing. Applying YAML and deleting resources require confirmation. Changing a filter or column does not change the cluster.

![YAML inspector for the selected resource]({{ '/assets/screenshots/native-inspector-yaml.png' | relative_url }})

## Open a container shell

<a id="terminal"></a>

Open Terminal in a Pod's inspector, choose the container and connect. The shell supports ANSI colors, keyboard input and interactive programs. Podlord does not need kubectl to connect.

Use Connections to return to an open shell or stop a port forward. Closing its tab stops those connections.

![Connected container terminal with colored shell output]({{ '/assets/screenshots/native-terminal.png' | relative_url }})

## Resolve a connection problem

Errors identify the affected context or resource. Authentication failures do not trigger automatic sign-in attempts. Choose Sign in when you want to retry. Access to resources, logs and metrics depends on the cluster's permissions and installed APIs. Missing measurements are not reported as zero usage.
