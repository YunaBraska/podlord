using Avalonia.Media;
using AvaloniaEdit.Document;
using AvaloniaEdit.Rendering;
using System.Text.RegularExpressions;

namespace Podlord.App;

internal sealed partial class LogSyntaxColorizer : DocumentColorizingTransformer
{
    private static readonly IBrush ErrorBrush = SolidColorBrush.Parse("#ff6b6b");
    private static readonly IBrush WarnBrush = SolidColorBrush.Parse("#f0c44f");
    private static readonly IBrush InfoBrush = SolidColorBrush.Parse("#7ddc8d");
    private static readonly IBrush DebugBrush = SolidColorBrush.Parse("#9aa5b3");
    private static readonly IBrush TimestampBrush = SolidColorBrush.Parse("#7ad8ff");
    private static readonly IBrush ResourceRefBrush = SolidColorBrush.Parse("#7ad8ff");

    private static readonly IReadOnlyDictionary<int, IBrush> AnsiPalette = new Dictionary<int, IBrush>
    {
        [30] = SolidColorBrush.Parse("#808080"),
        [31] = SolidColorBrush.Parse("#ff6b6b"),
        [32] = SolidColorBrush.Parse("#7ddc8d"),
        [33] = SolidColorBrush.Parse("#f0c44f"),
        [34] = SolidColorBrush.Parse("#7ad8ff"),
        [35] = SolidColorBrush.Parse("#d18ff5"),
        [36] = SolidColorBrush.Parse("#79e0e0"),
        [37] = SolidColorBrush.Parse("#dcdcdc"),
        [90] = SolidColorBrush.Parse("#9aa5b3"),
        [91] = SolidColorBrush.Parse("#ff8585"),
        [92] = SolidColorBrush.Parse("#a3f0b3"),
        [93] = SolidColorBrush.Parse("#ffd66b"),
        [94] = SolidColorBrush.Parse("#a8e5ff"),
        [95] = SolidColorBrush.Parse("#e0acff"),
        [96] = SolidColorBrush.Parse("#a3f5f0"),
        [97] = SolidColorBrush.Parse("#ffffff"),
    };

    [GeneratedRegex(@"\x1b\[([0-9;]*)m", RegexOptions.Compiled)]
    private static partial Regex AnsiRegex();

    [GeneratedRegex(@"\b(ERROR|ERR|FATAL|PANIC|WARN(?:ING)?|INFO|DEBUG|TRACE|NOTICE)\b", RegexOptions.IgnoreCase | RegexOptions.Compiled)]
    private static partial Regex SeverityRegex();

    [GeneratedRegex(@"\b\d{4}-\d{2}-\d{2}[T ]\d{2}:\d{2}:\d{2}(?:\.\d{1,9})?(?:Z|[+\-]\d{2}:?\d{2})?\b", RegexOptions.Compiled)]
    private static partial Regex TimestampRegex();

    [GeneratedRegex(@"\b(pod|deployment|service|configmap|secret|namespace|node|job|cronjob|statefulset|daemonset|replicaset|ingress)/([a-z0-9][a-z0-9.\-]{0,252})\b", RegexOptions.IgnoreCase | RegexOptions.Compiled)]
    private static partial Regex ResourceRefRegex();

    private readonly Func<string, bool> isKnownReference;

    public LogSyntaxColorizer(Func<string, bool> isKnownReference)
    {
        this.isKnownReference = isKnownReference;
    }

    protected override void ColorizeLine(DocumentLine line)
    {
        var text = CurrentContext.Document.GetText(line);
        if (text.Length == 0)
        {
            return;
        }

        var lineStart = line.Offset;

        IBrush? activeBrush = null;
        foreach (Match m in AnsiRegex().Matches(text))
        {
            ChangeLinePart(lineStart + m.Index, lineStart + m.Index + m.Length, element =>
            {
                element.TextRunProperties.SetForegroundBrush(Brushes.Transparent);
            });
            var codes = m.Groups[1].Value.Split(';', StringSplitOptions.RemoveEmptyEntries);
            foreach (var code in codes)
            {
                if (int.TryParse(code, out var n))
                {
                    if (n == 0)
                    {
                        activeBrush = null;
                    }
                    else if (AnsiPalette.TryGetValue(n, out var brush))
                    {
                        activeBrush = brush;
                    }
                }
            }
            if (activeBrush is { } colour)
            {
                ChangeLinePart(lineStart + m.Index + m.Length, lineStart + text.Length, element =>
                {
                    element.TextRunProperties.SetForegroundBrush(colour);
                });
            }
        }

        foreach (Match m in TimestampRegex().Matches(text))
        {
            ChangeLinePart(lineStart + m.Index, lineStart + m.Index + m.Length, element =>
            {
                element.TextRunProperties.SetForegroundBrush(TimestampBrush);
            });
        }

        foreach (Match m in SeverityRegex().Matches(text))
        {
            var token = m.Value.ToUpperInvariant();
            var brush = token switch
            {
                "ERROR" or "ERR" or "FATAL" or "PANIC" => ErrorBrush,
                "WARN" or "WARNING" => WarnBrush,
                "INFO" or "NOTICE" => InfoBrush,
                "DEBUG" or "TRACE" => DebugBrush,
                _ => null
            };
            if (brush is null)
            {
                continue;
            }
            ChangeLinePart(lineStart + m.Index, lineStart + m.Index + m.Length, element =>
            {
                element.TextRunProperties.SetForegroundBrush(brush);
                element.TextRunProperties.SetTypeface(new Typeface(element.TextRunProperties.Typeface.FontFamily, FontStyle.Normal, FontWeight.Bold));
            });
        }

        foreach (Match m in ResourceRefRegex().Matches(text))
        {
            if (!isKnownReference(m.Value))
            {
                continue;
            }
            ChangeLinePart(lineStart + m.Index, lineStart + m.Index + m.Length, element =>
            {
                element.TextRunProperties.SetForegroundBrush(ResourceRefBrush);
                element.TextRunProperties.SetTextDecorations(TextDecorations.Underline);
            });
        }
    }

    public static string? FindResourceRefAt(string lineText, int column)
    {
        foreach (Match m in ResourceRefRegex().Matches(lineText))
        {
            if (column >= m.Index && column <= m.Index + m.Length)
            {
                return m.Value;
            }
        }
        return null;
    }
}
