namespace Podlord.App.LayoutTests;

public sealed class YamlResourceRefTests
{
    [Theory]
    [InlineData("name")]
    [InlineData("Name")]
    [InlineData("namespace")]
    [InlineData("kind")]
    public void Recognizes_resource_ref_keys(string key)
    {
        Assert.True(YamlSyntaxAnalyzer.IsResourceRefKey(key));
    }

    [Theory]
    [InlineData("image")]
    [InlineData("status")]
    [InlineData("")]
    [InlineData(null)]
    public void Rejects_non_ref_keys(string? key)
    {
        Assert.False(YamlSyntaxAnalyzer.IsResourceRefKey(key));
    }

    [Fact]
    public void Tokenizer_marks_name_value_as_resource_ref()
    {
        var tokens = YamlSyntaxAnalyzer.AnalyzeLine("  name: my-pod");
        Assert.Contains(tokens, t => t.Kind == YamlTokenKind.ResourceRef);
    }

    [Fact]
    public void Tokenizer_marks_kind_value_as_resource_ref()
    {
        var tokens = YamlSyntaxAnalyzer.AnalyzeLine("kind: Deployment");
        Assert.Contains(tokens, t => t.Kind == YamlTokenKind.ResourceRef);
    }

    [Fact]
    public void Tokenizer_does_not_mark_other_keys_as_resource_ref()
    {
        var tokens = YamlSyntaxAnalyzer.AnalyzeLine("image: nginx:latest");
        Assert.DoesNotContain(tokens, t => t.Kind == YamlTokenKind.ResourceRef);
    }
}
