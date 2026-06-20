using System.Net;
using System.Text.Json;
using System.Text.Json.Nodes;
using Podlord.Core;
using Podlord.Kubernetes;

namespace Podlord.Kubernetes.Tests;

public sealed class KubernetesHelperBehaviorTests
{
    [Fact]
    public void Metrics_quantity_and_usage_helpers_parse_kubernetes_units()
    {
        Assert.Null(KubernetesResourceService.ParseCpuQuantity(null));
        Assert.Null(KubernetesResourceService.ParseCpuQuantity("wat"));
        Assert.Equal(250, KubernetesResourceService.ParseCpuQuantity("250m"));
        Assert.Equal(1000, KubernetesResourceService.ParseCpuQuantity("1"));
        Assert.Equal(0.5, KubernetesResourceService.ParseCpuQuantity("500u"));
        Assert.Equal(1, KubernetesResourceService.ParseCpuQuantity("1000000n"));

        Assert.Null(KubernetesResourceService.ParseByteQuantity(""));
        Assert.Null(KubernetesResourceService.ParseByteQuantity("wat"));
        Assert.Equal(1024, KubernetesResourceService.ParseByteQuantity("1Ki"));
        Assert.Equal(1_500_000, KubernetesResourceService.ParseByteQuantity("1.5M"));
        Assert.Equal(42, KubernetesResourceService.ParseByteQuantity("42"));

        var usage = KubernetesResourceService.ContainerMetricUsage(Object("""
        {"cpu":"250m","memory":"64Mi"}
        """));
        Assert.Equal(250, usage.CpuMillicores);
        Assert.Equal(64L * 1024 * 1024, usage.MemoryBytes);
        Assert.Equal(LivePulseUsage.Empty, KubernetesResourceService.ContainerMetricUsage(null));

        var pod = Object("""
        {
          "metadata":{"namespace":"payments","name":"api"},
          "containers":[
            {"usage":{"cpu":"250m","memory":"64Mi"}},
            {"usage":{"cpu":"1","memory":"1Gi"}}
          ]
        }
        """);
        var podMetric = KubernetesResourceService.PodMetricUsage(pod);
        Assert.Equal("payments/api", podMetric.Key);
        Assert.Equal(1250, podMetric.Usage.CpuMillicores);
        Assert.Equal(64L * 1024 * 1024 + 1024L * 1024 * 1024, podMetric.Usage.MemoryBytes);
        Assert.Equal((string.Empty, LivePulseUsage.Empty), KubernetesResourceService.PodMetricUsage(new JsonObject()));

        var nodeMetric = KubernetesResourceService.NodeMetricUsage(Object("""
        {"metadata":{"name":"node-a"},"usage":{"cpu":"2","memory":"2Gi"}}
        """));
        Assert.Equal("node-a", nodeMetric.Key);
        Assert.Equal(2000, nodeMetric.Usage.CpuMillicores);
        Assert.Equal((string.Empty, LivePulseUsage.Empty), KubernetesResourceService.NodeMetricUsage(new JsonObject()));

        var items = KubernetesResourceService.Items(Object("""{"items":[{"a":1},{"b":2}]}""")).ToList();
        Assert.Equal(2, items.Count);
        Assert.Empty(KubernetesResourceService.Items(new JsonObject()));
        Assert.True(KubernetesResourceService.IsOptionalMetricsFailure(new KubernetesStatusException(HttpStatusCode.NotFound, "missing")));
        Assert.True(KubernetesResourceService.IsOptionalMetricsFailure(new KubernetesStatusException(HttpStatusCode.TooManyRequests, "slow")));
        Assert.False(KubernetesResourceService.IsOptionalMetricsFailure(new KubernetesStatusException(HttpStatusCode.BadRequest, "bad")));
        Assert.Equal("403 Forbidden", KubernetesResourceService.ShortStatus(new KubernetesStatusException(HttpStatusCode.Forbidden, "blocked")));
        Assert.Equal("/pod", KubernetesResourceService.PulsePodKey(null, "pod"));
    }

    [Fact]
    public void Resource_shape_helpers_map_status_ready_images_owners_and_service_ports()
    {
        var pod = Object("""
        {
          "metadata":{
            "name":"api","namespace":"payments",
            "ownerReferences":[{"kind":"ReplicaSet","name":"api-123"}]
          },
          "spec":{
            "nodeName":"node-a",
            "containers":[
              {"image":"registry.local/team/api:v1","ports":[{"name":"http","containerPort":8080}]},
              {"image":"sidecar:v2","resources":{"limits":{"cpu":"500m","memory":"256Mi"}}}
            ]
          },
          "status":{
            "phase":"Running",
            "containerStatuses":[
              {"ready":true,"restartCount":1},
              {"ready":false,"restartCount":2,"state":{"waiting":{"reason":"CrashLoopBackOff"}}}
            ]
          }
        }
        """);

        Assert.Equal("CrashLoopBackOff", KubernetesResourceService.Status("Pod", pod));
        Assert.Equal("1/2", KubernetesResourceService.Ready("Pod", pod));
        Assert.Equal(3, KubernetesResourceService.Restarts("Pod", pod));
        Assert.Equal("api:v1, sidecar:v2", KubernetesResourceService.ImageSummary("Pod", pod));
        Assert.Equal("ReplicaSet/api-123", KubernetesResourceService.Owner("Pod", pod));
        Assert.Equal("Terminating", KubernetesResourceService.Status("Pod", Object("""{"metadata":{"deletionTimestamp":"now"}}""")));
        Assert.Equal("-", KubernetesResourceService.Ready("Pod", new JsonObject()));

        Assert.Equal("Available", KubernetesResourceService.Status("Deployment", Object("""{"spec":{"replicas":2},"status":{"availableReplicas":2}}""")));
        Assert.Equal("Unavailable", KubernetesResourceService.Status("Deployment", Object("""{"spec":{"replicas":2},"status":{"availableReplicas":1}}""")));
        Assert.Equal("ScaledZero", KubernetesResourceService.Status("ReplicaSet", Object("""{"spec":{"replicas":0},"status":{"readyReplicas":0}}""")));
        Assert.Equal("Progressing", KubernetesResourceService.Status("ReplicaSet", Object("""{"spec":{"replicas":3},"status":{"readyReplicas":1}}""")));
        Assert.Equal("Ready", KubernetesResourceService.Status("Node", Object("""{"status":{"conditions":[{"type":"Ready","status":"True"}]}}""")));
        Assert.Equal("NotReady", KubernetesResourceService.Status("Node", Object("""{"status":{"conditions":[{"type":"Ready","status":"False"}]}}""")));
        Assert.Equal("Suspended", KubernetesResourceService.Status("CronJob", Object("""{"spec":{"suspend":true}}""")));
        Assert.Equal("ClusterIP", KubernetesResourceService.Status("Service", Object("""{"spec":{"type":"ClusterIP"}}""")));
        Assert.Equal("3/5", KubernetesResourceService.Ready("Deployment", Object("""{"spec":{"replicas":5},"status":{"readyReplicas":3}}""")));
        Assert.Equal("-", KubernetesResourceService.Ready("Node", new JsonObject()));
        Assert.Equal("metadata only", KubernetesResourceService.ImageSummary("Secret", new JsonObject()));
        Assert.Equal("2 keys", KubernetesResourceService.ImageSummary("ConfigMap", Object("""{"data":{"a":"1","b":"2"}}""")));
        Assert.Equal("-", KubernetesResourceService.ImageSummary("Node", new JsonObject()));

        var service = Object("""
        {"spec":{"selector":{"app":"api","empty":"","ignored":2},"ports":[
          {"port":80,"targetPort":"http"},
          {"port":443,"targetPort":8443},
          {"port":9000}
        ]}}
        """);
        var selector = KubernetesResourceService.LabelSelector(service);
        Assert.Equal("api", selector["app"]);
        Assert.Equal("app=api", KubernetesResourceService.LabelSelectorExpression(selector));
        Assert.Equal(8080, KubernetesResourceService.ResolveServicePort(service, pod, 80));
        Assert.Equal(8443, KubernetesResourceService.ResolveServicePort(service, pod, 443));
        Assert.Equal(9000, KubernetesResourceService.ResolveServicePort(service, pod, 9000));
        Assert.Equal(1234, KubernetesResourceService.ResolveServicePort(service, pod, 1234));
        Assert.Null(KubernetesResourceService.NamedContainerPort(pod, "missing"));
    }

    [Fact]
    public void Value_sanitize_yaml_and_text_helpers_keep_secret_handling_explicit()
    {
        var configMap = Object("""
        {"data":{"plain":"hello","encoded":"aGVsbG8="},"binaryData":{"blob":"AQID"}}
        """);
        var configValues = KubernetesResourceService.ValueItems(configMap, "ConfigMap");
        Assert.Equal(["blob", "encoded", "plain"], configValues.Select(item => item.Key));
        Assert.False(configValues.Single(item => item.Key == "plain").Sensitive);
        Assert.True(configValues.Single(item => item.Key == "encoded").Base64Encoded);
        Assert.True(configValues.Single(item => item.Key == "blob").Base64Encoded);

        var secret = Object("""
        {"metadata":{"annotations":{"a":"b"}},"data":{"password":"czNjcjN0"},"stringData":{"token":"raw"}}
        """);
        var secretValues = KubernetesResourceService.ValueItems(secret, "Secret");
        Assert.All(secretValues, item => Assert.True(item.Sensitive));
        Assert.True(secretValues.Single(item => item.Key == "password").Base64Encoded);
        Assert.False(secretValues.Single(item => item.Key == "token").Base64Encoded);
        Assert.Empty(KubernetesResourceService.ValueItems(new JsonObject(), "Node"));
        Assert.True(KubernetesResourceService.LooksLikeBase64("aGVsbG8="));
        Assert.False(KubernetesResourceService.LooksLikeBase64("not base64!"));

        var sanitized = KubernetesResourceService.SanitizeObject(secret.DeepClone()!.AsObject(), "Secret");
        Assert.False(sanitized.ContainsKey("data"));
        Assert.False(sanitized.ContainsKey("stringData"));
        Assert.False(sanitized["metadata"]!.AsObject().ContainsKey("annotations"));

        var noisy = Object("""
        {"metadata":{"managedFields":[{}],"annotations":{"kubectl.kubernetes.io/last-applied-configuration":"x","deployment.kubernetes.io/revision":"1"}}}
        """);
        var clean = KubernetesResourceService.SanitizeObject(noisy, "ConfigMap");
        Assert.False(clean["metadata"]!.AsObject().ContainsKey("managedFields"));
        Assert.False(clean["metadata"]!.AsObject().ContainsKey("annotations"));

        var document = Object("""{"text":"x","flag":true,"number":7,"array":[1,"two"],"nested":{"a":1}}""");
        var plain = Assert.IsType<Dictionary<string, object?>>(KubernetesResourceService.PlainJsonValue(document));
        Assert.Equal("x", plain["text"]);
        Assert.Equal(true, plain["flag"]);
        Assert.Equal(7, plain["number"]);
        Assert.Contains("text: x", KubernetesResourceService.ToYaml(document), StringComparison.Ordinal);
        Assert.Equal("a%2Fb", KubernetesResourceService.Escape("a/b"));
        Assert.Equal("redacted redacted", KubernetesResourceService.Sanitize("token password"));
        Assert.Contains("redacted", KubernetesResourceService.HttpFailureMessage(new HttpRequestException("token", new InvalidOperationException("password"))), StringComparison.Ordinal);
        Assert.Equal("x", KubernetesResourceService.Text(document, "/text", "-"));
        Assert.Equal("7", KubernetesResourceService.OptionalText(document, "/number"));
        Assert.Equal("True", KubernetesResourceService.OptionalText(document, "/flag"));
        Assert.Null(KubernetesResourceService.OptionalText(document, "/missing"));
        Assert.Equal(7, KubernetesResourceService.Int(document, "/number"));
        Assert.Equal(99, KubernetesResourceService.Int(document, "/missing", 99));
        Assert.True(KubernetesResourceService.Bool(document, "/flag"));
        Assert.False(KubernetesResourceService.Bool(document, "/missing"));
    }

    [Fact]
    public void Summary_condition_and_event_helpers_surface_operational_context()
    {
        var eventItem = Object("""
        {"metadata":{"name":"api.1","uid":"event-1"},"reason":"BackOff","message":"container crashed","involvedObject":{"kind":"Pod","name":"api"}}
        """);
        var eventInfo = KubernetesResourceService.EventInfo("Event", eventItem);
        Assert.Equal(("api.1", "BackOff", "container crashed", "Pod/api"), eventInfo);
        Assert.Equal((string.Empty, string.Empty, string.Empty, string.Empty), KubernetesResourceService.EventInfo("Pod", eventItem));
        Assert.Equal("Pod/api", KubernetesResourceService.Owner("Event", eventItem));

        var item = Object("""
        {
          "metadata":{"uid":"uid-1","creationTimestamp":"2026-03-02T14:32:00Z"},
          "spec":{"replicas":3},
          "status":{
            "replicas":3,
            "readyReplicas":2,
            "availableReplicas":1,
            "fullyLabeledReplicas":3,
            "observedGeneration":9,
            "conditions":[{"type":"Available","status":"False","reason":"MinimumReplicasUnavailable"}]
          }
        }
        """);
        var row = new FlatResourceRow(
            "id",
            "Unavailable",
            "Deployment",
            "api",
            "payments",
            "cluster",
            "1m",
            "2/3",
            0,
            "node-a",
            "api:v1",
            "ReplicaSet/api-123",
            "now",
            FreshnessState.Fresh)
        {
            Pulse = ResourcePulse.Empty with
            {
                CpuMillicores = 125,
                CpuLimitMillicores = 500,
                MemoryBytes = 128L * 1024 * 1024,
                MemoryLimitBytes = 512L * 1024 * 1024
            }
        };

        var summary = KubernetesResourceService.SummaryItems(item, row).ToDictionary(detail => detail.Label, detail => detail.Value);
        Assert.Equal("Deployment", summary["Kind"]);
        Assert.Equal("uid-1", summary["UID"]);
        var expectedCreated = PodlordText.HumanIsoTimestamp(new DateTimeOffset(2026, 3, 2, 14, 32, 0, TimeSpan.Zero));
        Assert.Equal(expectedCreated, summary["Created"]);
        Assert.Matches(@"^\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}[+-]\d{2}:\d{2}$", summary["Created"]);
        Assert.Equal("1m", summary["Age"]);
        Assert.Equal("160m request / 250m limit", summary["CPU limit suggestion"]);
        Assert.Equal("160Mi request / 256Mi limit", summary["Memory limit suggestion"]);
        Assert.Equal("3", summary["Replicas desired"]);
        Assert.Equal("2", summary["Replicas ready"]);

        var conditions = KubernetesResourceService.ConditionItems(item);
        Assert.Equal("Available", Assert.Single(conditions).Label);
        Assert.Equal("False MinimumReplicasUnavailable", Assert.Single(conditions).Value);
        Assert.Empty(KubernetesResourceService.ConditionItems(new JsonObject()));
    }

    private static JsonObject Object(string json)
    {
        return JsonNode.Parse(json)!.AsObject();
    }
}
