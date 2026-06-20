using System.Diagnostics;
using System.Net;
using System.Net.Http;
using System.Net.Sockets;
using System.Text;
using System.Text.Json;
using Podlord.Core;
using Podlord.Kubernetes;
using Xunit;

namespace Podlord.Kubernetes.Tests;

[CollectionDefinition("k3d-real-kubernetes")]
public sealed class K3dCollection : ICollectionFixture<K3dClusterFixture>;

[Collection("k3d-real-kubernetes")]
public sealed class K3dKubernetesBehaviorTests
{
    private readonly K3dClusterFixture cluster;

    public K3dKubernetesBehaviorTests(K3dClusterFixture cluster)
    {
        this.cluster = cluster;
    }

    [Fact]
    public async Task Explorer_loads_real_cluster_resource_map()
    {
        var rows = await WaitForRowsWithCondition(
            () => cluster.AdminService.ListClusterResourcesAsync(new ResourceQuery()),
            rows => rows.Any(row => row.Kind == "Service" && row.Name == "podlord-healthy")
                    && rows.Any(row => row.Kind == "EndpointSlice" && row.Namespace == "payments")
                    && rows.Any(row => row.Kind == "Secret" && row.Name == "podlord-secret")
                    && rows.Any(row => row.Kind == "Event"),
            TimeSpan.FromMinutes(2));
        var snapshot = await cluster.AdminService.ListClusterResourcesAsync(new ResourceQuery());

        Assert.Empty(snapshot.Failures);
        Assert.Contains(rows, row => row.Kind == "Namespace" && row.Name == "payments");
        Assert.Contains(rows, row => row.Kind == "Node");
        Assert.Contains(rows, row => row.Kind == "Pod" && row.Namespace == "payments" && row.Name.StartsWith("podlord-log", StringComparison.Ordinal));
        Assert.Contains(rows, row => row.Kind == "Deployment" && row.Namespace == "payments" && row.Name == "podlord-healthy");
        Assert.Contains(rows, row => row.Kind == "Deployment" && row.Namespace == "broken-zone" && row.Name == "podlord-broken");
        Assert.Contains(rows, row => row.Kind == "Service" && row.Name == "podlord-healthy");
        Assert.Contains(rows, row => row.Kind == "EndpointSlice" && row.Namespace == "payments");
        Assert.Contains(rows, row => row.Kind == "ConfigMap" && row.Name == "podlord-config");
        Assert.Contains(rows, row => row.Kind == "Secret" && row.Name == "podlord-secret");
        Assert.Contains(rows, row => row.Kind == "Job" && row.Namespace == "batch");
        Assert.Contains(rows, row => row.Kind == "CronJob" && row.Name == "podlord-cron");
        Assert.Contains(rows, row => row.Kind == "PersistentVolumeClaim" && row.Name == "podlord-data");
        Assert.Contains(rows, row => row.Kind == "NetworkPolicy" && row.Name == "podlord-deny-all");
        Assert.Contains(rows, row => row.Kind == "Event");
    }

    [Fact]
    public async Task Explorer_filters_real_rows_by_kind_namespace_status_and_search()
    {
        var pods = await cluster.AdminService.ListClusterResourcesAsync(new ResourceQuery(
            Kind: "Pod",
            Namespace: "payments",
            Search: "podlord"));
        var deployments = await cluster.AdminService.ListClusterResourcesAsync(new ResourceQuery(
            Kind: "Deployment",
            Namespace: "payments",
            Status: "Available"));

        Assert.Empty(pods.Failures);
        Assert.NotEmpty(pods.Rows);
        Assert.All(pods.Rows, row =>
        {
            Assert.Equal("Pod", row.Kind);
            Assert.Equal("payments", row.Namespace);
            Assert.Contains("podlord", row.Name, StringComparison.OrdinalIgnoreCase);
        });
        Assert.Contains(deployments.Rows, row => row.Name == "podlord-healthy" && row.Status == "Available");
    }

    [Fact]
    public async Task Explorer_filters_real_rows_by_image_ready_owner_restart_limit_and_problems()
    {
        var pods = await cluster.AdminService.ListClusterResourcesAsync(new ResourceQuery(
            Kind: "\"Pod\"",
            Namespace: "\"payments\"",
            Image: "nginx",
            Ready: "\"1/1\"",
            Restarts: "=0",
            Limit: 1));
        var broken = await cluster.AdminService.ListClusterResourcesAsync(new ResourceQuery(
            Kind: "\"Deployment\"",
            Namespace: "\"broken-zone\"",
            ProblemsOnly: true));

        Assert.Single(pods.Rows);
        Assert.Equal("Pod", pods.Rows[0].Kind);
        Assert.Equal("payments", pods.Rows[0].Namespace);
        Assert.Contains("nginx", pods.Rows[0].ImageSummary, StringComparison.OrdinalIgnoreCase);
        Assert.Equal("1/1", pods.Rows[0].Ready);
        Assert.Equal(0, pods.Rows[0].Restarts);
        Assert.Contains(broken.Rows, row => row.Name == "podlord-broken" && row.Status == "Unavailable");
        Assert.All(broken.Rows, row => Assert.True(ResourceFilterMatcher.IsProblem(row), $"{row.Kind}/{row.Name} was not marked as a problem"));
    }

    [Fact]
    public async Task Explorer_problem_default_scope_does_not_require_quiet_resource_kinds()
    {
        var snapshot = await cluster.AdminService.ListClusterResourcesAsync(new ResourceQuery(ProblemsOnly: true));

        Assert.Empty(snapshot.Failures);
        Assert.Contains(snapshot.Rows, row => row.Kind == "Deployment" && row.Name == "podlord-broken");
        Assert.DoesNotContain(snapshot.Rows, row => row.Kind is "ConfigMap" or "Secret" or "CustomResourceDefinition");
        Assert.All(snapshot.Rows, row => Assert.True(ResourceFilterMatcher.IsProblem(row), $"{row.Kind}/{row.Name} was not marked as a problem"));
    }

    [Fact]
    public async Task Cache_snapshot_is_empty_until_background_warm_fills_it()
    {
        var service = cluster.NewAdminService();
        var query = new ResourceQuery(Kind: "\"Pod\"", Namespace: "\"payments\"", ProblemsOnly: false);

        var cold = service.GetCachedResourceSnapshot(query);
        var warmed = await service.WarmResourceCacheAsync(query, KubernetesRequestPriority.Background);
        var cached = service.GetCachedResourceSnapshot(query);

        Assert.Empty(cold.Rows);
        Assert.Equal(FreshnessState.Unknown, cold.Freshness);
        Assert.NotEmpty(warmed.Rows);
        Assert.NotEmpty(cached.Rows);
        Assert.Equal(
            warmed.Rows.Select(row => row.Id).Order().ToArray(),
            cached.Rows.Select(row => row.Id).Order().ToArray());
    }

    [Fact]
    public async Task Detail_and_log_identity_requests_fill_identity_caches_through_real_queue()
    {
        var service = cluster.NewAdminService();
        var pod = await cluster.LogPodName();
        var identity = new ResourceIdentity(null, "Pod", "payments", pod);
        var logs = new PodLogRequest(null, "payments", pod, null, 25, false);

        Assert.Null(service.GetCachedResourceDetail(identity));
        Assert.Null(service.GetCachedPodLogs(logs));
        var detail = await service.GetResourceDetailAsync(identity, true, KubernetesRequestPriority.Foreground);
        var logSnapshot = await service.GetPodLogsAsync(logs, true, KubernetesRequestPriority.Foreground);

        Assert.Equal(pod, detail.Identity.Name);
        Assert.Equal(pod, service.GetCachedResourceDetail(identity)?.Identity.Name);
        Assert.Contains("podlord-log-heartbeat", logSnapshot.Text, StringComparison.OrdinalIgnoreCase);
        Assert.Contains("podlord-log-heartbeat", service.GetCachedPodLogs(logs)?.Text, StringComparison.OrdinalIgnoreCase);
    }

    [Fact]
    public async Task Detail_redacts_real_secret_data()
    {
        var detail = await cluster.AdminService.GetResourceDetailAsync(
            new ResourceIdentity(null, "Secret", "payments", "podlord-secret"));

        Assert.Equal("Observed", detail.Status);
        Assert.DoesNotContain("swordfish", detail.Yaml, StringComparison.OrdinalIgnoreCase);
        Assert.DoesNotContain("stringData", detail.Yaml, StringComparison.OrdinalIgnoreCase);
        Assert.DoesNotContain("last-applied-configuration", detail.Yaml, StringComparison.OrdinalIgnoreCase);
        Assert.DoesNotContain("managedFields", detail.Yaml, StringComparison.OrdinalIgnoreCase);
        Assert.Contains(detail.Summary, item => item.Label == "Image" && item.Value == "metadata only");
    }

    [Fact]
    public async Task Detail_loads_real_broken_pod_status_and_related_events()
    {
        var brokenPod = await cluster.BrokenPodName();

        var detail = await cluster.AdminService.GetResourceDetailAsync(
            new ResourceIdentity(null, "Pod", "broken-zone", brokenPod));

        Assert.True(
            new[] { "ErrImagePull", "ImagePullBackOff", "Pending" }.Contains(detail.Status, StringComparer.Ordinal),
            $"Unexpected broken pod status: {detail.Status}");
        Assert.Contains(detail.Summary, item => item.Label == "Namespace" && item.Value == "broken-zone");
        Assert.Contains(detail.Events, item =>
            item.Reason.Contains("Pull", StringComparison.OrdinalIgnoreCase)
            || item.Message.Contains("image", StringComparison.OrdinalIgnoreCase));
    }

    [Fact]
    public async Task Detail_summary_reports_local_creation_timestamp_and_age_for_real_pod()
    {
        var brokenPod = await cluster.BrokenPodName();

        var detail = await cluster.AdminService.GetResourceDetailAsync(
            new ResourceIdentity(null, "Pod", "broken-zone", brokenPod));

        var created = Assert.Single(detail.Summary, item => item.Label == "Created");
        Assert.Matches(@"^\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}[+-]\d{2}:\d{2}$", created.Value);
        var age = Assert.Single(detail.Summary, item => item.Label == "Age");
        Assert.False(string.IsNullOrWhiteSpace(age.Value));
        Assert.NotEqual("-", age.Value);
    }

    [Fact]
    public async Task Pod_logs_fetch_real_tail_from_running_pod()
    {
        var logPod = await cluster.LogPodName();

        var logs = await cluster.AdminService.GetPodLogsAsync(
            new PodLogRequest(null, "payments", logPod, null, 25, false));

        Assert.Equal(logPod, logs.Identity.Name);
        Assert.Contains("podlord-log-heartbeat", logs.Text, StringComparison.OrdinalIgnoreCase);
    }

    [Fact]
    public async Task Pod_logs_fetch_real_tails_from_all_containers_when_container_is_unspecified()
    {
        var pod = await cluster.MultiContainerLogPodName();

        var logs = await cluster.AdminService.GetPodLogsAsync(
            new PodLogRequest(null, "payments", pod, null, 25, false));

        Assert.Equal(pod, logs.Identity.Name);
        Assert.Contains("===== container: api =====", logs.Text, StringComparison.Ordinal);
        Assert.Contains("podlord-multi-api-heartbeat", logs.Text, StringComparison.OrdinalIgnoreCase);
        Assert.Contains("===== container: sidecar =====", logs.Text, StringComparison.Ordinal);
        Assert.Contains("podlord-multi-sidecar-heartbeat", logs.Text, StringComparison.OrdinalIgnoreCase);
    }

    [Fact]
    public async Task Jobs_and_cronjobs_are_visible_with_real_statuses()
    {
        var snapshot = await cluster.AdminService.ListClusterResourcesAsync(new ResourceQuery(
            Kind: "Job",
            Namespace: "batch",
            Search: "podlord"));

        Assert.Contains(snapshot.Rows, row => row.Name == "podlord-success" && row.Status == "Complete");
        Assert.Contains(snapshot.Rows, row => row.Name == "podlord-fail" && row.Status == "Failed");
        var cron = await cluster.AdminService.ListClusterResourcesAsync(new ResourceQuery(
            Kind: "CronJob",
            Namespace: "batch",
            Search: "podlord-cron"));
        Assert.Contains(cron.Rows, row => row.Name == "podlord-cron");
    }

    [Fact]
    public async Task Namespace_scoped_and_cluster_scoped_resource_queries_keep_cache_snapshots_consistent()
    {
        var service = cluster.NewAdminService();
        var namespaceQuery = new ResourceQuery(Kind: "\"Pod\"", Namespace: "\"payments\"", ForceRefresh: true);
        var clusterQuery = new ResourceQuery(Kind: "\"Pod\"", ForceRefresh: true);
        var namespaceFirst = await service.WarmResourceCacheAsync(namespaceQuery, KubernetesRequestPriority.Background);
        var clusterFirst = await service.WarmResourceCacheAsync(clusterQuery, KubernetesRequestPriority.Background);

        Assert.NotEmpty(namespaceFirst.Rows);
        Assert.NotEmpty(clusterFirst.Rows);
        Assert.True(clusterFirst.Rows.Count >= namespaceFirst.Rows.Count);

        var namespaceCached = service.GetCachedResourceSnapshot(namespaceQuery);
        var clusterCached = service.GetCachedResourceSnapshot(clusterQuery);

        var clusterIds = clusterCached.Rows.Select(row => row.Id).ToHashSet(StringComparer.Ordinal);
        Assert.All(namespaceCached.Rows, row => Assert.Contains(row.Id, clusterIds));

        var namespaceSecond = await service.WarmResourceCacheAsync(namespaceQuery, KubernetesRequestPriority.Background);
        var clusterSecond = await service.WarmResourceCacheAsync(clusterQuery, KubernetesRequestPriority.Background);

        var namespaceSecondIds = namespaceSecond.Rows.Select(row => row.Id).OrderBy(id => id).ToArray();
        var clusterSecondIds = clusterSecond.Rows.Select(row => row.Id).OrderBy(id => id).ToArray();

        Assert.Equal(namespaceFirst.Rows.Select(row => row.Id).OrderBy(id => id).ToArray(), namespaceSecondIds);
        Assert.True(namespaceSecondIds.All(id => clusterSecondIds.Contains(id, StringComparer.Ordinal)));
    }

    [Fact]
    public async Task Ingress_and_custom_resource_definition_resources_roundtrip_with_detail_views()
    {
        var suffix = Guid.NewGuid().ToString("N")[..8];
        var service = cluster.NewAdminService();
        var ingressName = $"podlord-ingress-{suffix}";
        var crdName = "podlord-tests.podlord.example";
        var customName = $"podlord-cr-{suffix}";
        var crdManifest = $$"""
apiVersion: apiextensions.k8s.io/v1
kind: CustomResourceDefinition
metadata:
  name: {{crdName}}
spec:
  group: podlord.example
  names:
    plural: podlord-tests
    singular: podlord-test
    kind: PodlordTest
    shortNames:
      - ptest
  scope: Namespaced
  versions:
    - name: v1
      served: true
      storage: true
      schema:
        openAPIV3Schema:
          type: object
          x-kubernetes-preserve-unknown-fields: true
""";
        var resourceManifest = $$"""
apiVersion: podlord.example/v1
kind: PodlordTest
metadata:
  name: {{customName}}
  namespace: payments
---
apiVersion: networking.k8s.io/v1
kind: Ingress
metadata:
  name: {{ingressName}}
  namespace: payments
spec:
  rules:
    - http:
        paths:
          - path: /podlord
            pathType: Prefix
            backend:
              service:
                name: podlord-healthy
                port:
                  number: 80
""";

        await cluster.ApplyManifestTextAsync(crdManifest);
        await cluster.WaitForCustomResourceDefinitionEstablishedAsync(crdName);
        await cluster.ApplyManifestTextAsync(resourceManifest);

        try
        {
            var ingressRows = await WaitForRowsWithCondition(
                () => service.ListClusterResourcesAsync(new ResourceQuery(Kind: "Ingress", Namespace: "payments")),
                rows => rows.Any(row => row.Name == ingressName),
                TimeSpan.FromMinutes(2));
            var crdRows = await WaitForRowsWithCondition(
                () => service.ListClusterResourcesAsync(new ResourceQuery(Kind: "CustomResourceDefinition")),
                rows => rows.Any(row => row.Name == crdName),
                TimeSpan.FromMinutes(2));

            Assert.Contains(ingressRows, row => row.Name == ingressName);
            Assert.Contains(crdRows, row => row.Name == crdName);

            var customFromKubectl = await cluster.KubectlOutputAsync(
                ["get", "podlord-tests", customName, "-n", "payments", "-o", "jsonpath={.metadata.name}"]);
            Assert.Equal(customName, customFromKubectl);

            var ingressDetail = await service.GetResourceDetailAsync(new ResourceIdentity(null, "Ingress", "payments", ingressName));
            var crdDetail = await service.GetResourceDetailAsync(new ResourceIdentity(null, "CustomResourceDefinition", null, crdName));

            Assert.Contains(ingressDetail.Summary, item => item.Label == "Kind" && item.Value == "Ingress");
            Assert.Contains(crdDetail.Summary, item => item.Label == "Kind" && item.Value == "CustomResourceDefinition");
        }
        finally
        {
            await cluster.DeleteManifestTextAsync(resourceManifest);
            await cluster.DeleteManifestTextAsync(crdManifest);
        }
    }

    [Fact]
    public async Task Resources_appear_and_disappear_between_live_queries()
    {
        var service = cluster.NewAdminService();
        var marker = Guid.NewGuid().ToString("N")[..8];
        var podName = $"podlord-lifecycle-{marker}";
        var manifest = $$"""
apiVersion: v1
kind: Pod
metadata:
  name: {{podName}}
  namespace: payments
spec:
  restartPolicy: Never
  containers:
    - name: sleeper
      image: busybox:1.36
      command: ["/bin/sh", "-c"]
      args: ["sleep 600"]
""";

        await cluster.ApplyManifestTextAsync(manifest);
        try
        {
            await WaitForRowsWithCondition(
                () => service.ListClusterResourcesAsync(new ResourceQuery(Kind: "Pod", Namespace: "payments")),
                rows => rows.Any(row => row.Name == podName),
                TimeSpan.FromMinutes(2));

            await cluster.DeleteManifestTextAsync(manifest);

            await WaitForRowsWithCondition(
                () => service.ListClusterResourcesAsync(new ResourceQuery(Kind: "Pod", Namespace: "payments")),
                rows => rows.All(row => row.Name != podName),
                TimeSpan.FromMinutes(3));
        }
        finally
        {
            await cluster.DeleteManifestTextAsync(manifest);
        }
    }

    [Fact]
    public async Task Service_endpoint_slice_and_config_objects_are_inspectable()
    {
        var service = await cluster.AdminService.GetResourceDetailAsync(
            new ResourceIdentity(null, "Service", "payments", "podlord-healthy"));
        var configMap = await cluster.AdminService.GetResourceDetailAsync(
            new ResourceIdentity(null, "ConfigMap", "payments", "podlord-config"));

        Assert.Contains(service.Summary, item => item.Label == "Kind" && item.Value == "Service");
        Assert.Contains(configMap.Summary, item => item.Label == "Image" && item.Value == "2 keys");
        var snapshot = await cluster.AdminService.ListClusterResourcesAsync(new ResourceQuery(Kind: "EndpointSlice", Namespace: "payments"));
        Assert.Contains(snapshot.Rows, row => row.Name.StartsWith("podlord-healthy", StringComparison.Ordinal));
    }

    [Fact]
    public async Task Native_port_forward_reaches_real_service_on_localhost()
    {
        var localPort = FreeTcpPort();
        await using var forward = await cluster.AdminService.StartPortForwardAsync(
            new PortForwardRequest(null, "Service", "payments", "podlord-healthy", localPort, 80));
        using var client = new HttpClient { Timeout = TimeSpan.FromSeconds(5) };

        var html = await WaitForHttp(client, new Uri($"http://127.0.0.1:{localPort}/"), TimeSpan.FromSeconds(20));

        Assert.True(forward.IsRunning);
        Assert.Contains("nginx", html, StringComparison.OrdinalIgnoreCase);
    }

    [Fact]
    public async Task Cluster_scoped_node_and_namespace_details_are_inspectable()
    {
        var snapshot = await cluster.AdminService.ListClusterResourcesAsync(new ResourceQuery(Kind: "Node"));
        var node = Assert.Single(snapshot.Rows, row => row.Kind == "Node");

        var nodeDetail = await cluster.AdminService.GetResourceDetailAsync(
            new ResourceIdentity(null, "Node", null, node.Name));
        var nsDetail = await cluster.AdminService.GetResourceDetailAsync(
            new ResourceIdentity(null, "Namespace", null, "payments"));

        Assert.Contains(nodeDetail.Summary, item => item.Label == "Kind" && item.Value == "Node");
        Assert.Contains(nsDetail.Summary, item => item.Label == "Name" && item.Value == "payments");
    }

    [Fact]
    public async Task Limited_rbac_context_reports_forbidden_real_api_failures()
    {
        var snapshot = await cluster.LimitedService.ListClusterResourcesAsync(new ResourceQuery());

        Assert.Contains(snapshot.Failures, failure => failure.Freshness == FreshnessState.Forbidden);
        Assert.Contains(snapshot.Failures, failure => failure.Kind is "Node" or "Secret" or "Deployment");
    }

    [Fact]
    public async Task Invalid_namespaced_detail_still_fails_before_network_call()
    {
        var error = await Assert.ThrowsAsync<PodlordException>(() =>
            cluster.AdminService.GetResourceDetailAsync(new ResourceIdentity(null, "Pod", null, "missing")));

        Assert.Equal(PodlordErrorKind.InvalidInput, error.Kind);
    }

    [Fact]
    public async Task Test_map_scenarios_are_present_in_cluster()
    {
        var snapshot = await cluster.AdminService.ListClusterResourcesAsync(new ResourceQuery(Search: "podlord"));
        var map = snapshot.Rows
            .GroupBy(row => row.Kind)
            .ToDictionary(group => group.Key, group => group.Select(row => row.Name).ToHashSet(StringComparer.Ordinal));

        Assert.Contains("podlord-healthy", map["Deployment"]);
        Assert.Contains("podlord-broken", map["Deployment"]);
        Assert.Contains("podlord-success", map["Job"]);
        Assert.Contains("podlord-fail", map["Job"]);
        Assert.Contains("podlord-cron", map["CronJob"]);
        Assert.Contains("podlord-config", map["ConfigMap"]);
        Assert.Contains("podlord-secret", map["Secret"]);
        Assert.Contains("podlord-data", map["PersistentVolumeClaim"]);
    }

    private static async Task<string> WaitForHttp(HttpClient client, Uri uri, TimeSpan timeout)
    {
        using var cts = new CancellationTokenSource(timeout);
        Exception? last = null;
        while (!cts.IsCancellationRequested)
        {
            try
            {
                return await client.GetStringAsync(uri, cts.Token).ConfigureAwait(false);
            }
            catch (Exception ex) when (ex is HttpRequestException or TaskCanceledException or OperationCanceledException)
            {
                last = ex;
                await Task.Delay(TimeSpan.FromMilliseconds(250), cts.Token).ConfigureAwait(false);
            }
        }

        throw new TimeoutException($"Port-forwarded endpoint {uri} did not respond.", last);
    }

    private static async Task<IReadOnlyList<FlatResourceRow>> WaitForRowsWithCondition(
        Func<Task<ResourceExplorerSnapshot>> query,
        Func<IReadOnlyList<FlatResourceRow>, bool> predicate,
        TimeSpan timeout)
    {
        var deadline = DateTimeOffset.UtcNow + timeout;
        Exception? last = null;
        while (DateTimeOffset.UtcNow < deadline)
        {
            try
            {
                var snapshot = await query().ConfigureAwait(false);
                var rows = snapshot.Rows;
                if (predicate(rows))
                {
                    return rows;
                }
            }
            catch (Exception ex)
            {
                last = ex;
            }

            await Task.Delay(TimeSpan.FromSeconds(2)).ConfigureAwait(false);
        }

        if (last is null)
        {
            throw new TimeoutException("Timed out waiting for rows matching condition.");
        }

        throw new TimeoutException("Timed out waiting for rows matching condition.", last);
    }

    private static int FreeTcpPort()
    {
        using var listener = new TcpListener(IPAddress.Loopback, 0);
        listener.Start();
        return ((IPEndPoint)listener.LocalEndpoint).Port;
    }
}

public sealed class K3dClusterFixture : IAsyncLifetime
{
    private readonly string tempDirectory = Path.Combine(Path.GetTempPath(), $"podlord-k3d-{Guid.NewGuid():N}");
    private readonly string clusterName = $"podlord-it-{Guid.NewGuid():N}"[..23];
    private bool clusterCreated;

    public KubernetesResourceService AdminService { get; private set; } = null!;

    public KubernetesResourceService LimitedService { get; private set; } = null!;

    public string KubeconfigPath { get; private set; } = string.Empty;

    public async Task InitializeAsync()
    {
        Directory.CreateDirectory(tempDirectory);
        RequireTool("docker");
        RequireTool("k3d");
        RequireTool("kubectl");
        EnsureDockerAvailable();
        await DeleteStaleTestClusters();
        await Run("k3d", ["cluster", "create", clusterName, "--servers", "1", "--agents", "0", "--wait", "--timeout", "180s", "--kubeconfig-update-default=false", "--kubeconfig-switch-context=false"], TimeSpan.FromMinutes(5));
        clusterCreated = true;
        KubeconfigPath = Path.Combine(tempDirectory, "kubeconfig.yaml");
        var kubeconfig = await Capture("k3d", ["kubeconfig", "get", clusterName], TimeSpan.FromMinutes(1));
        await File.WriteAllTextAsync(KubeconfigPath, kubeconfig.Replace("https://0.0.0.0:", "https://127.0.0.1:", StringComparison.Ordinal));
        await Run("kubectl", ["--kubeconfig", KubeconfigPath, "wait", "--for=condition=Ready", "node", "--all", "--timeout=240s"], TimeSpan.FromMinutes(5));
        await EnsureScenarioImagesAvailable();
        await ApplyScenarioManifest();
        await WaitForScenario();
        AdminService = ServiceFromKubeconfig(KubeconfigPath);
        var limited = await CreateLimitedKubeconfig();
        LimitedService = ServiceFromKubeconfig(limited);
    }

    public async Task DisposeAsync()
    {
        if (clusterCreated)
        {
            await Run("k3d", ["cluster", "delete", clusterName], TimeSpan.FromMinutes(2), throwOnError: false);
        }

        if (Directory.Exists(tempDirectory))
        {
            Directory.Delete(tempDirectory, recursive: true);
        }
    }

    public async Task<string> BrokenPodName()
    {
        return await WaitForOutput(
            "kubectl",
            ["--kubeconfig", KubeconfigPath, "-n", "broken-zone", "get", "pod", "-l", "app=podlord-broken", "-o", "jsonpath={.items[0].metadata.name}"],
            output => output.StartsWith("podlord-broken", StringComparison.Ordinal),
            TimeSpan.FromMinutes(3));
    }

    public async Task<string> LogPodName()
    {
        return await WaitForOutput(
            "kubectl",
            ["--kubeconfig", KubeconfigPath, "-n", "payments", "get", "pod", "-l", "app=podlord-log", "-o", "jsonpath={.items[0].metadata.name}"],
            output => output.StartsWith("podlord-log", StringComparison.Ordinal),
            TimeSpan.FromMinutes(3));
    }

    public async Task<string> MultiContainerLogPodName()
    {
        return await WaitForOutput(
            "kubectl",
            ["--kubeconfig", KubeconfigPath, "-n", "payments", "get", "pod", "-l", "app=podlord-multi-log", "-o", "jsonpath={.items[0].metadata.name}"],
            output => output.StartsWith("podlord-multi-log", StringComparison.Ordinal),
            TimeSpan.FromMinutes(3));
    }

    public async Task ApplyManifestTextAsync(string manifest)
    {
        await ApplyManifestTextInternal(manifest, delete: false).ConfigureAwait(false);
    }

    public async Task DeleteManifestTextAsync(string manifest)
    {
        await ApplyManifestTextInternal(manifest, delete: true).ConfigureAwait(false);
    }

    public async Task<string> KubectlOutputAsync(IReadOnlyList<string> arguments)
    {
        var kubectlArgs = new List<string> { "--kubeconfig", KubeconfigPath };
        kubectlArgs.AddRange(arguments);
        var result = await RunProcess("kubectl", kubectlArgs, TimeSpan.FromMinutes(2), true).ConfigureAwait(false);
        return result.Stdout.Trim();
    }

    public async Task WaitForCustomResourceDefinitionEstablishedAsync(string name)
    {
        var deadline = DateTimeOffset.UtcNow.AddMinutes(2);
        while (DateTimeOffset.UtcNow < deadline)
        {
            var result = await RunProcess(
                "kubectl",
                ["--kubeconfig", KubeconfigPath, "get", "crd", name, "-o", "json"],
                TimeSpan.FromSeconds(20),
                throwOnError: false).ConfigureAwait(false);
            if (result.ExitCode == 0 && CustomResourceDefinitionIsEstablished(result.Stdout))
            {
                return;
            }

            await Task.Delay(TimeSpan.FromSeconds(1)).ConfigureAwait(false);
        }

        throw new TimeoutException($"CRD {name} did not become Established within 2 minutes.");
    }

    private static bool CustomResourceDefinitionIsEstablished(string json)
    {
        using var document = JsonDocument.Parse(json);
        if (!document.RootElement.TryGetProperty("status", out var status)
            || !status.TryGetProperty("conditions", out var conditions)
            || conditions.ValueKind != JsonValueKind.Array)
        {
            return false;
        }

        return conditions.EnumerateArray().Any(condition =>
            condition.TryGetProperty("type", out var type)
            && string.Equals(type.GetString(), "Established", StringComparison.Ordinal)
            && condition.TryGetProperty("status", out var state)
            && string.Equals(state.GetString(), "True", StringComparison.Ordinal));
    }

    private async Task ApplyScenarioManifest()
    {
        var manifest = Path.Combine(tempDirectory, "scenario.yaml");
        await File.WriteAllTextAsync(manifest, ScenarioManifest);
        var arguments = new[] { "--kubeconfig", KubeconfigPath, "apply", "-f", manifest };
        for (var attempt = 1; attempt <= 3; attempt++)
        {
            var result = await RunProcess("kubectl", arguments, TimeSpan.FromMinutes(2), throwOnError: false);
            if (result.ExitCode == 0)
            {
                return;
            }

            if (attempt == 3 || !IsTransientKubectlApplyFailure(result))
            {
                throw new InvalidOperationException(ProcessFailureMessage("kubectl", arguments, result));
            }

            await Task.Delay(TimeSpan.FromSeconds(attempt * 3));
        }
    }

    private async Task ApplyManifestTextInternal(string manifestText, bool delete)
    {
        var manifest = Path.Combine(tempDirectory, $"manifest-{Guid.NewGuid():N}.yaml");
        await File.WriteAllTextAsync(manifest, manifestText).ConfigureAwait(false);
        try
        {
            var arguments = new List<string>
            {
                "--kubeconfig", KubeconfigPath,
                delete ? "delete" : "apply",
                "-f", manifest
            };
            if (delete)
            {
                arguments.Insert(2, "--ignore-not-found=true");
            }

            await Run("kubectl", arguments, TimeSpan.FromMinutes(2), throwOnError: true);
        }
        finally
        {
            if (File.Exists(manifest))
            {
                File.Delete(manifest);
            }
        }
    }

    private static bool IsTransientKubectlApplyFailure(ProcessResult result)
    {
        var output = $"{result.Stdout}\n{result.Stderr}";
        return output.Contains("http2: server sent GOAWAY", StringComparison.OrdinalIgnoreCase)
               || output.Contains(" EOF", StringComparison.OrdinalIgnoreCase)
               || output.Contains(": EOF", StringComparison.OrdinalIgnoreCase);
    }

    private async Task WaitForScenario()
    {
        try
        {
            await Run("kubectl", ["--kubeconfig", KubeconfigPath, "-n", "payments", "rollout", "status", "deployment/podlord-healthy", "--timeout=420s"], TimeSpan.FromMinutes(8));
            await Run("kubectl", ["--kubeconfig", KubeconfigPath, "-n", "payments", "wait", "--for=condition=Ready", "pod", "-l", "app=podlord-log", "--timeout=420s"], TimeSpan.FromMinutes(8));
            await Run("kubectl", ["--kubeconfig", KubeconfigPath, "-n", "payments", "wait", "--for=condition=Ready", "pod", "-l", "app=podlord-multi-log", "--timeout=420s"], TimeSpan.FromMinutes(8));
            await Run("kubectl", ["--kubeconfig", KubeconfigPath, "-n", "batch", "wait", "--for=condition=complete", "job/podlord-success", "--timeout=420s"], TimeSpan.FromMinutes(8));
            await Run("kubectl", ["--kubeconfig", KubeconfigPath, "-n", "batch", "wait", "--for=condition=failed", "job/podlord-fail", "--timeout=420s"], TimeSpan.FromMinutes(8));
            await WaitForOutput(
                "kubectl",
                ["--kubeconfig", KubeconfigPath, "-n", "broken-zone", "get", "events", "--field-selector", "involvedObject.kind=Pod", "-o", "jsonpath={.items[*].reason}"],
                output => output.Contains("Pull", StringComparison.OrdinalIgnoreCase) || output.Contains("Failed", StringComparison.OrdinalIgnoreCase),
                TimeSpan.FromMinutes(4));
        }
        catch (Exception ex) when (ex is InvalidOperationException or TimeoutException)
        {
            var diagnostics = await CaptureScenarioDiagnostics();
            throw new InvalidOperationException($"{ex.Message}\n\nScenario diagnostics:\n{diagnostics}", ex);
        }
    }

    private async Task EnsureScenarioImagesAvailable()
    {
        foreach (var image in ScenarioImages)
        {
            await Run("docker", ["pull", image], TimeSpan.FromMinutes(4));
        }

        var importArguments = new List<string> { "image", "import" };
        importArguments.AddRange(ScenarioImages);
        importArguments.AddRange(["-c", clusterName]);
        await Run("k3d", importArguments, TimeSpan.FromMinutes(4));
    }

    private async Task DeleteStaleTestClusters()
    {
        var existing = await RunProcess("k3d", ["cluster", "list", "-o", "json"], TimeSpan.FromMinutes(1), throwOnError: true);
        using var document = JsonDocument.Parse(existing.Stdout);
        foreach (var cluster in document.RootElement.EnumerateArray())
        {
            if (!cluster.TryGetProperty("name", out var nameProperty))
            {
                continue;
            }

            var existingName = nameProperty.GetString();
            if (string.IsNullOrWhiteSpace(existingName)
                || existingName == clusterName
                || !existingName.StartsWith("podlord-it-", StringComparison.Ordinal))
            {
                continue;
            }

            await Run("k3d", ["cluster", "delete", existingName], TimeSpan.FromMinutes(2), throwOnError: false);
        }
    }

    private async Task<string> CaptureScenarioDiagnostics()
    {
        var commands = new (string FileName, IReadOnlyList<string> Arguments, string Label)[]
        {
            ("kubectl", ["--kubeconfig", KubeconfigPath, "get", "pods", "-A", "-o", "wide"], "pods"),
            ("kubectl", ["--kubeconfig", KubeconfigPath, "-n", "payments", "describe", "deployment", "podlord-healthy"], "healthy deployment"),
            ("kubectl", ["--kubeconfig", KubeconfigPath, "-n", "payments", "describe", "pods"], "payments pod descriptions"),
            ("kubectl", ["--kubeconfig", KubeconfigPath, "-n", "batch", "describe", "jobs"], "batch jobs"),
            ("kubectl", ["--kubeconfig", KubeconfigPath, "-n", "broken-zone", "get", "events", "--sort-by=.metadata.creationTimestamp"], "broken-zone events")
        };

        var output = new StringBuilder();
        foreach (var command in commands)
        {
            var result = await RunProcess(command.FileName, command.Arguments, TimeSpan.FromMinutes(1), throwOnError: false);
            output.AppendLine($"## {command.Label}");
            output.AppendLine(result.Stdout.Trim());
            if (!string.IsNullOrWhiteSpace(result.Stderr))
            {
                output.AppendLine(result.Stderr.Trim());
            }

            output.AppendLine();
        }

        return output.ToString().TrimEnd();
    }

    private async Task<string> CreateLimitedKubeconfig()
    {
        var token = await Capture("kubectl", ["--kubeconfig", KubeconfigPath, "-n", "payments", "create", "token", "podlord-limited", "--duration=1h"], TimeSpan.FromMinutes(1));
        var server = await Capture("kubectl", ["config", "view", "--kubeconfig", KubeconfigPath, "--raw", "-o", "jsonpath={.clusters[0].cluster.server}"], TimeSpan.FromMinutes(1));
        var ca = await Capture("kubectl", ["config", "view", "--kubeconfig", KubeconfigPath, "--raw", "-o", "jsonpath={.clusters[0].cluster.certificate-authority-data}"], TimeSpan.FromMinutes(1));
        var path = Path.Combine(tempDirectory, "limited-kubeconfig.yaml");
        await File.WriteAllTextAsync(path, $$"""
apiVersion: v1
clusters:
- name: podlord-limited
  cluster:
    server: {{server.Trim()}}
    certificate-authority-data: {{ca.Trim()}}
contexts:
- name: podlord-limited
  context:
    cluster: podlord-limited
    user: podlord-limited
    namespace: payments
current-context: podlord-limited
users:
- name: podlord-limited
  user:
    token: {{token.Trim()}}
""");
        return path;
    }

    private KubernetesResourceService ServiceFromKubeconfig(string path)
    {
        var state = AppState.InMemoryWithConfigDirectory(Path.Combine(tempDirectory, $"state-{Guid.NewGuid():N}"));
        state.ImportKubeconfig(path);
        return new KubernetesResourceService(state);
    }

    public KubernetesResourceService NewAdminService()
    {
        return ServiceFromKubeconfig(KubeconfigPath);
    }

    private static void RequireTool(string name)
    {
        var path = RunSync("/bin/sh", ["-c", $"command -v {name}"], TimeSpan.FromSeconds(10), throwOnError: false).Stdout.Trim();
        if (path.Length == 0)
        {
            throw new InvalidOperationException($"{name} is required for real Kubernetes tests. Run scripts/bootstrap-k3d.sh.");
        }
    }

    private static void EnsureDockerAvailable()
    {
        var docker = RunSync("docker", ["info"], TimeSpan.FromSeconds(20), throwOnError: false);
        if (docker.ExitCode == 0)
        {
            return;
        }

        var colima = RunSync("/bin/sh", ["-c", "command -v colima"], TimeSpan.FromSeconds(10), throwOnError: false);
        if (colima.Stdout.Trim().Length > 0)
        {
            RunSync("colima", ["start", "--cpu", "2", "--memory", "4"], TimeSpan.FromMinutes(5));
        }

        docker = RunSync("docker", ["info"], TimeSpan.FromSeconds(30), throwOnError: false);
        if (docker.ExitCode != 0)
        {
            throw new InvalidOperationException($"Docker is required for k3d tests. docker info failed: {docker.Stderr}");
        }
    }

    private static async Task<string> WaitForOutput(
        string fileName,
        IReadOnlyList<string> arguments,
        Func<string, bool> predicate,
        TimeSpan timeout)
    {
        var deadline = DateTimeOffset.UtcNow + timeout;
        string last = string.Empty;
        while (DateTimeOffset.UtcNow < deadline)
        {
            var result = await RunProcess(fileName, arguments, TimeSpan.FromSeconds(30), throwOnError: false);
            last = result.Stdout.Trim();
            if (result.ExitCode == 0 && predicate(last))
            {
                return last;
            }

            await Task.Delay(TimeSpan.FromSeconds(2));
        }

        throw new TimeoutException($"Timed out waiting for {fileName} {string.Join(' ', arguments)}. Last output: {last}");
    }

    private static Task Run(string fileName, IReadOnlyList<string> arguments, TimeSpan timeout, bool throwOnError = true)
    {
        return RunProcess(fileName, arguments, timeout, throwOnError);
    }

    private static async Task<string> Capture(string fileName, IReadOnlyList<string> arguments, TimeSpan timeout)
    {
        var result = await RunProcess(fileName, arguments, timeout, throwOnError: true);
        return result.Stdout;
    }

    private static ProcessResult RunSync(
        string fileName,
        IReadOnlyList<string> arguments,
        TimeSpan timeout,
        bool throwOnError = true)
    {
        return RunProcess(fileName, arguments, timeout, throwOnError).GetAwaiter().GetResult();
    }

    private static async Task<ProcessResult> RunProcess(
        string fileName,
        IReadOnlyList<string> arguments,
        TimeSpan timeout,
        bool throwOnError)
    {
        using var process = new Process();
        process.StartInfo.FileName = fileName;
        foreach (var argument in arguments)
        {
            process.StartInfo.ArgumentList.Add(argument);
        }

        process.StartInfo.RedirectStandardOutput = true;
        process.StartInfo.RedirectStandardError = true;
        process.StartInfo.UseShellExecute = false;
        var stdout = new StringBuilder();
        var stderr = new StringBuilder();
        process.OutputDataReceived += (_, args) =>
        {
            if (args.Data is not null)
            {
                stdout.AppendLine(args.Data);
            }
        };
        process.ErrorDataReceived += (_, args) =>
        {
            if (args.Data is not null)
            {
                stderr.AppendLine(args.Data);
            }
        };
        if (!process.Start())
        {
            throw new InvalidOperationException($"Could not start {fileName}.");
        }

        process.BeginOutputReadLine();
        process.BeginErrorReadLine();
        using var cts = new CancellationTokenSource(timeout);
        try
        {
            await process.WaitForExitAsync(cts.Token);
        }
        catch (OperationCanceledException)
        {
            process.Kill(entireProcessTree: true);
            throw new TimeoutException($"{fileName} {string.Join(' ', arguments)} timed out after {timeout}.");
        }

        var result = new ProcessResult(process.ExitCode, stdout.ToString(), stderr.ToString());
        if (throwOnError && result.ExitCode != 0)
        {
            throw new InvalidOperationException(ProcessFailureMessage(fileName, arguments, result));
        }

        return result;
    }

    private static string ProcessFailureMessage(
        string fileName,
        IReadOnlyList<string> arguments,
        ProcessResult result)
    {
        return $"{fileName} {string.Join(' ', arguments)} failed with {result.ExitCode}\nSTDOUT:\n{result.Stdout}\nSTDERR:\n{result.Stderr}";
    }

    private sealed record ProcessResult(int ExitCode, string Stdout, string Stderr);

    private const string ScenarioManifest = """
apiVersion: v1
kind: Namespace
metadata:
  name: payments
---
apiVersion: v1
kind: Namespace
metadata:
  name: broken-zone
---
apiVersion: v1
kind: Namespace
metadata:
  name: batch
---
apiVersion: v1
kind: ServiceAccount
metadata:
  name: default
  namespace: payments
---
apiVersion: v1
kind: ServiceAccount
metadata:
  name: default
  namespace: broken-zone
---
apiVersion: v1
kind: ServiceAccount
metadata:
  name: default
  namespace: batch
---
apiVersion: v1
kind: ConfigMap
metadata:
  name: podlord-config
  namespace: payments
data:
  mode: tactical
  faction: podlord
---
apiVersion: v1
kind: Secret
metadata:
  name: podlord-secret
  namespace: payments
stringData:
  password: swordfish
---
apiVersion: v1
kind: PersistentVolumeClaim
metadata:
  name: podlord-data
  namespace: payments
spec:
  accessModes:
    - ReadWriteOnce
  resources:
    requests:
      storage: 64Mi
---
apiVersion: v1
kind: ServiceAccount
metadata:
  name: podlord-limited
  namespace: payments
---
apiVersion: rbac.authorization.k8s.io/v1
kind: Role
metadata:
  name: podlord-pod-reader
  namespace: payments
rules:
  - apiGroups: [""]
    resources: ["pods", "pods/log"]
    verbs: ["get", "list"]
---
apiVersion: rbac.authorization.k8s.io/v1
kind: RoleBinding
metadata:
  name: podlord-pod-reader
  namespace: payments
subjects:
  - kind: ServiceAccount
    name: podlord-limited
    namespace: payments
roleRef:
  apiGroup: rbac.authorization.k8s.io
  kind: Role
  name: podlord-pod-reader
---
apiVersion: apps/v1
kind: Deployment
metadata:
  name: podlord-healthy
  namespace: payments
spec:
  replicas: 1
  selector:
    matchLabels:
      app: podlord-healthy
  template:
    metadata:
      labels:
        app: podlord-healthy
    spec:
      containers:
        - name: web
          image: nginx:1.27-alpine
          ports:
            - containerPort: 80
---
apiVersion: v1
kind: Service
metadata:
  name: podlord-healthy
  namespace: payments
spec:
  selector:
    app: podlord-healthy
  ports:
    - name: http
      port: 80
      targetPort: 80
---
apiVersion: apps/v1
kind: Deployment
metadata:
  name: podlord-broken
  namespace: broken-zone
spec:
  replicas: 1
  selector:
    matchLabels:
      app: podlord-broken
  template:
    metadata:
      labels:
        app: podlord-broken
    spec:
      containers:
        - name: broken
          image: ghcr.io/podlord/definitely-missing:never
---
apiVersion: v1
kind: Pod
metadata:
  name: podlord-log
  namespace: payments
  labels:
    app: podlord-log
spec:
  restartPolicy: Always
  containers:
    - name: logger
      image: busybox:1.36
      command: ["/bin/sh", "-c"]
      args:
        - echo podlord-log-ready; while true; do echo podlord-log-heartbeat; sleep 2; done
---
apiVersion: v1
kind: Pod
metadata:
  name: podlord-multi-log
  namespace: payments
  labels:
    app: podlord-multi-log
spec:
  restartPolicy: Always
  containers:
    - name: api
      image: busybox:1.36
      command: ["/bin/sh", "-c"]
      args:
        - echo podlord-multi-api-ready; while true; do echo podlord-multi-api-heartbeat; sleep 2; done
    - name: sidecar
      image: busybox:1.36
      command: ["/bin/sh", "-c"]
      args:
        - echo podlord-multi-sidecar-ready; while true; do echo podlord-multi-sidecar-heartbeat; sleep 2; done
---
apiVersion: batch/v1
kind: Job
metadata:
  name: podlord-success
  namespace: batch
spec:
  template:
    spec:
      restartPolicy: Never
      containers:
        - name: ok
          image: busybox:1.36
          command: ["/bin/sh", "-c", "echo podlord-job-success"]
---
apiVersion: batch/v1
kind: Job
metadata:
  name: podlord-fail
  namespace: batch
spec:
  backoffLimit: 0
  template:
    spec:
      restartPolicy: Never
      containers:
        - name: fail
          image: busybox:1.36
          command: ["/bin/sh", "-c", "echo podlord-job-fail; exit 1"]
---
apiVersion: batch/v1
kind: CronJob
metadata:
  name: podlord-cron
  namespace: batch
spec:
  schedule: "*/5 * * * *"
  suspend: true
  jobTemplate:
    spec:
      template:
        spec:
          restartPolicy: Never
          containers:
            - name: cron
              image: busybox:1.36
              command: ["/bin/sh", "-c", "echo podlord-cron"]
---
apiVersion: networking.k8s.io/v1
kind: NetworkPolicy
metadata:
  name: podlord-deny-all
  namespace: payments
spec:
  podSelector: {}
  policyTypes:
    - Ingress
    - Egress
""";

    private static readonly string[] ScenarioImages =
    [
        "nginx:1.27-alpine",
        "busybox:1.36"
    ];
}
