using Avalonia;
using Avalonia.Controls;
using Avalonia.Media;
using System.Globalization;

namespace Podlord.App;

internal static class ResourceReferenceTooltipBuilder
{
    public static Control Build(MainWindowViewModel viewModel, string reference)
    {
        var hint = viewModel.T("ref.triggerHint");
        var row = viewModel.ResolveResourceReferenceForPreview(reference);
        var goldBrush = (IBrush)Application.Current!.FindResource("PlGoldBrightBrush")!;
        var mutedBrush = (IBrush)Application.Current!.FindResource("PlTextMutedBrush")!;
        var textBrush = (IBrush)Application.Current!.FindResource("PlTextBrush")!;
        var panelBrush = (IBrush)Application.Current!.FindResource("PlBgPanelBrush")!;
        var edgeBrush = (IBrush)Application.Current!.FindResource("PlPlaqueEdgeBrush")!;

        var stack = new StackPanel { Spacing = 4 };
        if (row is null)
        {
            stack.Children.Add(Title(reference, goldBrush));
            stack.Children.Add(Subtle(viewModel.T("ref.notInCache"), mutedBrush));
        }
        else
        {
            stack.Children.Add(Title($"{row.Kind}/{row.Name}", goldBrush));
            stack.Children.Add(Subtle(row.Namespace ?? "cluster", mutedBrush));
            AppendKv(stack, "Status", row.Status, textBrush, mutedBrush);
            if (row.HasReadyInfo) AppendKv(stack, "Ready", row.Ready, textBrush, mutedBrush);
            if (row.HasRestartInfo) AppendKv(stack, "Restarts", row.Restarts.ToString(CultureInfo.InvariantCulture), textBrush, mutedBrush);
            if (row.HasCpuMetricInfo) AppendKv(stack, "CPU", row.CpuSummaryDisplay, textBrush, mutedBrush);
            if (row.HasMemoryMetricInfo) AppendKv(stack, "Memory", row.MemorySummaryDisplay, textBrush, mutedBrush);
            if (row.HasNodeInfo) AppendKv(stack, "Node", row.Node ?? "-", textBrush, mutedBrush);
            if (row.HasImageInfo) AppendKv(stack, "Image", row.ImageSummary, textBrush, mutedBrush);
            if (row.HasOwnerInfo) AppendKv(stack, "Owner", row.Owner ?? "-", textBrush, mutedBrush);
        }
        stack.Children.Add(new Border
        {
            Margin = new Thickness(0, 6, 0, 0),
            Padding = new Thickness(0, 4, 0, 0),
            BorderBrush = edgeBrush,
            BorderThickness = new Thickness(0, 1, 0, 0),
            Child = new TextBlock
            {
                Text = hint,
                Foreground = mutedBrush,
                FontSize = 11
            }
        });
        return new Border
        {
            MinWidth = 280,
            MaxWidth = 420,
            Padding = new Thickness(10),
            Background = panelBrush,
            BorderBrush = edgeBrush,
            BorderThickness = new Thickness(1),
            Child = stack
        };
    }

    private static TextBlock Title(string text, IBrush brush) => new()
    {
        Text = text,
        Foreground = brush,
        FontWeight = FontWeight.Bold,
        TextWrapping = TextWrapping.Wrap
    };

    private static TextBlock Subtle(string text, IBrush brush) => new()
    {
        Text = text,
        Foreground = brush
    };

    private static void AppendKv(StackPanel parent, string label, string value, IBrush valueBrush, IBrush labelBrush)
    {
        var grid = new Grid { ColumnDefinitions = new ColumnDefinitions("76,*") };
        var key = new TextBlock { Text = label, Foreground = labelBrush };
        var val = new TextBlock { Text = value, Foreground = valueBrush, TextTrimming = TextTrimming.CharacterEllipsis };
        Grid.SetColumn(val, 1);
        grid.Children.Add(key);
        grid.Children.Add(val);
        parent.Children.Add(grid);
    }
}
