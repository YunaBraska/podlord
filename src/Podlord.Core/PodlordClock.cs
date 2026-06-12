using System.Security.Cryptography;
using System.Text;

namespace Podlord.Core;

public interface IPodlordClock
{
    DateTimeOffset Now { get; }
}

public sealed class SystemPodlordClock : IPodlordClock
{
    public DateTimeOffset Now => DateTimeOffset.UtcNow;
}

public static class PodlordText
{
    public static string NowUtcString(IPodlordClock clock)
    {
        return clock.Now.ToUniversalTime().ToString("yyyy-MM-dd'T'HH:mm:ss.fffffff'Z'");
    }

    public static string StableSlug(string value)
    {
        var builder = new StringBuilder(value.Length);
        foreach (var character in value)
        {
            builder.Append(char.IsAsciiLetterOrDigit(character) ? char.ToLowerInvariant(character) : '-');
        }

        return string.Join(
            "-",
            builder
                .ToString()
                .Split('-', StringSplitOptions.RemoveEmptyEntries | StringSplitOptions.TrimEntries));
    }

    public static string StableHash(string value)
    {
        var bytes = SHA256.HashData(Encoding.UTF8.GetBytes(value));
        return Convert.ToHexString(bytes).ToLowerInvariant()[..16];
    }

    public static string HumanAge(DateTimeOffset? timestamp, IPodlordClock clock)
    {
        if (timestamp is null)
        {
            return "-";
        }

        var age = clock.Now.ToUniversalTime() - timestamp.Value.ToUniversalTime();
        if (age < TimeSpan.Zero)
        {
            age = TimeSpan.Zero;
        }

        if (age.TotalDays >= 1)
        {
            return $"{(int)age.TotalDays}d{age.Hours}h";
        }

        if (age.TotalHours >= 1)
        {
            return $"{(int)age.TotalHours}h{age.Minutes}m";
        }

        if (age.TotalMinutes >= 1)
        {
            return $"{(int)age.TotalMinutes}m";
        }

        return $"{Math.Max(0, (int)age.TotalSeconds)}s";
    }
}
