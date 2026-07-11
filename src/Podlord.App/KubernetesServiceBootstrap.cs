using Podlord.Core;
using Podlord.Kubernetes;

namespace Podlord.App;

internal static class KubernetesServiceBootstrap
{
    public static IKubernetesApplicationPort Create(AppState state)
    {
        return new KubernetesResourceService(state);
    }
}
