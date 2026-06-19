using Podlord.Core;
using System.Globalization;

namespace Podlord.App.LayoutTests;

public sealed class AgeAndTimestampFormatTests
{
    [Theory]
    [InlineData("27d18h", "27d 18h")]
    [InlineData("5m30s", "5m 30s")]
    [InlineData("2h15m4s", "2h 15m 4s")]
    [InlineData("90s", "90s")]
    [InlineData("", "")]
    [InlineData("10d", "10d")]
    public void Age_inserts_space_between_unit_and_next_value(string raw, string expected)
    {
        Assert.Equal(expected, FlatResourceRow.FormatAgeWithSpaces(raw));
    }

    [Fact]
    public void FormatAgeWithSpaces_preserves_filter_parse_compatibility_when_raw_kept()
    {
        var raw = "5m30s";
        var display = FlatResourceRow.FormatAgeWithSpaces(raw);
        Assert.Equal("5m 30s", display);
        Assert.Equal("5m30s", raw);
    }

    [Fact]
    public void Audit_timestamp_format_is_human_readable()
    {
        var now = new DateTimeOffset(2026, 6, 16, 12, 5, 4, TimeSpan.Zero).AddTicks(412_595);
        var formatted = now.ToUniversalTime().ToString("yyyy-MM-dd HH:mm:ss.ff", CultureInfo.InvariantCulture);
        Assert.Equal("2026-06-16 12:05:04.04", formatted);
        Assert.DoesNotContain("T", formatted);
        Assert.DoesNotContain("Z", formatted);
    }
}
