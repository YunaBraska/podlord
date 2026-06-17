using Avalonia.Media;
using AvaloniaEdit.Document;
using AvaloniaEdit.Rendering;

namespace Podlord.App;

internal sealed class YamlSyntaxColorizer : DocumentColorizingTransformer
{
    private static readonly IBrush KeyBrush = SolidColorBrush.Parse("#6ec6ff");
    private static readonly IBrush ScalarBrush = SolidColorBrush.Parse("#d6c39a");
    private static readonly IBrush NumberBrush = SolidColorBrush.Parse("#7ddc8d");
    private static readonly IBrush KeywordBrush = SolidColorBrush.Parse("#f0c44f");
    private static readonly IBrush CommentBrush = SolidColorBrush.Parse("#6f7f8f");
    private static readonly IBrush MarkerBrush = SolidColorBrush.Parse("#d9a13b");
    private static readonly IBrush ResourceRefBrush = SolidColorBrush.Parse("#7ad8ff");

    protected override void ColorizeLine(DocumentLine line)
    {
        var text = CurrentContext.Document.GetText(line);
        var lineStart = line.Offset;
        foreach (var token in YamlSyntaxAnalyzer.AnalyzeLine(text))
        {
            switch (token.Kind)
            {
                case YamlTokenKind.ResourceRef:
                    PaintRef(lineStart + token.Start, lineStart + token.End, ResourceRefBrush);
                    break;
                default:
                    var brush = token.Kind switch
                    {
                        YamlTokenKind.Key => KeyBrush,
                        YamlTokenKind.Number => NumberBrush,
                        YamlTokenKind.Keyword => KeywordBrush,
                        YamlTokenKind.Comment => CommentBrush,
                        YamlTokenKind.Marker => MarkerBrush,
                        _ => ScalarBrush
                    };
                    Paint(lineStart + token.Start, lineStart + token.End, brush);
                    break;
            }
        }
    }

    private void PaintRef(int start, int end, IBrush brush)
    {
        if (end <= start)
        {
            return;
        }
        ChangeLinePart(start, end, element =>
        {
            element.TextRunProperties.SetForegroundBrush(brush);
            element.TextRunProperties.SetTextDecorations(TextDecorations.Underline);
        });
    }

    private void Paint(int start, int end, IBrush brush)
    {
        if (end <= start)
        {
            return;
        }

        ChangeLinePart(start, end, element => element.TextRunProperties.SetForegroundBrush(brush));
    }

}
