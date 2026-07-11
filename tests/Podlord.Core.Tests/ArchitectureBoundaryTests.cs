namespace Podlord.Core.Tests;

public sealed class ArchitectureBoundaryTests
{
    [Fact]
    public void Core_has_no_ui_or_kubernetes_client_dependencies()
    {
        var root = FindRepositoryRoot();
        var project = File.ReadAllText(Path.Combine(root, "src", "Podlord.Core", "Podlord.Core.csproj"));
        var source = SourceText(Path.Combine(root, "src", "Podlord.Core"));

        Assert.DoesNotContain("Avalonia", project, StringComparison.Ordinal);
        Assert.DoesNotContain("KubernetesClient", project, StringComparison.Ordinal);
        Assert.DoesNotContain("k8s", project, StringComparison.OrdinalIgnoreCase);
        Assert.DoesNotContain("<ProjectReference", project, StringComparison.Ordinal);
        Assert.DoesNotContain("using Avalonia", source, StringComparison.Ordinal);
        Assert.DoesNotContain("using k8s", source, StringComparison.Ordinal);
        Assert.DoesNotContain("KubernetesClient", source, StringComparison.Ordinal);
    }

    [Fact]
    public void Kubernetes_adapter_has_no_avalonia_dependency()
    {
        var root = FindRepositoryRoot();
        var project = File.ReadAllText(Path.Combine(root, "src", "Podlord.Kubernetes", "Podlord.Kubernetes.csproj"));
        var source = SourceText(Path.Combine(root, "src", "Podlord.Kubernetes"));

        Assert.DoesNotContain("Avalonia", project, StringComparison.Ordinal);
        Assert.DoesNotContain("Avalonia", source, StringComparison.Ordinal);
    }

    [Fact]
    public void App_uses_kubernetes_adapter_only_from_composition_root()
    {
        var root = FindRepositoryRoot();
        var appProject = File.ReadAllText(Path.Combine(root, "src", "Podlord.App", "Podlord.App.csproj"));
        var appRoot = Path.Combine(root, "src", "Podlord.App");
        var compositionRoot = Path.Combine(appRoot, "KubernetesServiceBootstrap.cs");
        var assemblyInfo = Path.Combine(appRoot, "AssemblyInfo.cs");
        var leaks = SourceFiles(appRoot)
            .Where(path => !Path.GetFullPath(path).Equals(Path.GetFullPath(compositionRoot), StringComparison.Ordinal))
            .Where(path => !Path.GetFullPath(path).Equals(Path.GetFullPath(assemblyInfo), StringComparison.Ordinal))
            .SelectMany(path => ForbiddenAppReferences(path))
            .ToList();

        Assert.DoesNotContain("KubernetesClient", appProject, StringComparison.Ordinal);
        Assert.True(leaks.Count == 0, string.Join(Environment.NewLine, leaks));

        var bootstrap = File.ReadAllText(compositionRoot);
        Assert.Contains("using Podlord.Kubernetes;", bootstrap, StringComparison.Ordinal);
        Assert.Contains("new KubernetesResourceService", bootstrap, StringComparison.Ordinal);
        Assert.DoesNotContain("using k8s", bootstrap, StringComparison.Ordinal);
        Assert.DoesNotContain("KubernetesClient", bootstrap, StringComparison.Ordinal);
        Assert.True(File.ReadLines(compositionRoot).Count() <= 20, "Kubernetes composition root must stay tiny.");
    }

    [Fact]
    public void Production_kubernetes_client_types_do_not_escape_adapter_project()
    {
        var root = FindRepositoryRoot();
        var nonAdapterSource = SourceText(
            Path.Combine(root, "src", "Podlord.Core"),
            Path.Combine(root, "src", "Podlord.App"));

        Assert.DoesNotContain("using k8s", nonAdapterSource, StringComparison.Ordinal);
        Assert.DoesNotContain("k8s.", nonAdapterSource, StringComparison.Ordinal);
        Assert.DoesNotContain("KubernetesClient", nonAdapterSource, StringComparison.Ordinal);
    }

    private static IEnumerable<string> ForbiddenAppReferences(string path)
    {
        var text = File.ReadAllText(path);
        foreach (var token in new[]
        {
            "using Podlord.Kubernetes;",
            "Podlord.Kubernetes.",
            "KubernetesResourceService",
            "using k8s",
            "KubernetesClient"
        })
        {
            if (text.Contains(token, StringComparison.Ordinal))
            {
                yield return $"{Path.GetRelativePath(FindRepositoryRoot(), path)} contains {token}";
            }
        }

        if (System.Text.RegularExpressions.Regex.IsMatch(text, @"(?<!I)\bPodlordPortForward\b"))
        {
            yield return $"{Path.GetRelativePath(FindRepositoryRoot(), path)} contains PodlordPortForward";
        }
    }

    private static string SourceText(params string[] roots)
    {
        return string.Join(
            Environment.NewLine,
            roots.SelectMany(SourceFiles).Select(File.ReadAllText));
    }

    private static IEnumerable<string> SourceFiles(string root)
    {
        return Directory.EnumerateFiles(root, "*.cs", SearchOption.AllDirectories)
            .Order(StringComparer.Ordinal);
    }

    private static string FindRepositoryRoot()
    {
        var current = AppContext.BaseDirectory;
        while (!string.IsNullOrWhiteSpace(current))
        {
            if (File.Exists(Path.Combine(current, "Podlord.slnx")))
            {
                return current;
            }

            current = Directory.GetParent(current)?.FullName;
        }

        throw new InvalidOperationException("Could not locate Podlord repository root.");
    }
}
