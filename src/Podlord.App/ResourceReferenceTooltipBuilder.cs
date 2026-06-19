using Avalonia;
using Avalonia.Controls;
using Avalonia.Controls.Shapes;
using Avalonia.Layout;
using Avalonia.Media;
using Podlord.Core;
using System.Globalization;

namespace Podlord.App;

internal static class ResourceReferenceTooltipBuilder
{
    private const double BarWidth = 154d;

    public static Control Build(MainWindowViewModel viewModel, string reference)
    {
        var hint = viewModel.T("ref.triggerHint");
        var row = viewModel.ResolveResourceReferenceForPreview(reference);
        var goldBrush = (IBrush)Application.Current!.FindResource("PlGoldBrightBrush")!;
        var mutedBrush = (IBrush)Application.Current!.FindResource("PlTextMutedBrush")!;
        var textBrush = (IBrush)Application.Current!.FindResource("PlTextBrush")!;
        var panelBrush = (IBrush)Application.Current!.FindResource("PlBgPanelBrush")!;
        var edgeBrush = (IBrush)Application.Current!.FindResource("PlPlaqueEdgeBrush")!;
        var trackBrush = (IBrush)Application.Current!.FindResource("PlProgressTrackBrush")!;

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
            if (row.HasCpuMetricBar)
            {
                var cpuSuggestionPct = MainWindowViewModel.SuggestionRatioPercent("CPU", row.CpuSummaryDisplay, row.Pulse.CpuLimitSuggestion) ?? 0;
                AppendMetricRow(stack, "CPU", row.CpuSummaryDisplay, row.Pulse.CpuPercent, cpuSuggestionPct, textBrush, mutedBrush, trackBrush);
            }
            else if (row.HasCpuMetricInfo)
            {
                AppendKv(stack, "CPU", row.CpuSummaryDisplay, textBrush, mutedBrush);
            }
            if (row.HasMemoryMetricBar)
            {
                var memSuggestionPct = MainWindowViewModel.SuggestionRatioPercent("Memory", row.MemorySummaryDisplay, row.Pulse.MemoryLimitSuggestion) ?? 0;
                AppendMetricRow(stack, "Memory", row.MemorySummaryDisplay, row.Pulse.MemoryPercent, memSuggestionPct, textBrush, mutedBrush, trackBrush);
            }
            else if (row.HasMemoryMetricInfo)
            {
                AppendKv(stack, "Memory", row.MemorySummaryDisplay, textBrush, mutedBrush);
            }
            if (row.HasStorageMetricBar)
            {
                AppendMetricRow(stack, "Storage", row.StorageDisplay, row.Pulse.StoragePercent, 0, textBrush, mutedBrush, trackBrush);
            }
            else if (row.HasStorageMetricInfo)
            {
                AppendKv(stack, "Storage", row.StorageDisplay, textBrush, mutedBrush);
            }
            if (row.HasNetworkMetricInfo) AppendKv(stack, "Network", row.NetworkDisplay, textBrush, mutedBrush);
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

    private static void AppendMetricRow(StackPanel parent, string label, string value, double percent, double suggestionPercent, IBrush valueBrush, IBrush labelBrush, IBrush trackBrush)
    {
        var grid = new Grid
        {
            ColumnDefinitions = new ColumnDefinitions("76,*"),
            RowDefinitions = new RowDefinitions("Auto,Auto")
        };
        var key = new TextBlock { Text = label, Foreground = labelBrush };
        var val = new TextBlock { Text = value, Foreground = valueBrush, TextTrimming = TextTrimming.CharacterEllipsis };
        Grid.SetColumn(val, 1);
        grid.Children.Add(key);
        grid.Children.Add(val);

        var bar = BuildBar(percent, suggestionPercent, trackBrush);
        Grid.SetRow(bar, 1);
        Grid.SetColumn(bar, 1);
        Grid.SetColumnSpan(bar, 2);
        grid.Children.Add(bar);
        parent.Children.Add(grid);
    }

    private static Control BuildBar(double percent, double suggestionPercent, IBrush trackBrush)
    {
        var clamped = Math.Clamp(percent, 0, 100);
        var canvas = new Canvas
        {
            Width = BarWidth,
            Height = 8,
            HorizontalAlignment = HorizontalAlignment.Left,
            Margin = new Thickness(0, 2, 0, 0)
        };
        canvas.Children.Add(new Rectangle
        {
            Width = BarWidth,
            Height = 8,
            Fill = trackBrush
        });
        canvas.Children.Add(new Rectangle
        {
            Width = Math.Max(0, BarWidth * clamped / 100d),
            Height = 8,
            Fill = AppThemeCatalog.StatusBrush(StatusFor(clamped))
        });

        if (suggestionPercent > 0)
        {
            var marker = new Rectangle
            {
                Width = 2,
                Height = 12,
                Fill = (IBrush)Application.Current!.FindResource("PlAccentBrush")!
            };
            Canvas.SetLeft(marker, Math.Clamp(BarWidth * suggestionPercent / 100d - 1, 0, BarWidth - 2));
            Canvas.SetTop(marker, -2);
            canvas.Children.Add(marker);
        }

        return canvas;
    }

    private static string StatusFor(double percent) =>
        percent switch
        {
            >= 90 => "CRITICAL",
            >= 70 => "WARNING",
            _ => "HEALTHY"
        };
}
