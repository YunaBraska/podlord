namespace Podlord.Kubernetes;

internal sealed record ResourceSpec(
    string Kind,
    string ListPath,
    bool Namespaced,
    bool Optional,
    bool DefaultProblemScan,
    Func<string?, string, string> DetailPath)
{
    public string ListPathForNamespace(string? ns)
    {
        if (!Namespaced || string.IsNullOrWhiteSpace(ns))
        {
            return ListPath;
        }

        var plural = ListPath.Split('/').Last();
        if (ListPath.StartsWith("/api/v1/", StringComparison.Ordinal))
        {
            return $"/api/v1/namespaces/{Escape(ns)}/{plural}";
        }

        var parts = ListPath.Split('/', StringSplitOptions.RemoveEmptyEntries);
        return parts.Length >= 4 && parts[0] == "apis"
            ? $"/apis/{parts[1]}/{parts[2]}/namespaces/{Escape(ns)}/{plural}"
            : ListPath;
    }

    private static string Escape(string value)
    {
        return Uri.EscapeDataString(value);
    }
}

internal static class ResourceSpecs
{
    public static IReadOnlyList<ResourceSpec> Listable { get; } =
    [
        Cluster("Namespace", "/api/v1/namespaces", true, (ns, name) => $"/api/v1/namespaces/{Escape(name)}"),
        Cluster("Node", "/api/v1/nodes", true, (ns, name) => $"/api/v1/nodes/{Escape(name)}"),
        Namespaced("Pod", "/api/v1/pods", true, (ns, name) => $"/api/v1/namespaces/{Escape(ns!)}/pods/{Escape(name)}"),
        Namespaced("Service", "/api/v1/services", true, (ns, name) => $"/api/v1/namespaces/{Escape(ns!)}/services/{Escape(name)}"),
        Namespaced("ConfigMap", "/api/v1/configmaps", false, (ns, name) => $"/api/v1/namespaces/{Escape(ns!)}/configmaps/{Escape(name)}"),
        Namespaced("Secret", "/api/v1/secrets", false, (ns, name) => $"/api/v1/namespaces/{Escape(ns!)}/secrets/{Escape(name)}"),
        Cluster("PersistentVolume", "/api/v1/persistentvolumes", true, (ns, name) => $"/api/v1/persistentvolumes/{Escape(name)}"),
        Namespaced("PersistentVolumeClaim", "/api/v1/persistentvolumeclaims", true, (ns, name) => $"/api/v1/namespaces/{Escape(ns!)}/persistentvolumeclaims/{Escape(name)}"),
        Namespaced("ServiceAccount", "/api/v1/serviceaccounts", false, (ns, name) => $"/api/v1/namespaces/{Escape(ns!)}/serviceaccounts/{Escape(name)}"),
        Namespaced("Event", "/api/v1/events", true, (ns, name) => $"/api/v1/namespaces/{Escape(ns!)}/events/{Escape(name)}"),
        Namespaced("Deployment", "/apis/apps/v1/deployments", true, (ns, name) => $"/apis/apps/v1/namespaces/{Escape(ns!)}/deployments/{Escape(name)}"),
        Namespaced("ReplicaSet", "/apis/apps/v1/replicasets", true, (ns, name) => $"/apis/apps/v1/namespaces/{Escape(ns!)}/replicasets/{Escape(name)}"),
        Namespaced("StatefulSet", "/apis/apps/v1/statefulsets", true, (ns, name) => $"/apis/apps/v1/namespaces/{Escape(ns!)}/statefulsets/{Escape(name)}"),
        Namespaced("DaemonSet", "/apis/apps/v1/daemonsets", true, (ns, name) => $"/apis/apps/v1/namespaces/{Escape(ns!)}/daemonsets/{Escape(name)}"),
        Namespaced("Job", "/apis/batch/v1/jobs", true, (ns, name) => $"/apis/batch/v1/namespaces/{Escape(ns!)}/jobs/{Escape(name)}"),
        Namespaced("CronJob", "/apis/batch/v1/cronjobs", true, (ns, name) => $"/apis/batch/v1/namespaces/{Escape(ns!)}/cronjobs/{Escape(name)}"),
        Namespaced("Ingress", "/apis/networking.k8s.io/v1/ingresses", true, (ns, name) => $"/apis/networking.k8s.io/v1/namespaces/{Escape(ns!)}/ingresses/{Escape(name)}"),
        Namespaced("NetworkPolicy", "/apis/networking.k8s.io/v1/networkpolicies", false, (ns, name) => $"/apis/networking.k8s.io/v1/namespaces/{Escape(ns!)}/networkpolicies/{Escape(name)}"),
        Namespaced("EndpointSlice", "/apis/discovery.k8s.io/v1/endpointslices", false, (ns, name) => $"/apis/discovery.k8s.io/v1/namespaces/{Escape(ns!)}/endpointslices/{Escape(name)}"),
        OptionalNamespaced("Gateway", "/apis/gateway.networking.k8s.io/v1/gateways", false, (ns, name) => $"/apis/gateway.networking.k8s.io/v1/namespaces/{Escape(ns!)}/gateways/{Escape(name)}"),
        OptionalCluster("GatewayClass", "/apis/gateway.networking.k8s.io/v1/gatewayclasses", false, (ns, name) => $"/apis/gateway.networking.k8s.io/v1/gatewayclasses/{Escape(name)}"),
        OptionalNamespaced("HTTPRoute", "/apis/gateway.networking.k8s.io/v1/httproutes", false, (ns, name) => $"/apis/gateway.networking.k8s.io/v1/namespaces/{Escape(ns!)}/httproutes/{Escape(name)}"),
        OptionalNamespaced("GRPCRoute", "/apis/gateway.networking.k8s.io/v1/grpcroutes", false, (ns, name) => $"/apis/gateway.networking.k8s.io/v1/namespaces/{Escape(ns!)}/grpcroutes/{Escape(name)}"),
        Cluster("CustomResourceDefinition", "/apis/apiextensions.k8s.io/v1/customresourcedefinitions", false, (ns, name) => $"/apis/apiextensions.k8s.io/v1/customresourcedefinitions/{Escape(name)}")
    ];

    public static ResourceSpec? ForKind(string kind)
    {
        return Listable.FirstOrDefault(spec => spec.Kind.Equals(kind, StringComparison.OrdinalIgnoreCase));
    }

    private static ResourceSpec Cluster(string kind, string listPath, bool defaultProblemScan, Func<string?, string, string> detailPath)
    {
        return new ResourceSpec(kind, listPath, false, false, defaultProblemScan, detailPath);
    }

    private static ResourceSpec Namespaced(string kind, string listPath, bool defaultProblemScan, Func<string?, string, string> detailPath)
    {
        return new ResourceSpec(kind, listPath, true, false, defaultProblemScan, detailPath);
    }

    private static ResourceSpec OptionalCluster(string kind, string listPath, bool defaultProblemScan, Func<string?, string, string> detailPath)
    {
        return new ResourceSpec(kind, listPath, false, true, defaultProblemScan, detailPath);
    }

    private static ResourceSpec OptionalNamespaced(string kind, string listPath, bool defaultProblemScan, Func<string?, string, string> detailPath)
    {
        return new ResourceSpec(kind, listPath, true, true, defaultProblemScan, detailPath);
    }

    private static string Escape(string value)
    {
        return Uri.EscapeDataString(value);
    }
}
