---
layout: default
help: true
title: Podlord help
description: Open contexts, follow resource problems, use AI and browse container files.
---

# PodLord help

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

Select a resource to inspect its details, events, YAML, logs and supported actions. ConfigMap and Secret keys are listed separately. Secret values stay hidden until you explicitly reveal or copy them.

The map highlights linked resources when the inspector is open. The highlight fades with each relationship. Links lists the selected resource first, then related resources with their known problems and messages. Open a row to inspect that resource.

Wait for current YAML before editing. Applying YAML and deleting resources require confirmation. Changing a filter or column does not change the cluster.

![YAML inspector for the selected resource]({{ '/assets/screenshots/native-inspector-yaml.png' | relative_url }})

## AI analysis

On desktop, choose a model in **Settings → AI**. The list groups models under connected ChatGPT, Claude API and OpenRouter accounts or local Ollama. PodLord never changes your selected model when you connect another provider. Enable **Auto** once to analyze new problems in every open context with the selected model. The AI tab appears only when a provider is available.

The report separates **Observation**, **Possible cause** and **Next check**. Known resource names in the answer open those resources in the inspector. **Request** shows the instructions and resource facts that would be sent. The refresh icon asks for a new answer, including when a warning was not analyzed automatically. Otherwise, unchanged problems reuse a cached answer for up to 90 days. The copyable `kubectl` command is a read-only suggestion. PodLord does not run it or require `kubectl` locally.

The model can receive names, statuses, selected Warning Events and linked Pod, Node, volume and other resource facts. Secret values, kubeconfigs, full logs, file contents and full YAML are not included. Event and status messages are masked on a best-effort basis. Review **Request** before enabling AI for sensitive clusters. [Privacy details]({{ '/privacy.html' | relative_url }}) explain the data flow.

![Illustrative AI report for a fictional Pod, claim, Node and warning Event]({{ '/assets/screenshots/demo-ai.svg' | relative_url }})

*Illustrative UI example. All resource names and values above are fictional.*

## Container files

Open **Files** on a supported Pod or workload. Choose the Pod and container, open a directory, then select a file to save a local copy. PodLord talks directly to Kubernetes. Listing and download require Pod exec permission and `/bin/sh` or `cat` inside the container. Downloads are limited to 32 MiB. If those requirements are missing, the browser cannot read the container.

![Illustrative file browser for a fictional checkout container]({{ '/assets/screenshots/demo-files.svg' | relative_url }})

*Illustrative UI example. The paths and filenames above are fictional.*

## Open a container shell

<a id="terminal"></a>

Open Terminal in a Pod's inspector, choose the container and connect. The shell supports ANSI colors, keyboard input and interactive programs. Podlord does not need kubectl to connect.

Use Connections to return to an open shell or stop a port forward. Closing its tab stops those connections.

![Connected container terminal with colored shell output]({{ '/assets/screenshots/native-terminal.png' | relative_url }})

## Alerts and audio

Use **Settings → Alerts** for built-in or custom resource warnings. Focus a matching resource on the map or play an alert sound when a new problem appears. Music is optional and has its own volume control. Muting alerts does not remove the visual warning.

Temporary connection failures play one alert sound per short burst. The sound obeys the alert mute and volume settings.

## Resolve a connection problem

Errors identify the affected context or resource. Authentication failures do not trigger automatic sign-in attempts. Choose Sign in when you want to retry. Access to resources, logs and metrics depends on the cluster's permissions and installed APIs. Missing measurements are not reported as zero usage.

PodLord slows refreshes for unchanged resource kinds and checks related resources when a new problem appears. Temporary read failures are retried without restarting the app. **Settings → Diagnostics** shows request times, results and calls in the past minute. Manual refresh skips the passive interval but still respects server backoff.
