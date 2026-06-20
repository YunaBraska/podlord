using System.Net;
using System.Net.Http.Headers;
using System.Net.Sockets;
using System.Security.Cryptography;
using System.Security.Cryptography.X509Certificates;
using System.Text;
using System.Diagnostics;
using Podlord.Core;
using Podlord.Kubernetes;

namespace Podlord.Kubernetes.Tests;

public sealed class FakeKubernetesBehaviorTests
{
    [Theory]
    [InlineData("token", "Bearer", "static-token")]
    [InlineData("token-file", "Bearer", "file-token")]
    [InlineData("auth-provider", "Bearer", "oidc-token")]
    [InlineData("basic", "Basic", "dXNlcjpwYXNz")]
    public async Task Resource_service_applies_kubeconfig_auth_to_requests(string authKind, string scheme, string value)
    {
        var directory = TempDirectory();
        var tokenFile = Path.Combine(directory, "token.txt");
        File.WriteAllText(tokenFile, "file-token\n");
        var kubeconfig = Path.Combine(directory, "config.yaml");
        File.WriteAllText(kubeconfig, Kubeconfig(authKind, directory));
        var handler = new RecordingHandler(_ => JsonResponse(NamespaceList()));
        var service = Service(kubeconfig, handler, directory);

        var snapshot = await service.ListClusterResourcesAsync(new ResourceQuery(Kind: "\"Namespace\""));

        Assert.Empty(snapshot.Failures);
        var authorization = Assert.Single(handler.Authorizations);
        Assert.Equal(scheme, authorization?.Scheme);
        Assert.Equal(value, authorization?.Parameter);
    }

    [Fact]
    public async Task Resource_service_maps_kubernetes_event_name_reason_message_and_involved_object()
    {
        var directory = TempDirectory();
        var kubeconfig = Path.Combine(directory, "config.yaml");
        File.WriteAllText(kubeconfig, Kubeconfig("token", directory));
        var handler = new RecordingHandler(_ => JsonResponse("""
        {"items":[
          {
            "metadata":{"name":"api.17f4","namespace":"payments","uid":"event-1","creationTimestamp":"2026-06-10T08:00:00Z"},
            "type":"Warning",
            "reason":"FailedScheduling",
            "message":"0/2 nodes are available: insufficient cpu",
            "involvedObject":{"kind":"Pod","name":"api-7f9d"}
          }
        ]}
        """));
        var service = Service(kubeconfig, handler, directory);

        var snapshot = await service.ListClusterResourcesAsync(new ResourceQuery(Kind: "\"Event\"", ForceRefresh: true));

        var row = Assert.Single(snapshot.Rows);
        Assert.Equal("Event", row.Kind);
        Assert.Equal("Warning", row.Status);
        Assert.Equal("api.17f4", row.Name);
        Assert.Equal("api.17f4", row.EventName);
        Assert.Equal("FailedScheduling", row.EventReason);
        Assert.Equal("0/2 nodes are available: insufficient cpu", row.EventMessage);
        Assert.Equal("Pod/api-7f9d", row.EventObject);
        Assert.Equal("Pod/api-7f9d", row.Owner);
        Assert.Equal("0/2 nodes are available: insufficient cpu", row.ImageSummary);
    }

    [Fact]
    public async Task Resource_service_reports_estimated_cache_size_after_public_resource_list()
    {
        var directory = TempDirectory();
        var kubeconfig = Path.Combine(directory, "config.yaml");
        File.WriteAllText(kubeconfig, Kubeconfig("token", directory));
        var handler = new RecordingHandler(_ => JsonResponse(NamespaceList()));
        var service = Service(kubeconfig, handler, directory);

        var empty = service.CacheTelemetry();
        var snapshot = await service.ListClusterResourcesAsync(new ResourceQuery(Kind: "\"Namespace\"", ForceRefresh: true));
        var cache = service.CacheTelemetry();

        Assert.Equal(0, empty.TotalEntries);
        Assert.Equal(0, empty.EstimatedBytes);
        Assert.Single(snapshot.Rows);
        Assert.True(cache.ListEntries > 0);
        Assert.True(cache.TotalEntries > 0);
        Assert.True(cache.EstimatedBytes > 0);
    }

    [Fact]
    public async Task Native_port_forward_resolves_service_to_running_pod_and_starts_local_listener_without_kubectl()
    {
        var directory = TempDirectory();
        var kubeconfig = Path.Combine(directory, "config.yaml");
        File.WriteAllText(kubeconfig, Kubeconfig("token", directory));
        var handler = new RecordingHandler(request =>
        {
            var path = request.RequestUri?.PathAndQuery ?? string.Empty;
            if (path.Contains("/services/api", StringComparison.Ordinal))
            {
                return JsonResponse("""
                {
                  "metadata":{"name":"api","namespace":"payments"},
                  "spec":{
                    "selector":{"app":"api"},
                    "ports":[{"name":"web","port":80,"targetPort":"http"}]
                  }
                }
                """);
            }

            if (path.Contains("/pods?", StringComparison.Ordinal))
            {
                return JsonResponse("""
                {
                  "items":[
                    {
                      "metadata":{"name":"api-pod","namespace":"payments"},
                      "spec":{"containers":[{"name":"web","ports":[{"name":"http","containerPort":8080}]}]},
                      "status":{"phase":"Running"}
                    }
                  ]
                }
                """);
            }

            return JsonResponse("""{"items":[]}""");
        });
        var service = Service(kubeconfig, handler, directory);
        using var forward = await service.StartPortForwardAsync(new PortForwardRequest(null, "Service", "payments", "api", FreePort(), 80));

        Assert.True(forward.IsRunning);
        Assert.Contains("/api/v1/namespaces/payments/services/api", handler.Requests);
        Assert.Contains("/api/v1/namespaces/payments/pods?labelSelector=app%3Dapi", handler.Requests);
        Assert.Equal("/api/v1/namespaces/payments/pods/api-pod/portforward?ports=8080", forward.KubernetesPath);
    }

    [Fact]
    public async Task Resource_service_loads_certificate_auth_material_without_exposing_credentials()
    {
        var directory = TempDirectory();
        var kubeconfig = Path.Combine(directory, "config.yaml");
        File.WriteAllText(kubeconfig, CertificateKubeconfig());
        var handler = new RecordingHandler(_ => JsonResponse(NamespaceList()));
        var service = Service(kubeconfig, handler, directory);

        var snapshot = await service.ListClusterResourcesAsync(new ResourceQuery(Kind: "\"Namespace\"", ForceRefresh: true));

        Assert.Empty(snapshot.Failures);
        Assert.Null(Assert.Single(handler.Authorizations));
    }

    [Fact]
    public async Task Resource_service_loads_relative_certificate_files_without_exposing_credentials()
    {
        var directory = TempDirectory();
        var kubeconfig = Path.Combine(directory, "config.yaml");
        File.WriteAllText(kubeconfig, CertificateFileKubeconfig(directory));
        var handler = new RecordingHandler(_ => JsonResponse(NamespaceList()));
        var service = Service(kubeconfig, handler, directory);

        var snapshot = await service.ListClusterResourcesAsync(new ResourceQuery(Kind: "\"Namespace\"", ForceRefresh: true));

        Assert.Empty(snapshot.Failures);
        Assert.Null(Assert.Single(handler.Authorizations));
    }

    [Fact]
    public async Task Resource_service_uses_exec_credential_token_when_exec_plugin_succeeds()
    {
        if (OperatingSystem.IsWindows())
        {
            return;
        }

        var directory = TempDirectory();
        var script = Path.Combine(directory, "exec-token.sh");
        File.WriteAllText(script, """
#!/bin/sh
printf '%s' '{"status":{"token":"exec-token","expirationTimestamp":"2099-01-01T00:00:00Z"}}'
""");
        File.SetUnixFileMode(script, UnixFileMode.UserRead | UnixFileMode.UserExecute | UnixFileMode.UserWrite);
        var kubeconfig = Path.Combine(directory, "config.yaml");
        File.WriteAllText(kubeconfig, Kubeconfig("exec", directory));
        var handler = new RecordingHandler(_ => JsonResponse(NamespaceList()));
        var service = Service(kubeconfig, handler, directory);

        var snapshot = await service.ListClusterResourcesAsync(new ResourceQuery(Kind: "\"Namespace\"", ForceRefresh: true));
        var cached = await service.ListClusterResourcesAsync(new ResourceQuery(Kind: "\"Namespace\"", ForceRefresh: true));

        Assert.Empty(snapshot.Failures);
        Assert.Empty(cached.Failures);
        Assert.Equal(2, handler.Authorizations.Count);
        var authorization = handler.Authorizations[1];
        Assert.Equal("Bearer", authorization?.Scheme);
        Assert.Equal("exec-token", authorization?.Parameter);
    }

    [Fact]
    public void Exec_credential_path_includes_common_macos_gui_missing_tool_locations()
    {
        var augmented = KubeconfigAuthLoader.AugmentedExecPath("/usr/bin:/bin");
        var entries = augmented.Split(Path.PathSeparator);

        Assert.Contains("/usr/bin", entries);
        Assert.Contains("/bin", entries);
        if (!OperatingSystem.IsWindows())
        {
            Assert.Contains("/opt/homebrew/bin", entries);
            Assert.Contains("/usr/local/bin", entries);
        }
    }

    [Fact]
    public void Exec_credential_command_resolution_finds_command_from_augmented_path()
    {
        var directory = TempDirectory();
        var commandName = OperatingSystem.IsWindows() ? "podlord-token.exe" : "podlord-token";
        var commandPath = Path.Combine(directory, commandName);
        File.WriteAllText(commandPath, string.Empty);

        var resolved = KubeconfigAuthLoader.ResolveExecCommand(commandName, directory);

        Assert.Equal(commandPath, resolved);
    }

    [Fact]
    public async Task Resource_service_caches_exec_credential_token_when_expiration_is_missing()
    {
        if (OperatingSystem.IsWindows())
        {
            return;
        }

        var directory = TempDirectory();
        var script = Path.Combine(directory, "exec-counter.sh");
        File.WriteAllText(script, """
#!/bin/sh
count_file="$PODLORD_EXEC_COUNTER"
count=0
if [ -f "$count_file" ]; then
  count=$(cat "$count_file")
fi
count=$((count + 1))
printf '%s' "$count" > "$count_file"
printf '%s' '{"status":{"token":"exec-cache-token"}}'
""");
        File.SetUnixFileMode(script, UnixFileMode.UserRead | UnixFileMode.UserExecute | UnixFileMode.UserWrite);
        var kubeconfig = Path.Combine(directory, "config.yaml");
        File.WriteAllText(kubeconfig, Kubeconfig("exec-counter", directory));
        var handler = new RecordingHandler(_ => JsonResponse(NamespaceList()));
        var service = Service(kubeconfig, handler, directory);

        var first = await service.ListClusterResourcesAsync(new ResourceQuery(Kind: "\"Namespace\"", ForceRefresh: true));
        var second = await service.ListClusterResourcesAsync(new ResourceQuery(Kind: "\"Namespace\"", ForceRefresh: true));

        Assert.Empty(first.Failures);
        Assert.Empty(second.Failures);
        Assert.Equal("1", File.ReadAllText(Path.Combine(directory, "exec-count.txt")));
        Assert.Equal(2, handler.Authorizations.Count);
        Assert.All(handler.Authorizations, authorization =>
        {
            Assert.Equal("Bearer", authorization?.Scheme);
            Assert.Equal("exec-cache-token", authorization?.Parameter);
        });
    }

    [Theory]
    [InlineData("exit 7")]
    [InlineData("printf '%s' '{\"status\":{\"token\":\"   \"}}'")]
    [InlineData("printf '%s' 'not-json'")]
    public async Task Resource_service_treats_failed_exec_credentials_as_anonymous(string scriptBody)
    {
        if (OperatingSystem.IsWindows())
        {
            return;
        }

        var directory = TempDirectory();
        var script = Path.Combine(directory, "exec-token.sh");
        File.WriteAllText(script, $"""
#!/bin/sh
{scriptBody}
""");
        File.SetUnixFileMode(script, UnixFileMode.UserRead | UnixFileMode.UserExecute | UnixFileMode.UserWrite);
        var kubeconfig = Path.Combine(directory, "config.yaml");
        File.WriteAllText(kubeconfig, Kubeconfig("exec", directory));
        var handler = new RecordingHandler(_ => JsonResponse(NamespaceList()));
        var service = Service(kubeconfig, handler, directory);

        var snapshot = await service.ListClusterResourcesAsync(new ResourceQuery(Kind: "\"Namespace\"", ForceRefresh: true));

        Assert.Empty(snapshot.Failures);
        Assert.Null(Assert.Single(handler.Authorizations));
    }

    [Theory]
    [InlineData("missing-file")]
    [InlineData("empty-file")]
    [InlineData("missing-sections")]
    [InlineData("missing-token-file")]
    [InlineData("exec-missing-command")]
    [InlineData("exec-command-not-found")]
    public async Task Resource_service_treats_missing_or_unusable_auth_as_anonymous(string mode)
    {
        var directory = TempDirectory();
        var kubeconfig = Path.Combine(directory, "config.yaml");
        File.WriteAllText(kubeconfig, Kubeconfig(mode == "missing-token-file" ? "token-file" : "token", directory));
        var state = AppState.InMemory();
        state.ImportKubeconfig(kubeconfig);
        if (mode == "missing-file")
        {
            File.Delete(kubeconfig);
        }
        else if (mode == "empty-file")
        {
            File.WriteAllText(kubeconfig, " ");
        }
        else if (mode == "missing-sections")
        {
            File.WriteAllText(kubeconfig, "apiVersion: v1\n");
        }
        else if (mode == "missing-token-file")
        {
            File.Delete(Path.Combine(directory, "token.txt"));
        }
        else if (mode == "exec-missing-command")
        {
            File.WriteAllText(kubeconfig, Kubeconfig("exec-missing-command", directory));
        }
        else if (mode == "exec-command-not-found")
        {
            File.WriteAllText(kubeconfig, Kubeconfig("exec-command-not-found", directory));
        }

        var handler = new RecordingHandler(_ => JsonResponse(NamespaceList()));
        var service = new KubernetesResourceService(state, handler);

        var snapshot = await service.ListClusterResourcesAsync(new ResourceQuery(Kind: "\"Namespace\"", ForceRefresh: true));

        Assert.Empty(snapshot.Failures);
        Assert.Null(Assert.Single(handler.Authorizations));
    }

    [Fact]
    public async Task Resource_service_reports_rate_limits_and_exposes_request_telemetry()
    {
        var directory = TempDirectory();
        var kubeconfig = Path.Combine(directory, "config.yaml");
        File.WriteAllText(kubeconfig, Kubeconfig("token", directory));
        var handler = new RecordingHandler(_ =>
        {
            var response = new HttpResponseMessage(HttpStatusCode.TooManyRequests)
            {
                Content = new StringContent("slow down")
            };
            response.Headers.RetryAfter = new RetryConditionHeaderValue(TimeSpan.FromSeconds(1));
            return response;
        });
        var service = Service(kubeconfig, handler, directory);

        var snapshot = await service.ListClusterResourcesAsync(new ResourceQuery(Kind: "\"Namespace\"", ForceRefresh: true));
        var telemetry = service.RequestTelemetry();

        var failure = Assert.Single(snapshot.Failures);
        Assert.Equal("Namespace", failure.Kind);
        Assert.Equal(FreshnessState.Stale, failure.Freshness);
        Assert.Contains("rate limited", failure.Message, StringComparison.OrdinalIgnoreCase);
        Assert.True(telemetry.RequestsLastMinute >= 1);
        Assert.True(telemetry.RequestsPerSecond > 0);
        Assert.NotNull(telemetry.BackoffUntil);
        Assert.Contains(service.RequestAuditLog(), entry => entry.Status.Contains("429", StringComparison.Ordinal));
    }

    [Fact]
    public async Task Resource_service_shows_queued_requests_under_concurrent_refresh_burst()
    {
        var directory = TempDirectory();
        var kubeconfig = Path.Combine(directory, "config.yaml");
        File.WriteAllText(kubeconfig, Kubeconfig("token", directory));
        var activeRequests = 0;
        var maxActiveRequests = 0;
        var queueSamples = new List<int>();
        var stateLock = new object();
        KubernetesResourceService? service = null;
        var handler = new AsyncRecordingHandler(async (_, cancellationToken) =>
        {
            var active = Interlocked.Increment(ref activeRequests);
            lock (stateLock)
            {
                if (active > maxActiveRequests)
                {
                    maxActiveRequests = active;
                }
                queueSamples.Add(service!.RequestTelemetry().QueuedRequests);
            }

            try
            {
                await Task.Delay(120, cancellationToken).ConfigureAwait(false);
                return JsonResponse(NamespaceList());
            }
            finally
            {
                Interlocked.Decrement(ref activeRequests);
            }
        });
        service = Service(kubeconfig, handler, directory);

        var requests = Enumerable.Range(0, 8)
            .Select(_ => service.ListClusterResourcesAsync(new ResourceQuery(Kind: "\"Namespace\"", ForceRefresh: true)))
            .ToArray();
        var snapshots = await Task.WhenAll(requests);

        Assert.All(snapshots, snapshot => Assert.Empty(snapshot.Failures));
        Assert.True(maxActiveRequests <= 1, $"Expected at most one concurrent request, observed {maxActiveRequests}.");
        Assert.Contains(queueSamples, sample => sample > 0);
        Assert.True(service!.RequestTelemetry().RequestsLastMinute >= requests.Length);
    }

    [Fact]
    public async Task Configured_request_hard_limit_delays_public_kubernetes_requests()
    {
        var directory = TempDirectory();
        var kubeconfig = Path.Combine(directory, "config.yaml");
        File.WriteAllText(kubeconfig, Kubeconfig("token", directory));
        var handler = new RecordingHandler(_ => JsonResponse(NamespaceList()));
        var state = AppState.InMemoryWithConfigDirectory(Path.Combine(directory, "state"));
        state.SaveSettings(state.Settings() with { RequestHardLimitPerMinute = 600 });
        state.ImportKubeconfig(kubeconfig);
        var service = new KubernetesResourceService(state, handler);
        var query = new ResourceQuery(Kind: "\"Namespace\"", ForceRefresh: true);

        var stopwatch = Stopwatch.StartNew();
        await service.ListClusterResourcesAsync(query);
        await service.ListClusterResourcesAsync(query);
        stopwatch.Stop();

        Assert.True(
            stopwatch.Elapsed >= TimeSpan.FromMilliseconds(70),
            $"Expected hard request limit to delay second request; actual elapsed was {stopwatch.Elapsed.TotalMilliseconds:0}ms.");
        Assert.True(handler.Requests.Count >= 2);
    }

    [Fact]
    public async Task Request_audit_log_keeps_latest_256_entries()
    {
        var directory = TempDirectory();
        var kubeconfig = Path.Combine(directory, "config.yaml");
        File.WriteAllText(kubeconfig, Kubeconfig("token", directory));
        var handler = new RecordingHandler(_ => JsonResponse(NamespaceList()));
        var service = Service(kubeconfig, handler, directory);

        for (var index = 0; index < 260; index++)
        {
            await service.ListClusterResourcesAsync(new ResourceQuery(Kind: "\"Namespace\"", ForceRefresh: true));
        }

        var audit = service.RequestAuditLog();

        Assert.Equal(256, audit.Count);
        Assert.All(audit, entry => Assert.Equal("GET", entry.Method));
        Assert.Contains(audit, entry => entry.Path == "/api/v1/namespaces");
    }

    [Fact]
    public async Task Request_audit_log_tracks_success_failed_and_network_outcomes()
    {
        var directory = TempDirectory();
        var kubeconfig = Path.Combine(directory, "config.yaml");
        File.WriteAllText(kubeconfig, Kubeconfig("token", directory));
        var attempt = 0;
        var handler = new RecordingHandler(_ =>
        {
            var call = attempt++;
            return call switch
            {
                0 => JsonResponse(NamespaceList()),
                1 => new HttpResponseMessage(HttpStatusCode.BadRequest) { Content = new StringContent("invalid request") },
                _ => throw new HttpRequestException("connection reset"),
            };
        });
        var service = Service(kubeconfig, handler, directory);
        var query = new ResourceQuery(Kind: "\"Namespace\"", ForceRefresh: true);

        var first = await service.ListClusterResourcesAsync(query);
        var second = await service.ListClusterResourcesAsync(query);
        var third = await service.ListClusterResourcesAsync(query);

        Assert.Empty(first.Failures);
        Assert.Single(second.Failures);
        Assert.Single(third.Failures);
        Assert.Equal("Namespace", second.Failures[0].Kind);
        Assert.Equal("Namespace", third.Failures[0].Kind);
        Assert.Equal(FreshnessState.Stale, second.Failures[0].Freshness);
        Assert.Equal(FreshnessState.Stale, third.Failures[0].Freshness);

        var audit = service.RequestAuditLog();

        Assert.Equal(3, audit.Count);
        var outcomes = audit.Select(entry => entry.Outcome).ToHashSet(StringComparer.Ordinal);
        Assert.Contains("ok", outcomes);
        Assert.Contains("failed", outcomes);
        Assert.Contains("network error", outcomes);
    }

    [Fact]
    public async Task Request_audit_log_tracks_queued_and_running_request_states_under_burst()
    {
        var directory = TempDirectory();
        var kubeconfig = Path.Combine(directory, "config.yaml");
        File.WriteAllText(kubeconfig, Kubeconfig("token", directory));
        var queueSamples = new List<int>();
        var activeRequests = 0;
        var maxActiveRequests = 0;
        KubernetesResourceService? service = null;
        service = Service(
            kubeconfig,
            new AsyncRecordingHandler(async (_, cancellationToken) =>
            {
                var telemetry = service!.RequestTelemetry();
                queueSamples.Add(telemetry.QueuedRequests);
                var active = Interlocked.Increment(ref activeRequests);
                if (active > maxActiveRequests)
                {
                    maxActiveRequests = active;
                }

                await Task.Delay(160, cancellationToken).ConfigureAwait(false);
                Interlocked.Decrement(ref activeRequests);
                return JsonResponse(NamespaceList());
            }),
            directory);
        var query = new ResourceQuery(Kind: "\"Namespace\"", ForceRefresh: true);

        var snapshots = await Task.WhenAll(
            Enumerable.Range(0, 4).Select(_ => service.ListClusterResourcesAsync(query)));

        Assert.All(snapshots, snapshot => Assert.Empty(snapshot.Failures));
        var outcomes = service.RequestAuditLog().Select(entry => entry.Outcome).ToHashSet(StringComparer.Ordinal);
        var queuedSamples = queueSamples.Where(sample => sample > 0).ToList();

        Assert.Contains("ok", outcomes);
        Assert.All(outcomes, outcome => Assert.Equal("ok", outcome));
        Assert.True(maxActiveRequests <= 1);
        Assert.True(queuedSamples.Any());
    }

    [Fact]
    public async Task Resource_service_reports_connectivity_failures_without_throwing_from_list()
    {
        var directory = TempDirectory();
        var kubeconfig = Path.Combine(directory, "config.yaml");
        File.WriteAllText(kubeconfig, Kubeconfig("token", directory));
        var handler = new RecordingHandler(_ => throw new HttpRequestException("token failed", new InvalidOperationException("password inner")));
        var service = Service(kubeconfig, handler, directory);

        var snapshot = await service.ListClusterResourcesAsync(new ResourceQuery(Kind: "\"Namespace\"", ForceRefresh: true));

        var failure = Assert.Single(snapshot.Failures);
        Assert.Equal("Namespace", failure.Kind);
        Assert.Equal(FreshnessState.Stale, failure.Freshness);
        Assert.DoesNotContain("token", failure.Message, StringComparison.OrdinalIgnoreCase);
        Assert.DoesNotContain("password", failure.Message, StringComparison.OrdinalIgnoreCase);
        Assert.Contains("redacted", failure.Message, StringComparison.OrdinalIgnoreCase);
    }

    [Fact]
    public async Task Resource_service_rejects_missing_cluster_server_before_requests()
    {
        var directory = TempDirectory();
        var kubeconfig = Path.Combine(directory, "config.yaml");
        File.WriteAllText(kubeconfig, MissingServerKubeconfig());
        var handler = new RecordingHandler(_ => JsonResponse(NamespaceList()));
        var service = Service(kubeconfig, handler, directory);

        var error = await Assert.ThrowsAsync<PodlordException>(() =>
            service.ListClusterResourcesAsync(new ResourceQuery(Kind: "\"Namespace\"")));

        Assert.Equal(PodlordErrorKind.KubernetesConfig, error.Kind);
        Assert.Empty(handler.Requests);
        var audit = Assert.Single(service.RequestAuditLog());
        Assert.Equal("APP", audit.Method);
        Assert.Contains("client setup", audit.Path, StringComparison.OrdinalIgnoreCase);
        Assert.Contains("cluster server is missing", audit.Outcome, StringComparison.OrdinalIgnoreCase);
    }

    [Fact]
    public async Task Resource_detail_errors_redact_sensitive_response_text()
    {
        var directory = TempDirectory();
        var kubeconfig = Path.Combine(directory, "config.yaml");
        File.WriteAllText(kubeconfig, Kubeconfig("token", directory));
        var handler = new RecordingHandler(_ => new HttpResponseMessage(HttpStatusCode.InternalServerError)
        {
            Content = new StringContent("token abc password swordfish")
        });
        var service = Service(kubeconfig, handler, directory);

        var error = await Assert.ThrowsAsync<PodlordException>(() =>
            service.GetResourceDetailAsync(new ResourceIdentity(null, "Namespace", null, "default")));

        Assert.Equal(PodlordErrorKind.KubernetesApi, error.Kind);
        Assert.DoesNotContain("token", error.Message, StringComparison.OrdinalIgnoreCase);
        Assert.DoesNotContain("password", error.Message, StringComparison.OrdinalIgnoreCase);
        Assert.Contains("redacted", error.Message, StringComparison.OrdinalIgnoreCase);
    }

    [Fact]
    public async Task Resource_detail_and_logs_wrap_connectivity_failures()
    {
        var directory = TempDirectory();
        var kubeconfig = Path.Combine(directory, "config.yaml");
        File.WriteAllText(kubeconfig, Kubeconfig("token", directory));
        var handler = new RecordingHandler(_ => throw new HttpRequestException("socket password", new InvalidOperationException("token inner")));
        var service = Service(kubeconfig, handler, directory);

        var detail = await Assert.ThrowsAsync<PodlordException>(() =>
            service.GetResourceDetailAsync(new ResourceIdentity(null, "Namespace", null, "default")));
        var logs = await Assert.ThrowsAsync<PodlordException>(() =>
            service.GetPodLogsAsync(new PodLogRequest(null, "default", "api", "web", 5, false)));

        Assert.Equal(PodlordErrorKind.KubernetesApi, detail.Kind);
        Assert.Equal(PodlordErrorKind.KubernetesApi, logs.Kind);
        Assert.DoesNotContain("token", detail.Message, StringComparison.OrdinalIgnoreCase);
        Assert.DoesNotContain("password", logs.Message, StringComparison.OrdinalIgnoreCase);
    }

    [Fact]
    public async Task Pod_log_status_failures_include_container_and_previous_options()
    {
        var directory = TempDirectory();
        var kubeconfig = Path.Combine(directory, "config.yaml");
        File.WriteAllText(kubeconfig, Kubeconfig("token", directory));
        var handler = new RecordingHandler(_ => new HttpResponseMessage(HttpStatusCode.NotFound)
        {
            Content = new StringContent("missing log")
        });
        var service = Service(kubeconfig, handler, directory);

        var error = await Assert.ThrowsAsync<PodlordException>(() =>
            service.GetPodLogsAsync(new PodLogRequest(null, "default", "api", "web", 5, true)));

        Assert.Equal(PodlordErrorKind.KubernetesApi, error.Kind);
        Assert.Contains("container=web", Assert.Single(handler.Requests), StringComparison.Ordinal);
        Assert.Contains("previous=true", handler.Requests[0], StringComparison.Ordinal);
    }

    [Fact]
    public async Task Pod_logs_without_container_fetch_all_pod_containers()
    {
        var directory = TempDirectory();
        var kubeconfig = Path.Combine(directory, "config.yaml");
        File.WriteAllText(kubeconfig, Kubeconfig("token", directory));
        var handler = new RecordingHandler(request =>
        {
            var path = request.RequestUri?.PathAndQuery ?? string.Empty;
            if (path == "/api/v1/namespaces/default/pods/api/log?tailLines=10&timestamps=true")
            {
                return new HttpResponseMessage(HttpStatusCode.BadRequest)
                {
                    Content = new StringContent("""
                    {"kind":"Status","apiVersion":"v1","status":"Failure","message":"a container name must be specified for pod api, choose one of: [web sidecar]","reason":"BadRequest","code":400}
                    """)
                };
            }

            if (path == "/api/v1/namespaces/default/pods/api")
            {
                return JsonResponse("""
                {
                  "kind": "Pod",
                  "apiVersion": "v1",
                  "metadata": { "name": "api", "namespace": "default" },
                  "spec": {
                    "containers": [
                      { "name": "web", "image": "repo/web:1" },
                      { "name": "sidecar", "image": "repo/sidecar:1" }
                    ]
                  }
                }
                """);
            }

            if (path.Contains("/log?", StringComparison.Ordinal) && path.Contains("container=web", StringComparison.Ordinal))
            {
                return new HttpResponseMessage(HttpStatusCode.OK)
                {
                    Content = new StringContent("web-line\n")
                };
            }

            if (path.Contains("/log?", StringComparison.Ordinal) && path.Contains("container=sidecar", StringComparison.Ordinal))
            {
                return new HttpResponseMessage(HttpStatusCode.OK)
                {
                    Content = new StringContent("sidecar-line\n")
                };
            }

            throw new InvalidOperationException($"Unexpected path {path}");
        });
        var service = Service(kubeconfig, handler, directory);

        var logs = await service.GetPodLogsAsync(new PodLogRequest(null, "default", "api", null, 10, false));

        Assert.Contains("===== container: web =====", logs.Text, StringComparison.Ordinal);
        Assert.Contains("web-line", logs.Text, StringComparison.Ordinal);
        Assert.Contains("===== container: sidecar =====", logs.Text, StringComparison.Ordinal);
        Assert.Contains("sidecar-line", logs.Text, StringComparison.Ordinal);
        Assert.Contains(handler.Requests, path => path == "/api/v1/namespaces/default/pods/api");
        Assert.Contains(handler.Requests, path => path.Contains("container=web", StringComparison.Ordinal));
        Assert.Contains(handler.Requests, path => path.Contains("container=sidecar", StringComparison.Ordinal));
    }

    [Fact]
    public async Task Resource_service_validates_detail_and_log_inputs_before_requests()
    {
        var directory = TempDirectory();
        var kubeconfig = Path.Combine(directory, "config.yaml");
        File.WriteAllText(kubeconfig, Kubeconfig("token", directory));
        var handler = new RecordingHandler(_ => JsonResponse(NamespaceList()));
        var service = Service(kubeconfig, handler, directory);

        Assert.Equal(PodlordErrorKind.InvalidInput, (await Assert.ThrowsAsync<PodlordException>(() =>
            service.GetResourceDetailAsync(new ResourceIdentity(null, "", null, "x")))).Kind);
        Assert.Equal(PodlordErrorKind.InvalidInput, (await Assert.ThrowsAsync<PodlordException>(() =>
            service.GetResourceDetailAsync(new ResourceIdentity(null, "Namespace", null, "")))).Kind);
        Assert.Equal(PodlordErrorKind.UnsupportedResourceKind, (await Assert.ThrowsAsync<PodlordException>(() =>
            service.GetResourceDetailAsync(new ResourceIdentity(null, "Widget", null, "x")))).Kind);
        Assert.Equal(PodlordErrorKind.InvalidInput, (await Assert.ThrowsAsync<PodlordException>(() =>
            service.GetResourceDetailAsync(new ResourceIdentity(null, "Pod", null, "api")))).Kind);
        Assert.Equal(PodlordErrorKind.InvalidInput, (await Assert.ThrowsAsync<PodlordException>(() =>
            service.GetPodLogsAsync(new PodLogRequest(null, "", "pod", null, 10, false)))).Kind);
        Assert.Equal(PodlordErrorKind.InvalidInput, (await Assert.ThrowsAsync<PodlordException>(() =>
            service.GetPodLogsAsync(new PodLogRequest(null, "default", "", null, 10, false)))).Kind);
        Assert.Empty(handler.Requests);
    }

    [Fact]
    public async Task Resource_service_uses_namespaced_list_paths_for_core_and_group_apis()
    {
        var directory = TempDirectory();
        var kubeconfig = Path.Combine(directory, "config.yaml");
        File.WriteAllText(kubeconfig, Kubeconfig("token", directory));
        var handler = new RecordingHandler(request => JsonResponse(ListForPath(request.RequestUri?.AbsolutePath ?? string.Empty)));
        var service = Service(kubeconfig, handler, directory);

        var pods = await service.ListClusterResourcesAsync(new ResourceQuery(Kind: "\"Pod\"", Namespace: "\"payments\"", ForceRefresh: true));
        var deployments = await service.ListClusterResourcesAsync(new ResourceQuery(Kind: "\"Deployment\"", Namespace: "\"payments\"", ForceRefresh: true));

        Assert.Empty(pods.Failures);
        Assert.Empty(deployments.Failures);
        Assert.Contains("/api/v1/namespaces/payments/pods", handler.Requests);
        Assert.Contains("/apis/apps/v1/namespaces/payments/deployments", handler.Requests);
    }

    [Fact]
    public async Task Resource_service_uses_cached_list_rows_and_problem_default_scan()
    {
        var directory = TempDirectory();
        var kubeconfig = Path.Combine(directory, "config.yaml");
        File.WriteAllText(kubeconfig, Kubeconfig("token", directory));
        var handler = new RecordingHandler(request => JsonResponse(ListForPath(request.RequestUri?.AbsolutePath ?? string.Empty)));
        var service = Service(kubeconfig, handler, directory);

        var first = await service.ListClusterResourcesAsync(new ResourceQuery(Kind: "\"Pod\"", Namespace: "\"payments\"", ForceRefresh: true));
        var afterFirst = handler.Requests.Count;
        var second = await service.ListClusterResourcesAsync(new ResourceQuery(Kind: "\"Pod\"", Namespace: "\"payments\""));

        Assert.Single(first.Rows);
        Assert.Single(second.Rows);
        Assert.Equal(afterFirst, handler.Requests.Count);

        var problems = await service.ListClusterResourcesAsync(new ResourceQuery(ProblemsOnly: true, Limit: 5, ForceRefresh: true));

        Assert.Empty(problems.Failures);
    }

    [Fact]
    public async Task Resource_service_enriches_pods_with_metrics_server_pulse()
    {
        var directory = TempDirectory();
        var kubeconfig = Path.Combine(directory, "config.yaml");
        File.WriteAllText(kubeconfig, Kubeconfig("token", directory));
        var handler = new RecordingHandler(request =>
        {
            var path = request.RequestUri?.AbsolutePath ?? string.Empty;
            return path switch
            {
                "/api/v1/namespaces/payments/pods" => JsonResponse(PodListWithResources()),
                "/apis/metrics.k8s.io/v1beta1/pods" => JsonResponse(PodMetricsList()),
                "/apis/metrics.k8s.io/v1beta1/nodes" => JsonResponse("""{"items":[]}"""),
                _ => JsonResponse("""{"items":[]}""")
            };
        });
        var service = Service(kubeconfig, handler, directory);

        var snapshot = await service.ListClusterResourcesAsync(new ResourceQuery(Kind: "\"Pod\"", Namespace: "\"payments\"", ForceRefresh: true));

        var row = Assert.Single(snapshot.Rows);
        Assert.Empty(snapshot.Failures);
        Assert.Equal("125m", row.CpuDisplay);
        Assert.Equal("128Mi", row.MemoryDisplay);
        Assert.Equal("25%", row.CpuPercentDisplay);
        Assert.Equal("50%", row.MemoryPercentDisplay);
        Assert.Equal("API LIVE", row.MetricSourceBadge);
        Assert.Contains("/apis/metrics.k8s.io/v1beta1/pods", handler.Requests);
    }

    [Fact]
    public async Task Resource_service_falls_back_to_namespace_scoped_pod_metrics_when_global_pod_metrics_are_forbidden()
    {
        var directory = TempDirectory();
        var kubeconfig = Path.Combine(directory, "config.yaml");
        File.WriteAllText(kubeconfig, Kubeconfig("token", directory));
        var handler = new RecordingHandler(request =>
        {
            var path = request.RequestUri?.AbsolutePath ?? string.Empty;
            return path switch
            {
                "/api/v1/namespaces/payments/pods" => JsonResponse(PodListWithResources()),
                "/apis/metrics.k8s.io/v1beta1/pods" => new HttpResponseMessage(HttpStatusCode.Forbidden)
                {
                    Content = new StringContent("""{"kind":"Status","status":"Failure","reason":"Forbidden","message":"forbidden"}""")
                },
                "/apis/metrics.k8s.io/v1beta1/namespaces/payments/pods" => JsonResponse(PodMetricsList()),
                "/apis/metrics.k8s.io/v1beta1/nodes" => JsonResponse("""{"items":[]}"""),
                _ => JsonResponse("""{"items":[]}""")
            };
        });
        var service = Service(kubeconfig, handler, directory);

        var snapshot = await service.ListClusterResourcesAsync(new ResourceQuery(Kind: "\"Pod\"", Namespace: "\"payments\"", ForceRefresh: true));

        var row = Assert.Single(snapshot.Rows);
        Assert.Empty(snapshot.Failures);
        Assert.Equal("125m", row.CpuDisplay);
        Assert.Equal("128Mi", row.MemoryDisplay);
        Assert.Equal("API LIVE", row.MetricSourceBadge);
        Assert.Contains("/apis/metrics.k8s.io/v1beta1/pods", handler.Requests);
        Assert.Contains("/apis/metrics.k8s.io/v1beta1/namespaces/payments/pods", handler.Requests);
        Assert.Contains("namespace-scoped", row.MetricTooltip, StringComparison.OrdinalIgnoreCase);
    }

    [Fact]
    public async Task Resource_service_uses_namespace_pod_metrics_when_global_pod_metrics_are_empty()
    {
        var directory = TempDirectory();
        var kubeconfig = Path.Combine(directory, "config.yaml");
        File.WriteAllText(kubeconfig, Kubeconfig("token", directory));
        var handler = new RecordingHandler(request =>
        {
            var path = request.RequestUri?.AbsolutePath ?? string.Empty;
            return path switch
            {
                "/api/v1/namespaces/payments/pods" => JsonResponse(PodListWithResources()),
                "/apis/metrics.k8s.io/v1beta1/pods" => JsonResponse("""{"items":[]}"""),
                "/apis/metrics.k8s.io/v1beta1/namespaces/payments/pods" => JsonResponse(PodMetricsList()),
                "/apis/metrics.k8s.io/v1beta1/nodes" => JsonResponse(NodeMetricsList()),
                _ => JsonResponse("""{"items":[]}""")
            };
        });
        var service = Service(kubeconfig, handler, directory);

        var snapshot = await service.ListClusterResourcesAsync(new ResourceQuery(Kind: "\"Pod\"", Namespace: "\"payments\"", ForceRefresh: true));
        var cached = service.GetCachedResourceSnapshot(new ResourceQuery(Kind: "\"Pod\"", Namespace: "\"payments\""));

        var row = Assert.Single(snapshot.Rows);
        var cachedRow = Assert.Single(cached.Rows);
        Assert.Equal("125m", row.CpuDisplay);
        Assert.Equal("128Mi", row.MemoryDisplay);
        Assert.Equal("125m", cachedRow.CpuDisplay);
        Assert.Equal("128Mi", cachedRow.MemoryDisplay);
        Assert.Equal("API LIVE", row.MetricSourceBadge);
        Assert.Contains("/apis/metrics.k8s.io/v1beta1/pods", handler.Requests);
        Assert.Contains("/apis/metrics.k8s.io/v1beta1/namespaces/payments/pods", handler.Requests);
        Assert.Contains("returned no pod usage", row.MetricTooltip, StringComparison.OrdinalIgnoreCase);
        Assert.Contains("namespace-scoped", row.MetricTooltip, StringComparison.OrdinalIgnoreCase);
    }

    [Fact]
    public async Task Resource_service_keeps_metrics_neutral_when_metrics_server_is_unavailable()
    {
        var directory = TempDirectory();
        var kubeconfig = Path.Combine(directory, "config.yaml");
        File.WriteAllText(kubeconfig, Kubeconfig("token", directory));
        var handler = new RecordingHandler(request =>
        {
            var path = request.RequestUri?.AbsolutePath ?? string.Empty;
            if (path.StartsWith("/apis/metrics.k8s.io/", StringComparison.Ordinal))
            {
                return new HttpResponseMessage(HttpStatusCode.NotFound) { Content = new StringContent("metrics api not installed") };
            }

            return path == "/api/v1/namespaces/payments/pods"
                ? JsonResponse(PodListWithResources())
                : JsonResponse("""{"items":[]}""");
        });
        var service = Service(kubeconfig, handler, directory);

        var snapshot = await service.ListClusterResourcesAsync(new ResourceQuery(Kind: "\"Pod\"", Namespace: "\"payments\"", ForceRefresh: true));

        var row = Assert.Single(snapshot.Rows);
        Assert.Empty(snapshot.Failures);
        Assert.Equal("-", row.CpuDisplay);
        Assert.Equal("-", row.MemoryDisplay);
        Assert.Equal("-", row.CpuPercentDisplay);
        Assert.Equal("API", row.MetricSourceBadge);
        Assert.Contains("metrics unavailable", row.MetricTooltip, StringComparison.OrdinalIgnoreCase);
    }

    [Fact]
    public async Task Related_events_return_empty_when_event_api_is_forbidden()
    {
        var directory = TempDirectory();
        var kubeconfig = Path.Combine(directory, "config.yaml");
        File.WriteAllText(kubeconfig, Kubeconfig("token", directory));
        var handler = new RecordingHandler(request =>
        {
            var pathAndQuery = request.RequestUri?.PathAndQuery ?? string.Empty;
            if (pathAndQuery.Contains("/events?fieldSelector=", StringComparison.Ordinal))
            {
                return new HttpResponseMessage(HttpStatusCode.Forbidden) { Content = new StringContent("no events") };
            }

            return JsonResponse(PodObject("api-pod"));
        });
        var service = Service(kubeconfig, handler, directory);

        var detail = await service.GetResourceDetailAsync(new ResourceIdentity(null, "Pod", "payments", "api-pod"), forceRefresh: true, KubernetesRequestPriority.Foreground);

        Assert.Empty(detail.Events);
    }

    [Fact]
    public async Task Resource_service_supports_detail_paths_for_all_declared_resource_kinds()
    {
        var directory = TempDirectory();
        var kubeconfig = Path.Combine(directory, "config.yaml");
        File.WriteAllText(kubeconfig, Kubeconfig("token", directory));
        var handler = new RecordingHandler(request =>
        {
            var path = request.RequestUri?.AbsolutePath ?? string.Empty;
            if ((request.RequestUri?.PathAndQuery ?? string.Empty).Contains("/events?fieldSelector=", StringComparison.Ordinal))
            {
                return JsonResponse("""{"items":[{"type":"Normal","reason":"Seen","message":"ok","count":2,"lastTimestamp":"2026-06-10T08:02:00Z"}]}""");
            }

            return JsonResponse(ObjectForPath(path));
        });
        var service = Service(kubeconfig, handler, directory);
        var identities = new[]
        {
            new ResourceIdentity(null, "Namespace", null, "payments"),
            new ResourceIdentity(null, "Node", null, "node-a"),
            new ResourceIdentity(null, "Pod", "payments", "api-pod"),
            new ResourceIdentity(null, "Service", "payments", "api"),
            new ResourceIdentity(null, "ConfigMap", "payments", "cfg"),
            new ResourceIdentity(null, "Secret", "payments", "secret"),
            new ResourceIdentity(null, "PersistentVolume", null, "pv-a"),
            new ResourceIdentity(null, "PersistentVolumeClaim", "payments", "pvc-a"),
            new ResourceIdentity(null, "ServiceAccount", "payments", "sa-a"),
            new ResourceIdentity(null, "Event", "payments", "event-a"),
            new ResourceIdentity(null, "Deployment", "payments", "deploy-a"),
            new ResourceIdentity(null, "ReplicaSet", "payments", "rs-a"),
            new ResourceIdentity(null, "StatefulSet", "payments", "sts-a"),
            new ResourceIdentity(null, "DaemonSet", "payments", "ds-a"),
            new ResourceIdentity(null, "Job", "payments", "job-a"),
            new ResourceIdentity(null, "CronJob", "payments", "cron-a"),
            new ResourceIdentity(null, "Ingress", "payments", "ing-a"),
            new ResourceIdentity(null, "NetworkPolicy", "payments", "net-a"),
            new ResourceIdentity(null, "EndpointSlice", "payments", "slice-a"),
            new ResourceIdentity(null, "Gateway", "payments", "gw-a"),
            new ResourceIdentity(null, "GatewayClass", null, "gwc-a"),
            new ResourceIdentity(null, "HTTPRoute", "payments", "route-a"),
            new ResourceIdentity(null, "GRPCRoute", "payments", "grpc-a"),
            new ResourceIdentity(null, "CustomResourceDefinition", null, "widgets.example.com")
        };

        var details = new List<ResourceDetail>();
        foreach (var identity in identities)
        {
            details.Add(await service.GetResourceDetailAsync(identity, forceRefresh: true, KubernetesRequestPriority.Foreground));
        }

        Assert.Equal(identities.Length, details.Count);
        Assert.Contains(details, detail => detail.Identity.Kind == "Pod" && detail.Status == "Running");
        Assert.Contains(details, detail => detail.Identity.Kind == "Node" && detail.Status == "NotReady");
        Assert.Contains(details, detail => detail.Identity.Kind == "CronJob" && detail.Status == "Suspended");
        Assert.Contains(details, detail => detail.Identity.Kind == "ConfigMap" && detail.Summary.Any(item => item.Label == "Image" && item.Value == "2 keys"));
        var configMap = Assert.Single(details, detail => detail.Identity.Kind == "ConfigMap");
        Assert.Equal(["a", "b", "blob"], configMap.Values.Select(value => value.Key).ToArray());
        Assert.All(configMap.Values, value => Assert.False(value.Sensitive));
        Assert.True(Assert.Single(configMap.Values, value => value.Key == "blob").Base64Encoded);
        var secret = Assert.Single(details, detail => detail.Identity.Kind == "Secret");
        var secretValue = Assert.Single(secret.Values);
        Assert.Equal("password", secretValue.Key);
        Assert.Equal("c3dvcmQ=", secretValue.Value);
        Assert.True(secretValue.Sensitive);
        Assert.True(secretValue.Base64Encoded);
        Assert.DoesNotContain("c3dvcmQ=", secret.Yaml, StringComparison.Ordinal);
        Assert.Contains(handler.Requests, path => path == "/apis/gateway.networking.k8s.io/v1/gatewayclasses/gwc-a");
    }

    [Fact]
    public async Task Resource_service_detail_and_log_cache_return_cached_data_without_second_request()
    {
        var directory = TempDirectory();
        var kubeconfig = Path.Combine(directory, "config.yaml");
        File.WriteAllText(kubeconfig, Kubeconfig("token", directory));
        var handler = new RecordingHandler(request =>
        {
            var path = request.RequestUri?.AbsolutePath ?? string.Empty;
            if (path.EndsWith("/events", StringComparison.Ordinal))
            {
                return JsonResponse("""{"items":[]}""");
            }

            if (path.EndsWith("/log", StringComparison.Ordinal))
            {
                return new HttpResponseMessage(HttpStatusCode.OK)
                {
                    Content = new StringContent("log-line")
                };
            }

            return JsonResponse(NamespaceObject());
        });
        var service = Service(kubeconfig, handler, directory);

        var identity = new ResourceIdentity(null, "Namespace", null, "default");
        var request = new PodLogRequest(null, "default", "api", null, 0, true);

        Assert.Null(service.GetCachedResourceDetail(identity));
        Assert.Null(service.GetCachedPodLogs(request));
        var detail = await service.GetResourceDetailAsync(identity);
        var cachedDetail = await service.GetResourceDetailAsync(identity);
        var logs = await service.GetPodLogsAsync(request);
        var cachedLogs = await service.GetPodLogsAsync(request);

        Assert.Same(detail, cachedDetail);
        Assert.Same(logs, cachedLogs);
        Assert.Same(detail, service.GetCachedResourceDetail(identity));
        Assert.Same(logs, service.GetCachedPodLogs(request));
        Assert.Equal(1, logs.TailLines);
        Assert.Equal(2, handler.Requests.Count);
    }

    [Fact]
    public async Task Resource_service_applies_yaml_with_server_side_apply_patch()
    {
        var directory = TempDirectory();
        var kubeconfig = Path.Combine(directory, "config.yaml");
        File.WriteAllText(kubeconfig, Kubeconfig("token", directory));
        var handler = new RecordingHandler(request =>
        {
            var path = request.RequestUri?.PathAndQuery ?? string.Empty;
            if (request.Method == HttpMethod.Patch)
            {
                return JsonResponse(NamespaceObject());
            }

            if (path.Contains("/events?fieldSelector=", StringComparison.Ordinal))
            {
                return JsonResponse("""{"items":[]}""");
            }

            return JsonResponse(NamespaceObject());
        });
        var service = Service(kubeconfig, handler, directory);

        var detail = await service.ApplyResourceYamlAsync(
            new ResourceIdentity(null, "Namespace", null, "default"),
            "apiVersion: v1\nkind: Namespace\nmetadata:\n  name: default\n");

        Assert.Equal("Namespace", detail.Identity.Kind);
        Assert.Contains("kind: Namespace", detail.Yaml, StringComparison.Ordinal);
        Assert.Contains("metadata:", detail.Yaml, StringComparison.Ordinal);
        Assert.Contains("name: default", detail.Yaml, StringComparison.Ordinal);
        Assert.DoesNotContain("ValueKind", detail.Yaml, StringComparison.Ordinal);
        Assert.Contains(HttpMethod.Patch.Method, handler.Methods);
        Assert.Contains("/api/v1/namespaces/default?fieldManager=podlord", handler.Requests);
        Assert.Contains("application/apply-patch+yaml", handler.ContentTypes);
        Assert.Contains("kind: Namespace", handler.Bodies.Single(body => body.Contains("kind: Namespace", StringComparison.Ordinal)));
        Assert.Contains(service.RequestAuditLog(), entry => entry.Method == "PATCH" && entry.Status.Contains("200", StringComparison.Ordinal));
    }

    [Fact]
    public async Task Delete_resource_uses_kubernetes_delete_and_removes_cached_row()
    {
        var directory = TempDirectory();
        var kubeconfig = Path.Combine(directory, "config.yaml");
        File.WriteAllText(kubeconfig, Kubeconfig("token", directory));
        var handler = new RecordingHandler(request =>
        {
            if (request.Method == HttpMethod.Delete)
            {
                return JsonResponse("""{"kind":"Status","apiVersion":"v1","status":"Success"}""");
            }

            return JsonResponse(NamespaceList());
        });
        var service = Service(kubeconfig, handler, directory);

        await service.ListClusterResourcesAsync(new ResourceQuery(Kind: "\"Namespace\"", ForceRefresh: true));
        Assert.Contains(service.GetCachedResourceSnapshot(new ResourceQuery(Kind: "\"Namespace\"")).Rows, row => row.Name == "default");

        await service.DeleteResourceAsync(new ResourceIdentity(null, "Namespace", null, "default"));

        Assert.Contains(HttpMethod.Delete.Method, handler.Methods);
        Assert.Contains("/api/v1/namespaces/default", handler.Requests);
        Assert.DoesNotContain(service.GetCachedResourceSnapshot(new ResourceQuery(Kind: "\"Namespace\"")).Rows, row => row.Name == "default");
        Assert.Contains(service.RequestAuditLog(), entry => entry.Method == "DELETE" && entry.Status.Contains("200", StringComparison.Ordinal));
    }

    [Fact]
    public async Task Cached_resource_snapshot_can_be_filtered_or_return_unfiltered_rows()
    {
        var directory = TempDirectory();
        var kubeconfig = Path.Combine(directory, "config.yaml");
        File.WriteAllText(kubeconfig, Kubeconfig("token", directory));
        var handler = new RecordingHandler(_ => JsonResponse(NamespaceList()));
        var service = Service(kubeconfig, handler, directory);

        await service.ListClusterResourcesAsync(new ResourceQuery(Kind: "\"Namespace\"", ForceRefresh: true));
        var filtered = service.GetCachedResourceSnapshot(new ResourceQuery(Kind: "\"Namespace\"", Search: "no-match"));
        var unfiltered = service.GetCachedResourceSnapshot(new ResourceQuery(Kind: "\"Namespace\"", Search: "no-match"), applyFilters: false);

        Assert.Empty(filtered.Rows);
        Assert.Single(unfiltered.Rows);
    }

    [Fact]
    public async Task Optional_missing_apis_are_ignored_but_forbidden_apis_are_reported()
    {
        var directory = TempDirectory();
        var kubeconfig = Path.Combine(directory, "config.yaml");
        File.WriteAllText(kubeconfig, Kubeconfig("token", directory));
        var handler = new RecordingHandler(request =>
        {
            var path = request.RequestUri?.AbsolutePath ?? string.Empty;
            if (path.Contains("gateway.networking.k8s.io", StringComparison.Ordinal))
            {
                return new HttpResponseMessage(HttpStatusCode.NotFound) { Content = new StringContent("missing") };
            }

            if (path.EndsWith("/nodes", StringComparison.Ordinal))
            {
                return new HttpResponseMessage(HttpStatusCode.Forbidden) { Content = new StringContent("nope") };
            }

            return JsonResponse("""{"items":[]}""");
        });
        var service = Service(kubeconfig, handler, directory);

        var snapshot = await service.ListClusterResourcesAsync(new ResourceQuery(Kind: "\"Gateway\" \"GatewayClass\" \"HTTPRoute\" \"GRPCRoute\" \"Node\"", ForceRefresh: true));

        Assert.DoesNotContain(snapshot.Failures, failure => failure.Kind.Contains("Gateway", StringComparison.Ordinal));
        var failure = Assert.Single(snapshot.Failures);
        Assert.Equal("Node", failure.Kind);
        Assert.Equal(FreshnessState.Forbidden, failure.Freshness);
    }

    private static KubernetesResourceService Service(string kubeconfig, HttpMessageHandler handler, string directory)
    {
        var state = AppState.InMemoryWithConfigDirectory(Path.Combine(directory, "state"));
        state.ImportKubeconfig(kubeconfig);
        return new KubernetesResourceService(state, handler);
    }

    private static string MissingServerKubeconfig()
    {
        return """
apiVersion: v1
clusters:
- name: dev
  cluster: {}
contexts:
- name: dev
  context:
    cluster: dev
    user: dev
users:
- name: dev
  user:
    token: token
""";
    }

    private static string CertificateKubeconfig()
    {
        using var rsa = RSA.Create(2048);
        var request = new CertificateRequest("CN=podlord-test", rsa, HashAlgorithmName.SHA256, RSASignaturePadding.Pkcs1);
        using var certificate = request.CreateSelfSigned(DateTimeOffset.UtcNow.AddDays(-1), DateTimeOffset.UtcNow.AddDays(1));
        var certificateData = Convert.ToBase64String(Encoding.UTF8.GetBytes(certificate.ExportCertificatePem()));
        var keyData = Convert.ToBase64String(Encoding.UTF8.GetBytes(rsa.ExportRSAPrivateKeyPem()));
        return $$"""
apiVersion: v1
clusters:
- name: dev
  cluster:
    server: https://127.0.0.1:6443
    certificate-authority-data: {{certificateData}}
contexts:
- name: dev
  context:
    cluster: dev
    user: dev
users:
- name: dev
  user:
    client-certificate-data: {{certificateData}}
    client-key-data: {{keyData}}
""";
    }

    private static string CertificateFileKubeconfig(string directory)
    {
        using var rsa = RSA.Create(2048);
        var request = new CertificateRequest("CN=podlord-test", rsa, HashAlgorithmName.SHA256, RSASignaturePadding.Pkcs1);
        using var certificate = request.CreateSelfSigned(DateTimeOffset.UtcNow.AddDays(-1), DateTimeOffset.UtcNow.AddDays(1));
        File.WriteAllText(Path.Combine(directory, "ca.pem"), certificate.ExportCertificatePem());
        File.WriteAllText(Path.Combine(directory, "client.pem"), certificate.ExportCertificatePem());
        File.WriteAllText(Path.Combine(directory, "client.key"), rsa.ExportRSAPrivateKeyPem());
        return """
apiVersion: v1
clusters:
- name: dev
  cluster:
    server: https://127.0.0.1:6443
    certificate-authority: ca.pem
contexts:
- name: dev
  context:
    cluster: dev
    user: dev
users:
- name: dev
  user:
    client-certificate: client.pem
    client-key: client.key
""";
    }

    private static HttpResponseMessage JsonResponse(string json)
    {
        return new HttpResponseMessage(HttpStatusCode.OK)
        {
            Content = new StringContent(json, Encoding.UTF8, "application/json")
        };
    }

    private static string NamespaceList()
    {
        return """
{
  "items": [
    {
      "metadata": {
        "name": "default",
        "uid": "ns-1",
        "creationTimestamp": "2026-06-10T08:00:00Z"
      },
      "status": {
        "phase": "Active"
      }
    }
  ]
}
""";
    }

    private static string ListForPath(string path)
    {
        var kind = path.Contains("deployments", StringComparison.Ordinal) ? "Deployment" : "Pod";
        var item = kind == "Deployment" ? WorkloadObject("Deployment", "deploy-a") : PodObject("api-pod");
        return $$"""
{
  "items": [
    {{item}}
  ]
}
""";
    }

    private static string PodListWithResources()
    {
        return $$"""
{
  "items": [
    {
      "apiVersion": "v1",
      "kind": "Pod",
      "metadata": {
        "name": "api-pod",
        "namespace": "payments",
        "uid": "uid-api-pod",
        "creationTimestamp": "2026-06-10T08:00:00Z"
      },
      "spec": {
        "nodeName": "node-a",
        "containers": [
          {
            "name": "api",
            "image": "registry.example.com/team/api:1.0",
            "resources": {
              "limits": { "cpu": "500m", "memory": "256Mi" },
              "requests": { "cpu": "100m", "memory": "128Mi" }
            }
          }
        ]
      },
      "status": {
        "phase": "Running",
        "containerStatuses": [{ "name": "api", "ready": true, "restartCount": 0, "state": { "running": {} } }]
      }
    }
  ]
}
""";
    }

    private static string PodMetricsList()
    {
        return """
{
  "items": [
    {
      "metadata": { "name": "api-pod", "namespace": "payments" },
      "timestamp": "2026-06-10T08:00:10Z",
      "containers": [
        { "name": "api", "usage": { "cpu": "125m", "memory": "128Mi" } }
      ]
    }
  ]
}
""";
    }

    private static string NodeMetricsList()
    {
        return """
{
  "items": [
    {
      "metadata": { "name": "node-a" },
      "timestamp": "2026-06-10T08:00:10Z",
      "usage": { "cpu": "900m", "memory": "2Gi" }
    }
  ]
}
""";
    }

    private static string ObjectForPath(string path)
    {
        if (path.Contains("/pods/", StringComparison.Ordinal)) { return PodObject(Path.GetFileName(path)); }
        if (path.Contains("/nodes/", StringComparison.Ordinal)) { return NodeObject(Path.GetFileName(path)); }
        if (path.Contains("/deployments/", StringComparison.Ordinal)) { return WorkloadObject("Deployment", Path.GetFileName(path)); }
        if (path.Contains("/replicasets/", StringComparison.Ordinal)) { return WorkloadObject("ReplicaSet", Path.GetFileName(path)); }
        if (path.Contains("/statefulsets/", StringComparison.Ordinal)) { return WorkloadObject("StatefulSet", Path.GetFileName(path)); }
        if (path.Contains("/daemonsets/", StringComparison.Ordinal)) { return WorkloadObject("DaemonSet", Path.GetFileName(path)); }
        if (path.Contains("/jobs/", StringComparison.Ordinal)) { return WorkloadObject("Job", Path.GetFileName(path), failed: true); }
        if (path.Contains("/cronjobs/", StringComparison.Ordinal)) { return CronJobObject(Path.GetFileName(path)); }
        if (path.Contains("/configmaps/", StringComparison.Ordinal)) { return ConfigMapObject(Path.GetFileName(path)); }
        if (path.Contains("/secrets/", StringComparison.Ordinal)) { return SecretObject(Path.GetFileName(path)); }
        if (path.Contains("/events/", StringComparison.Ordinal)) { return EventObject(Path.GetFileName(path)); }
        if (path.Contains("/services/", StringComparison.Ordinal)) { return TypedObject(Path.GetFileName(path), "Service", "payments", extra: "\"spec\":{\"type\":\"ClusterIP\"}"); }
        if (path.Contains("/namespaces/", StringComparison.Ordinal)) { return TypedObject(Path.GetFileName(path), "Namespace", null, extra: "\"status\":{\"phase\":\"Active\"}"); }
        if (path.Contains("/persistentvolumes/", StringComparison.Ordinal)) { return TypedObject(Path.GetFileName(path), "PersistentVolume", null, extra: "\"status\":{\"phase\":\"Available\"}"); }
        if (path.Contains("/persistentvolumeclaims/", StringComparison.Ordinal)) { return TypedObject(Path.GetFileName(path), "PersistentVolumeClaim", "payments", extra: "\"status\":{\"phase\":\"Bound\"}"); }
        if (path.Contains("/serviceaccounts/", StringComparison.Ordinal)) { return TypedObject(Path.GetFileName(path), "ServiceAccount", "payments"); }
        if (path.Contains("/customresourcedefinitions/", StringComparison.Ordinal)) { return TypedObject(Path.GetFileName(path), "CustomResourceDefinition", null); }

        return TypedObject(Path.GetFileName(path), "Observed", path.Contains("namespaces", StringComparison.Ordinal) ? "payments" : null);
    }

    private static string PodObject(string name)
    {
        return $$"""
{
  "apiVersion": "v1",
  "kind": "Pod",
  "metadata": {
    "name": "{{name}}",
    "namespace": "payments",
    "uid": "uid-{{name}}",
    "creationTimestamp": "2026-06-10T08:00:00Z",
    "ownerReferences": [{ "kind": "ReplicaSet", "name": "api-rs" }],
    "managedFields": [{ "time": "2026-06-10T08:01:00Z" }]
  },
  "spec": {
    "nodeName": "node-a",
    "containers": [{ "name": "api", "image": "registry.example.com/team/api:1.0" }]
  },
  "status": {
    "phase": "Running",
    "containerStatuses": [{ "name": "api", "ready": true, "restartCount": 0, "state": { "running": {} } }]
  }
}
""";
    }

    private static string NodeObject(string name)
    {
        return $$"""
{
  "metadata": {
    "name": "{{name}}",
    "uid": "uid-{{name}}",
    "creationTimestamp": "2026-06-10T08:00:00Z"
  },
  "status": {
    "conditions": [{ "type": "Ready", "status": "False", "reason": "KubeletDown" }]
  }
}
""";
    }

    private static string WorkloadObject(string kind, string name, bool failed = false)
    {
        var status = kind == "Job"
            ? failed ? "\"status\":{\"failed\":1}" : "\"status\":{\"succeeded\":1}"
            : "\"status\":{\"availableReplicas\":2,\"readyReplicas\":2}";
        return $$"""
{
  "metadata": {
    "name": "{{name}}",
    "namespace": "payments",
    "uid": "uid-{{name}}",
    "creationTimestamp": "2026-06-10T08:00:00Z"
  },
  "spec": {
    "replicas": 2,
    "template": { "spec": { "containers": [{ "name": "api", "image": "repo/{{name}}:2.0" }] } }
  },
  {{status}}
}
""";
    }

    private static string CronJobObject(string name)
    {
        return $$"""
{
  "metadata": {
    "name": "{{name}}",
    "namespace": "payments",
    "uid": "uid-{{name}}",
    "creationTimestamp": "2026-06-10T08:00:00Z"
  },
  "spec": {
    "suspend": true,
    "jobTemplate": { "spec": { "template": { "spec": { "containers": [{ "name": "cron", "image": "repo/cron:1" }] } } } }
  }
}
""";
    }

    private static string ConfigMapObject(string name)
    {
        return $$"""
{
  "metadata": {
    "name": "{{name}}",
    "namespace": "payments",
    "uid": "uid-{{name}}",
    "creationTimestamp": "2026-06-10T08:00:00Z"
  },
  "data": {
    "a": "1",
    "b": "2"
  },
  "binaryData": {
    "blob": "aGVsbG8="
  }
}
""";
    }

    private static string SecretObject(string name)
    {
        return $$"""
{
  "metadata": {
    "name": "{{name}}",
    "namespace": "payments",
    "uid": "uid-{{name}}",
    "creationTimestamp": "2026-06-10T08:00:00Z"
  },
  "data": {
    "password": "c3dvcmQ="
  }
}
""";
    }

    private static string EventObject(string name)
    {
        return $$"""
{
  "metadata": {
    "name": "{{name}}",
    "namespace": "payments",
    "uid": "uid-{{name}}",
    "creationTimestamp": "2026-06-10T08:00:00Z"
  },
  "type": "Warning",
  "reason": "BackOff",
  "message": "retrying"
}
""";
    }

    private static string TypedObject(string name, string kind, string? ns, string? extra = null)
    {
        var namespaceLine = ns is null ? string.Empty : $"""
    "namespace": "{ns}",
""";
        var suffix = string.IsNullOrWhiteSpace(extra) ? string.Empty : $",\n  {extra}";
        return $$"""
{
  "metadata": {
    "name": "{{name}}",
{{namespaceLine}}    "uid": "uid-{{name}}",
    "creationTimestamp": "2026-06-10T08:00:00Z"
  }{{suffix}}
}
""";
    }

    private static string NamespaceObject()
    {
        return """
{
  "apiVersion": "v1",
  "kind": "Namespace",
  "metadata": {
    "name": "default",
    "uid": "ns-1",
    "creationTimestamp": "2026-06-10T08:00:00Z",
    "managedFields": [
      { "time": "2026-06-10T08:01:00Z" }
    ]
  },
  "status": {
    "phase": "Active"
  }
}
""";
    }

    private static string Kubeconfig(string authKind, string directory)
    {
        var userBody = authKind switch
        {
            "token" => "    token: static-token",
            "token-file" => "    tokenFile: token.txt",
            "auth-provider" => """
    auth-provider:
      name: oidc
      config:
        access-token: oidc-token
""",
            "basic" => """
    username: user
    password: pass
""",
            "exec" => $$"""
    exec:
      command: {{Path.Combine(directory, "exec-token.sh")}}
      args:
      - ignored
      env:
      - name: PODLORD_EXEC_TEST
        value: "1"
""",
            "exec-counter" => $$"""
    exec:
      command: {{Path.Combine(directory, "exec-counter.sh")}}
      env:
      - name: PODLORD_EXEC_COUNTER
        value: {{Path.Combine(directory, "exec-count.txt")}}
""",
            "exec-missing-command" => """
    exec: {}
""",
            "exec-command-not-found" => """
    exec:
      command: definitely-not-a-podlord-command
""",
            _ => "    token: static-token"
        };

        return $$"""
apiVersion: v1
clusters:
- name: dev
  cluster:
    server: https://127.0.0.1:6443
contexts:
- name: dev
  context:
    cluster: dev
    user: dev
users:
- name: dev
  user:
{{userBody}}
""";
    }

    private static string TempDirectory()
    {
        var path = Path.Combine(Path.GetTempPath(), $"podlord-kube-fake-{Guid.NewGuid():N}");
        Directory.CreateDirectory(path);
        return path;
    }

    private static int FreePort()
    {
        using var listener = new TcpListener(IPAddress.Loopback, 0);
        listener.Start();
        return ((IPEndPoint)listener.LocalEndpoint).Port;
    }

    private sealed class RecordingHandler(Func<HttpRequestMessage, HttpResponseMessage> respond) : HttpMessageHandler
    {
        public List<string> Requests { get; } = [];

        public List<string> Methods { get; } = [];

        public List<string> ContentTypes { get; } = [];

        public List<string> Bodies { get; } = [];

        public List<AuthenticationHeaderValue?> Authorizations { get; } = [];

        protected override Task<HttpResponseMessage> SendAsync(HttpRequestMessage request, CancellationToken cancellationToken)
        {
            Requests.Add(request.RequestUri?.PathAndQuery ?? string.Empty);
            Methods.Add(request.Method.Method);
            ContentTypes.Add(request.Content?.Headers.ContentType?.MediaType ?? string.Empty);
            Bodies.Add(request.Content?.ReadAsStringAsync(cancellationToken).GetAwaiter().GetResult() ?? string.Empty);
            Authorizations.Add(request.Headers.Authorization);
            return Task.FromResult(respond(request));
        }
    }

    private sealed class AsyncRecordingHandler(Func<HttpRequestMessage, CancellationToken, Task<HttpResponseMessage>> respond) : HttpMessageHandler
    {
        protected override Task<HttpResponseMessage> SendAsync(HttpRequestMessage request, CancellationToken cancellationToken)
        {
            return respond(request, cancellationToken);
        }
    }
}
