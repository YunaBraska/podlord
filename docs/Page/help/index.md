---
layout: default
help: true
title: Podlord help
description: Explore Kubernetes resources and take explicit, informed actions.
---

# Podlord help

Podlord is a desktop console for understanding a Kubernetes cluster without living in a namespace tree. It imports kubeconfigs into its own workspace, so the source file is not modified during normal use.

## Import a cluster

Start by importing a kubeconfig from the usual location, a file, a directory, pasted YAML or a generated k3d configuration. Podlord keeps an app-owned snapshot and makes the selected cluster visible before it requests data.

Treat imported kubeconfigs as sensitive. The app does not send them to a hosted service.

## Explore resources

<a id="explore"></a>

The resource explorer is flat across namespaces by default. Search, sort and filter until the table answers the question you came with. Columns can be reordered, resized or hidden without changing the cluster.

![Podlord resource explorer]({{ '/assets/help/resource-explorer.png' | relative_url }})

## Inspect and act

<a id="inspect-and-act"></a>

Select a resource to open the inspector. It brings together metrics, related events, YAML, supported logs, ConfigMap or Secret keys, and links. Secret values stay hidden by default and require an explicit reveal or copy action.

![Podlord inspector and filters]({{ '/assets/help/inspector-settings.png' | relative_url }})

Actions such as applying YAML, deleting a resource or starting a port forward stay explicit. Podlord shows the current context rather than assuming a shell configuration or calling `kubectl` behind your back.

## When data is incomplete

Cluster roles decide what can be read. If metrics, logs or a resource kind are unavailable, Podlord reports visibility and freshness rather than presenting a guess as current data. Check your cluster access before changing an app setting.
