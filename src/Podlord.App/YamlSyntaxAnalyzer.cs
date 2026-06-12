namespace Podlord.App;

internal static class YamlSyntaxAnalyzer
{
    public static IReadOnlyList<YamlToken> AnalyzeLine(string text)
    {
        if (text.Length == 0)
        {
            return [];
        }

        var tokens = new List<YamlToken>(3);
        var contentEnd = FindCommentStart(text);
        YamlToken? comment = null;
        if (contentEnd >= 0)
        {
            comment = new YamlToken(contentEnd, text.Length, YamlTokenKind.Comment);
        }
        else
        {
            contentEnd = text.Length;
        }

        var first = FirstNonWhiteSpace(text, contentEnd);
        if (first < 0)
        {
            AddCommentToken(tokens, comment);
            return tokens;
        }

        if (text[first] == '-' && (first + 1 >= contentEnd || char.IsWhiteSpace(text[first + 1])))
        {
            tokens.Add(new YamlToken(first, first + 1, YamlTokenKind.Marker));
            first = FirstNonWhiteSpace(text, contentEnd, first + 1);
            if (first < 0)
            {
                AddCommentToken(tokens, comment);
                return tokens;
            }
        }

        var colon = FindColonOutsideQuotes(text, contentEnd);
        if (colon > first)
        {
            tokens.Add(new YamlToken(first, colon, YamlTokenKind.Key));
            var valueStart = FirstNonWhiteSpace(text, contentEnd, colon + 1);
            if (valueStart >= 0)
            {
                AddScalar(tokens, text, valueStart, contentEnd);
            }

            AddCommentToken(tokens, comment);
            return tokens;
        }

        AddScalar(tokens, text, first, contentEnd);
        AddCommentToken(tokens, comment);
        return tokens;
    }

    private static void AddCommentToken(ICollection<YamlToken> tokens, YamlToken? comment)
    {
        if (comment is { } token)
        {
            tokens.Add(token);
        }
    }

    private static void AddScalar(ICollection<YamlToken> tokens, string text, int start, int end)
    {
        var value = text[start..end].Trim();
        if (value.Length == 0)
        {
            return;
        }

        var kind = IsYamlKeyword(value)
            ? YamlTokenKind.Keyword
            : IsNumber(value)
                ? YamlTokenKind.Number
                : YamlTokenKind.Scalar;
        tokens.Add(new YamlToken(start, end, kind));
    }

    private static int FirstNonWhiteSpace(string text, int end, int start = 0)
    {
        for (var i = Math.Max(0, start); i < end; i++)
        {
            if (!char.IsWhiteSpace(text[i]))
            {
                return i;
            }
        }

        return -1;
    }

    private static int FindCommentStart(string text)
    {
        var quote = '\0';
        for (var i = 0; i < text.Length; i++)
        {
            var ch = text[i];
            if ((ch == '"' || ch == '\'') && (i == 0 || text[i - 1] != '\\'))
            {
                quote = quote == ch ? '\0' : quote == '\0' ? ch : quote;
            }
            else if (ch == '#' && quote == '\0')
            {
                return i;
            }
        }

        return -1;
    }

    private static int FindColonOutsideQuotes(string text, int end)
    {
        var quote = '\0';
        for (var i = 0; i < end; i++)
        {
            var ch = text[i];
            if ((ch == '"' || ch == '\'') && (i == 0 || text[i - 1] != '\\'))
            {
                quote = quote == ch ? '\0' : quote == '\0' ? ch : quote;
            }
            else if (ch == ':' && quote == '\0')
            {
                return i;
            }
        }

        return -1;
    }

    private static bool IsYamlKeyword(string value)
    {
        var normalized = value.Trim('"', '\'');
        return normalized.Equals("true", StringComparison.OrdinalIgnoreCase)
               || normalized.Equals("false", StringComparison.OrdinalIgnoreCase)
               || normalized.Equals("null", StringComparison.OrdinalIgnoreCase)
               || normalized.Equals("~", StringComparison.Ordinal);
    }

    private static bool IsNumber(string value)
    {
        return double.TryParse(value, System.Globalization.NumberStyles.Float, System.Globalization.CultureInfo.InvariantCulture, out _);
    }
}

internal enum YamlTokenKind
{
    Key,
    Scalar,
    Number,
    Keyword,
    Comment,
    Marker
}

internal readonly record struct YamlToken(int Start, int End, YamlTokenKind Kind);
