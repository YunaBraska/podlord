using System.Security.Cryptography;
using System.Text;
using System.Globalization;

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

    public static string HumanTimestamp(DateTimeOffset? timestamp)
    {
        return HumanTimestamp(timestamp, TimeZoneInfo.Local);
    }

    public static string HumanTimestamp(DateTimeOffset? timestamp, TimeZoneInfo zone)
    {
        if (timestamp is null)
        {
            return "-";
        }

        var moment = TimeZoneInfo.ConvertTime(timestamp.Value, zone);
        var abbreviation = ZoneAbbreviation(zone, timestamp.Value);
        return $"{moment:yyyy-MM-dd HH:mm} {abbreviation}";
    }

    public static string HumanIsoTimestamp(DateTimeOffset? timestamp)
    {
        return HumanIsoTimestamp(timestamp, TimeZoneInfo.Local);
    }

    public static string HumanIsoTimestamp(DateTimeOffset? timestamp, TimeZoneInfo zone)
    {
        if (timestamp is null)
        {
            return "-";
        }

        var moment = TimeZoneInfo.ConvertTime(timestamp.Value, zone);
        return moment.ToString("yyyy-MM-dd'T'HH:mm:sszzz", CultureInfo.InvariantCulture);
    }

    public static string ZoneAbbreviation(TimeZoneInfo zone, DateTimeOffset moment)
    {
        if (zone.Id is "UTC" or "Etc/UTC" or "Etc/GMT")
        {
            return "UTC";
        }

        var dst = zone.IsDaylightSavingTime(moment);
        var name = dst ? zone.DaylightName : zone.StandardName;
        var abbreviation = BuildZoneAbbreviation(name);
        if (abbreviation.Length is >= 2 and <= 5)
        {
            return abbreviation;
        }

        var offset = TimeZoneInfo.ConvertTime(moment, zone).Offset;
        return offset == TimeSpan.Zero ? "UTC" : $"UTC{offset.Hours:+00;-00}:{Math.Abs(offset.Minutes):00}";
    }

    public static string BuildZoneAbbreviation(string zoneName)
    {
        if (string.IsNullOrWhiteSpace(zoneName))
        {
            return string.Empty;
        }

        var builder = new StringBuilder(6);
        foreach (var word in zoneName.Split(' ', StringSplitOptions.RemoveEmptyEntries))
        {
            if (char.IsLetter(word[0]))
            {
                builder.Append(char.ToUpperInvariant(word[0]));
            }
        }
        return builder.ToString();
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
