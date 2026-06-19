namespace Podlord.App.LayoutTests;

public sealed class LogColorizerTests
{
    [Theory]
    [InlineData("pod/foo-bar-baz", "pod/foo-bar-baz")]
    [InlineData("Crash in Pod/web-7d8 in namespace prod", "Pod/web-7d8")]
    [InlineData("deployment/checkout failed restart", "deployment/checkout")]
    [InlineData("Started Service/billing-api at 12:00", "Service/billing-api")]
    public void Finds_resource_ref_at_position_zero(string line, string expected)
    {
        var index = line.IndexOf(expected, StringComparison.OrdinalIgnoreCase);
        Assert.True(index >= 0);
        var found = LogSyntaxColorizer.FindResourceRefAt(line, index + 1);
        Assert.Equal(expected, found, ignoreCase: true);
    }

    [Fact]
    public void Returns_null_when_column_outside_any_ref()
    {
        var line = "no refs here at all";
        Assert.Null(LogSyntaxColorizer.FindResourceRefAt(line, 5));
    }

    [Fact]
    public void Skips_kind_without_slash()
    {
        var line = "pod was rescheduled";
        Assert.Null(LogSyntaxColorizer.FindResourceRefAt(line, 1));
    }
}
