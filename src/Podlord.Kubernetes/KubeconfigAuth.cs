using System.Text;
using System.Diagnostics;
using System.ComponentModel;
using System.Text.Json;
using System.Text.Json.Nodes;
using System.Security.Cryptography.X509Certificates;
using YamlDotNet.Serialization;

namespace Podlord.Kubernetes;

internal sealed record KubeconfigAuth(
    string? BearerToken,
    string? BasicAuthHeader,
    X509Certificate2? ClientCertificate,
    X509Certificate2? CertificateAuthority,
    bool SkipTlsVerify)
{
    public static KubeconfigAuth Empty { get; } = new(null, null, null, null, false);
}

internal static class KubeconfigAuthLoader
{
    private static readonly object ExecCacheLock = new();
    private static readonly Dictionary<string, ExecTokenCacheEntry> ExecTokenCache = [];

    public static KubeconfigAuth Load(string kubeconfigPath, string contextName)
    {
        if (!File.Exists(kubeconfigPath))
        {
            return KubeconfigAuth.Empty;
        }

        var deserializer = new DeserializerBuilder().IgnoreUnmatchedProperties().Build();
        var document = deserializer.Deserialize<Dictionary<object, object?>>(File.ReadAllText(kubeconfigPath));
        if (document is null)
        {
            return KubeconfigAuth.Empty;
        }

        var context = NamedEntry(document, "contexts", contextName)?["context"] as IDictionary<object, object?>;
        var userName = context is not null && context.TryGetValue("user", out var rawUser) ? rawUser?.ToString() : null;
        var clusterName = context is not null && context.TryGetValue("cluster", out var rawCluster) ? rawCluster?.ToString() : null;
        var cluster = string.IsNullOrWhiteSpace(clusterName)
            ? null
            : NamedEntry(document, "clusters", clusterName)?["cluster"] as IDictionary<object, object?>;
        var user = string.IsNullOrWhiteSpace(userName)
            ? null
            : NamedEntry(document, "users", userName)?["user"] as IDictionary<object, object?>;
        var token = user is null ? null : Value(user, "token") ?? TokenFile(user, kubeconfigPath) ?? AuthProviderToken(user) ?? ExecToken(user, kubeconfigPath, userName ?? contextName);
        var username = Value(user, "username");
        var password = Value(user, "password");
        var basic = username is { Length: > 0 } && password is { Length: > 0 }
            ? Convert.ToBase64String(Encoding.UTF8.GetBytes($"{username}:{password}"))
            : null;
        return new KubeconfigAuth(
            token,
            basic,
            user is null ? null : ClientCertificate(user, kubeconfigPath),
            CertificateAuthority(cluster, kubeconfigPath),
            Bool(cluster, "insecure-skip-tls-verify"));
    }

    private static IDictionary<object, object?>? NamedEntry(
        IDictionary<object, object?> document,
        string section,
        string name)
    {
        if (!document.TryGetValue(section, out var raw) || raw is not IList<object?> list)
        {
            return null;
        }

        return list
            .OfType<IDictionary<object, object?>>()
            .FirstOrDefault(entry => Value(entry, "name") == name);
    }

    private static string? TokenFile(IDictionary<object, object?> user, string kubeconfigPath)
    {
        var tokenPath = Value(user, "tokenFile");
        if (string.IsNullOrWhiteSpace(tokenPath))
        {
            return null;
        }

        var resolved = Path.IsPathRooted(tokenPath)
            ? tokenPath
            : Path.Combine(Path.GetDirectoryName(Path.GetFullPath(kubeconfigPath)) ?? ".", tokenPath);
        return File.Exists(resolved) ? File.ReadAllText(resolved).Trim() : null;
    }

    private static string? AuthProviderToken(IDictionary<object, object?> user)
    {
        if (!user.TryGetValue("auth-provider", out var provider)
            || provider is not IDictionary<object, object?> providerMap
            || !providerMap.TryGetValue("config", out var config)
            || config is not IDictionary<object, object?> configMap)
        {
            return null;
        }

        return Value(configMap, "access-token") ?? Value(configMap, "id-token");
    }

    private static string? ExecToken(IDictionary<object, object?> user, string kubeconfigPath, string cacheName)
    {
        if (!user.TryGetValue("exec", out var rawExec) || rawExec is not IDictionary<object, object?> exec)
        {
            return null;
        }

        var command = Value(exec, "command");
        if (string.IsNullOrWhiteSpace(command))
        {
            return null;
        }

        var cacheKey = $"{Path.GetFullPath(kubeconfigPath)}:{cacheName}:{command}:{Args(exec)}";
        lock (ExecCacheLock)
        {
            if (ExecTokenCache.TryGetValue(cacheKey, out var cached) && cached.ValidUntil > DateTimeOffset.UtcNow.AddMinutes(1))
            {
                return cached.Token;
            }
        }

        var token = RunExecCredential(exec, command);
        if (token is null)
        {
            return null;
        }

        lock (ExecCacheLock)
        {
            ExecTokenCache[cacheKey] = token;
        }

        return token.Token;
    }

    private static ExecTokenCacheEntry? RunExecCredential(IDictionary<object, object?> exec, string command)
    {
        using var process = new Process();
        foreach (var arg in Args(exec))
        {
            process.StartInfo.ArgumentList.Add(arg);
        }

        var execPath = AugmentedExecPath(Environment.GetEnvironmentVariable("PATH"));
        process.StartInfo.Environment["PATH"] = execPath;
        foreach (var (name, value) in Env(exec))
        {
            process.StartInfo.Environment[name] = value;
        }

        execPath = AugmentedExecPath(process.StartInfo.Environment.TryGetValue("PATH", out var configuredPath)
            ? configuredPath
            : execPath);
        process.StartInfo.Environment["PATH"] = execPath;
        process.StartInfo.FileName = ResolveExecCommand(command, execPath);
        process.StartInfo.RedirectStandardOutput = true;
        process.StartInfo.RedirectStandardError = true;
        process.StartInfo.UseShellExecute = false;
        try
        {
            if (!process.Start())
            {
                return null;
            }

            var stdout = process.StandardOutput.ReadToEnd();
            _ = process.StandardError.ReadToEnd();
            if (!process.WaitForExit(30_000) || process.ExitCode != 0)
            {
                return null;
            }

            var document = JsonNode.Parse(stdout)?.AsObject();
            var status = document?["status"]?.AsObject();
            var token = status?["token"]?.GetValue<string>();
            if (string.IsNullOrWhiteSpace(token))
            {
                return null;
            }

            var expiration = status?["expirationTimestamp"]?.GetValue<string>();
            var validUntil = DateTimeOffset.TryParse(expiration, out var parsed)
                ? parsed
                : DateTimeOffset.UtcNow.AddMinutes(15);
            return new ExecTokenCacheEntry(token, validUntil);
        }
        catch (Exception ex) when (ex is InvalidOperationException or IOException or UnauthorizedAccessException or JsonException or Win32Exception)
        {
            return null;
        }
    }

    internal static string AugmentedExecPath(string? configuredPath)
    {
        var entries = new List<string>();
        var seen = new HashSet<string>(StringComparer.Ordinal);
        AddPath(configuredPath);
        AddPath(Environment.GetEnvironmentVariable("PATH"));
        foreach (var path in DefaultExecSearchPaths())
        {
            AddPath(path);
        }

        return string.Join(Path.PathSeparator, entries);

        void AddPath(string? path)
        {
            if (string.IsNullOrWhiteSpace(path))
            {
                return;
            }

            foreach (var entry in path.Split(Path.PathSeparator, StringSplitOptions.RemoveEmptyEntries | StringSplitOptions.TrimEntries))
            {
                if (seen.Add(entry))
                {
                    entries.Add(entry);
                }
            }
        }
    }

    internal static string ResolveExecCommand(string command, string searchPath)
    {
        if (Path.IsPathRooted(command)
            || command.Contains(Path.DirectorySeparatorChar)
            || (Path.AltDirectorySeparatorChar != Path.DirectorySeparatorChar && command.Contains(Path.AltDirectorySeparatorChar)))
        {
            return command;
        }

        foreach (var directory in searchPath.Split(Path.PathSeparator, StringSplitOptions.RemoveEmptyEntries | StringSplitOptions.TrimEntries))
        {
            foreach (var candidate in ExecutableCandidates(directory, command))
            {
                if (File.Exists(candidate))
                {
                    return candidate;
                }
            }
        }

        return command;
    }

    private static IReadOnlyList<string> DefaultExecSearchPaths()
    {
        var paths = new List<string>();
        if (!OperatingSystem.IsWindows())
        {
            paths.AddRange([
                "/opt/homebrew/bin",
                "/usr/local/bin",
                "/opt/local/bin",
                "/usr/bin",
                "/bin",
                "/usr/sbin",
                "/sbin"
            ]);
        }

        var home = Environment.GetFolderPath(Environment.SpecialFolder.UserProfile);
        if (!string.IsNullOrWhiteSpace(home))
        {
            paths.Add(Path.Combine(home, ".local", "bin"));
            paths.Add(Path.Combine(home, "bin"));
        }

        return paths;
    }

    private static IEnumerable<string> ExecutableCandidates(string directory, string command)
    {
        yield return Path.Combine(directory, command);
        if (!OperatingSystem.IsWindows() || Path.HasExtension(command))
        {
            yield break;
        }

        foreach (var extension in (Environment.GetEnvironmentVariable("PATHEXT") ?? ".EXE;.CMD;.BAT")
                     .Split(';', StringSplitOptions.RemoveEmptyEntries | StringSplitOptions.TrimEntries))
        {
            yield return Path.Combine(directory, command + extension.ToLowerInvariant());
            yield return Path.Combine(directory, command + extension.ToUpperInvariant());
        }
    }

    private static IReadOnlyList<string> Args(IDictionary<object, object?> exec)
    {
        return exec.TryGetValue("args", out var raw) && raw is IList<object?> args
            ? args.Select(arg => arg?.ToString()).Where(arg => !string.IsNullOrWhiteSpace(arg)).Select(arg => arg!).ToList()
            : Array.Empty<string>();
    }

    private static IReadOnlyList<(string Name, string Value)> Env(IDictionary<object, object?> exec)
    {
        if (!exec.TryGetValue("env", out var raw) || raw is not IList<object?> env)
        {
            return Array.Empty<(string Name, string Value)>();
        }

        return env
            .OfType<IDictionary<object, object?>>()
            .Select(item => (Name: Value(item, "name"), Value: Value(item, "value")))
            .Where(item => !string.IsNullOrWhiteSpace(item.Name) && item.Value is not null)
            .Select(item => (item.Name!, item.Value!))
            .ToList();
    }

    private static string? Value(IDictionary<object, object?>? node, string key)
    {
        return node is not null && node.TryGetValue(key, out var value) ? value?.ToString() : null;
    }

    private static string? OptionalValue(IDictionary<object, object?>? node, string key)
    {
        return node is null ? null : Value(node, key);
    }

    private static bool Bool(IDictionary<object, object?>? node, string key)
    {
        return node is not null
               && node.TryGetValue(key, out var value)
               && value is not null
               && bool.TryParse(value.ToString(), out var flag)
               && flag;
    }

    private static X509Certificate2? CertificateAuthority(IDictionary<object, object?>? cluster, string kubeconfigPath)
    {
        var pem = PemFromDataOrFile(cluster, "certificate-authority-data", "certificate-authority", kubeconfigPath);
        return pem is null ? null : X509Certificate2.CreateFromPem(pem);
    }

    private static X509Certificate2? ClientCertificate(IDictionary<object, object?> user, string kubeconfigPath)
    {
        var certificate = PemFromDataOrFile(user, "client-certificate-data", "client-certificate", kubeconfigPath);
        var key = PemFromDataOrFile(user, "client-key-data", "client-key", kubeconfigPath);
        if (certificate is null || key is null)
        {
            return null;
        }

        using var loaded = X509Certificate2.CreateFromPem(certificate, key);
        return X509CertificateLoader.LoadPkcs12(loaded.Export(X509ContentType.Pkcs12), string.Empty);
    }

    private static string? PemFromDataOrFile(
        IDictionary<object, object?>? node,
        string dataKey,
        string pathKey,
        string kubeconfigPath)
    {
        var data = OptionalValue(node, dataKey);
        if (!string.IsNullOrWhiteSpace(data))
        {
            return Encoding.UTF8.GetString(Convert.FromBase64String(data.Trim()));
        }

        var path = OptionalValue(node, pathKey);
        if (string.IsNullOrWhiteSpace(path))
        {
            return null;
        }

        var resolved = Path.IsPathRooted(path)
            ? path
            : Path.Combine(Path.GetDirectoryName(Path.GetFullPath(kubeconfigPath)) ?? ".", path);
        return File.Exists(resolved) ? File.ReadAllText(resolved) : null;
    }
}

internal sealed record ExecTokenCacheEntry(string Token, DateTimeOffset ValidUntil);
