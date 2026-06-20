using System.Net.Http.Headers;
using System.Reflection;
using System.Runtime.InteropServices;
using System.Text.Json;
using System.Text.RegularExpressions;
using Podlord.Core;

namespace Podlord.App;

public interface IReleaseUpdateChecker : IDisposable
{
    Task<UpdateCheckState> CheckLatestAsync(string currentVersion, CancellationToken cancellationToken);
}

public sealed class NoOpReleaseUpdateChecker : IReleaseUpdateChecker
{
    public Task<UpdateCheckState> CheckLatestAsync(string currentVersion, CancellationToken cancellationToken)
    {
        var checkedAt = DateTimeOffset.UtcNow.ToString("O");
        return Task.FromResult(new UpdateCheckState(
            checkedAt,
            currentVersion,
            currentVersion,
            string.Empty,
            string.Empty,
            false,
            "Update checks are disabled."));
    }

    public void Dispose()
    {
    }
}

public sealed class GitHubReleaseUpdateChecker : IReleaseUpdateChecker
{
    public const string ReleasesLatestEndpoint = "https://api.github.com/repos/YunaBraska/podlord/releases/latest";
    public const string GitHubApiVersion = "2026-03-10";

    private readonly HttpClient client;
    private readonly bool disposeClient;

    public GitHubReleaseUpdateChecker()
        : this(new HttpClient { Timeout = TimeSpan.FromSeconds(10) }, disposeClient: true)
    {
    }

    public GitHubReleaseUpdateChecker(HttpClient client, bool disposeClient = false)
    {
        this.client = client;
        this.disposeClient = disposeClient;
    }

    public async Task<UpdateCheckState> CheckLatestAsync(string currentVersion, CancellationToken cancellationToken)
    {
        var checkedAt = DateTimeOffset.UtcNow.ToString("O");
        try
        {
            using var request = new HttpRequestMessage(HttpMethod.Get, ReleasesLatestEndpoint);
            request.Headers.Accept.Add(new MediaTypeWithQualityHeaderValue("application/vnd.github+json"));
            request.Headers.UserAgent.ParseAdd($"Podlord/{SafeUserAgentVersion(currentVersion)}");
            request.Headers.TryAddWithoutValidation("X-GitHub-Api-Version", GitHubApiVersion);

            using var response = await client.SendAsync(request, HttpCompletionOption.ResponseHeadersRead, cancellationToken).ConfigureAwait(false);
            if (!response.IsSuccessStatusCode)
            {
                return new UpdateCheckState(
                    checkedAt,
                    currentVersion,
                    string.Empty,
                    string.Empty,
                    string.Empty,
                    false,
                    $"GitHub returned {(int)response.StatusCode} {response.ReasonPhrase}".Trim());
            }

            await using var stream = await response.Content.ReadAsStreamAsync(cancellationToken).ConfigureAwait(false);
            using var document = await JsonDocument.ParseAsync(stream, cancellationToken: cancellationToken).ConfigureAwait(false);
            var root = document.RootElement;
            var latestVersion = JsonString(root, "tag_name");
            var releaseUrl = JsonString(root, "html_url");
            var downloadUrl = PreferredDownloadUrl(root) ?? releaseUrl;
            var isNewer = IsNewerRelease(currentVersion, latestVersion);

            return new UpdateCheckState(
                checkedAt,
                currentVersion,
                latestVersion,
                releaseUrl,
                downloadUrl,
                isNewer,
                string.Empty);
        }
        catch (JsonException ex)
        {
            return FailedState(checkedAt, currentVersion, $"GitHub release response was invalid JSON: {ex.Message}");
        }
        catch (HttpRequestException ex)
        {
            return FailedState(checkedAt, currentVersion, $"Could not reach GitHub releases: {ex.Message}");
        }
        catch (TaskCanceledException) when (!cancellationToken.IsCancellationRequested)
        {
            return FailedState(checkedAt, currentVersion, "GitHub release check timed out.");
        }
    }

    public void Dispose()
    {
        if (disposeClient)
        {
            client.Dispose();
        }
    }

    public static string CurrentApplicationVersion()
    {
        var assembly = typeof(GitHubReleaseUpdateChecker).Assembly;
        var informational = assembly.GetCustomAttribute<AssemblyInformationalVersionAttribute>()?.InformationalVersion;
        if (!string.IsNullOrWhiteSpace(informational))
        {
            return informational.Split('+', StringSplitOptions.TrimEntries | StringSplitOptions.RemoveEmptyEntries)[0];
        }

        return assembly.GetName().Version?.ToString() ?? "0.0.0";
    }

    public static bool IsNewerRelease(string currentVersion, string latestVersion)
    {
        var current = VersionParts(currentVersion);
        var latest = VersionParts(latestVersion);
        if (current.Count == 0 || latest.Count == 0)
        {
            return false;
        }

        var length = Math.Max(current.Count, latest.Count);
        for (var index = 0; index < length; index++)
        {
            var currentPart = index < current.Count ? current[index] : 0;
            var latestPart = index < latest.Count ? latest[index] : 0;
            if (latestPart > currentPart)
            {
                return true;
            }

            if (latestPart < currentPart)
            {
                return false;
            }
        }

        return false;
    }

    public static string RuntimeReleaseAssetName()
    {
        var architecture = RuntimeInformation.ProcessArchitecture switch
        {
            Architecture.Arm64 => "arm64",
            Architecture.Arm => "arm",
            Architecture.X86 => "x86",
            _ => "x64"
        };

        if (RuntimeInformation.IsOSPlatform(OSPlatform.OSX))
        {
            return $"podlord-macos-{architecture}.zip";
        }

        if (RuntimeInformation.IsOSPlatform(OSPlatform.Windows))
        {
            return $"podlord-win-{architecture}.zip";
        }

        return $"podlord-linux-{architecture}.tar.gz";
    }

    internal static string? PreferredAssetUrl(IEnumerable<ReleaseAssetInfo> assets, string preferredAssetName)
    {
        var candidates = assets.ToList();
        var compatibleNames = CompatibleAssetNames(preferredAssetName);
        return candidates
                   .FirstOrDefault(asset => compatibleNames.Any(name => asset.Name.Equals(name, StringComparison.OrdinalIgnoreCase)))
                   ?.BrowserDownloadUrl
               ?? candidates
                   .FirstOrDefault(asset => compatibleNames.Any(name => asset.Name.Contains(name.Replace("podlord-", string.Empty, StringComparison.Ordinal), StringComparison.OrdinalIgnoreCase)))
                   ?.BrowserDownloadUrl;
    }

    private static IReadOnlyList<string> CompatibleAssetNames(string preferredAssetName)
    {
        var names = new List<string> { preferredAssetName };
        if (preferredAssetName.Contains("podlord-macos-", StringComparison.OrdinalIgnoreCase))
        {
            names.Add(preferredAssetName.Replace("podlord-macos-", "podlord-osx-", StringComparison.OrdinalIgnoreCase));
        }
        else if (preferredAssetName.Contains("podlord-osx-", StringComparison.OrdinalIgnoreCase))
        {
            names.Add(preferredAssetName.Replace("podlord-osx-", "podlord-macos-", StringComparison.OrdinalIgnoreCase));
        }

        return names;
    }

    private static UpdateCheckState FailedState(string checkedAt, string currentVersion, string error)
    {
        return new UpdateCheckState(checkedAt, currentVersion, string.Empty, string.Empty, string.Empty, false, error);
    }

    private static string SafeUserAgentVersion(string currentVersion)
    {
        var sanitized = Regex.Replace(currentVersion.Trim(), "[^A-Za-z0-9._-]", "-");
        return sanitized.Length == 0 ? "0.0.0" : sanitized;
    }

    private static string? PreferredDownloadUrl(JsonElement root)
    {
        if (!root.TryGetProperty("assets", out var assets) || assets.ValueKind != JsonValueKind.Array)
        {
            return null;
        }

        return PreferredAssetUrl(assets.EnumerateArray().Select(ParseAsset), RuntimeReleaseAssetName());
    }

    private static ReleaseAssetInfo ParseAsset(JsonElement asset)
    {
        return new ReleaseAssetInfo(
            JsonString(asset, "name"),
            JsonString(asset, "browser_download_url"));
    }

    private static string JsonString(JsonElement element, string propertyName)
    {
        return element.TryGetProperty(propertyName, out var property) && property.ValueKind == JsonValueKind.String
            ? property.GetString() ?? string.Empty
            : string.Empty;
    }

    private static IReadOnlyList<int> VersionParts(string version)
    {
        return Regex.Matches(version.Split('+', StringSplitOptions.TrimEntries | StringSplitOptions.RemoveEmptyEntries).FirstOrDefault() ?? string.Empty, @"\d+")
            .Select(match => int.TryParse(match.Value, out var value) ? value : 0)
            .ToArray();
    }
}

internal sealed record ReleaseAssetInfo(string Name, string BrowserDownloadUrl);

public static class ReleaseUpdateCheckerFactory
{
    public static IReleaseUpdateChecker CreateDefault()
    {
        var disabled = Environment.GetEnvironmentVariable("PODLORD_DISABLE_UPDATE_CHECK");
        return disabled is "1" or "true" or "TRUE"
            ? new NoOpReleaseUpdateChecker()
            : new GitHubReleaseUpdateChecker();
    }
}
