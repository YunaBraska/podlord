using System.Collections.Specialized;
using System.ComponentModel;
using System.Diagnostics.CodeAnalysis;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Input;
using Avalonia.Media;
using Avalonia.Threading;

namespace Podlord.App;

[ExcludeFromCodeCoverage(Justification = "Custom Avalonia paint control; behavior is covered through visual model and headless UI tests while pixel rendering is verified manually.")]
public sealed class RadarBlockLayer : Control
{
    public static readonly StyledProperty<IEnumerable<RadarBlockViewModel>?> BlocksProperty =
        AvaloniaProperty.Register<RadarBlockLayer, IEnumerable<RadarBlockViewModel>?>(nameof(Blocks));

    private readonly DispatcherTimer animationTimer = new() { Interval = TimeSpan.FromMilliseconds(120) };
    private IEnumerable<RadarBlockViewModel>? subscribedBlocks;
    private INotifyCollectionChanged? subscribedCollection;
    private RadarBlockViewModel? hoveredBlock;
    private int animationPhase;
    private bool isAttached;

    static RadarBlockLayer()
    {
        AffectsRender<RadarBlockLayer>(BlocksProperty);
    }

    public RadarBlockLayer()
    {
        ClipToBounds = true;
        ToolTip.SetShowDelay(this, 0);
        animationTimer.Tick += (_, _) =>
        {
            animationPhase = (animationPhase + 1) & 0xff;
            InvalidateVisual();
        };
    }

    public IEnumerable<RadarBlockViewModel>? Blocks
    {
        get => GetValue(BlocksProperty);
        set => SetValue(BlocksProperty, value);
    }

    public RadarBlockViewModel? HitTestBlock(Point point)
    {
        if (Blocks is null)
        {
            return null;
        }

        foreach (var block in Blocks.Reverse())
        {
            if (block.IsPlaceholder || !block.IsClickable)
            {
                continue;
            }

            var rect = new Rect(block.X, block.Y, block.Width, block.Height);
            if (rect.Contains(point))
            {
                return block;
            }
        }

        return null;
    }

    protected override void OnPropertyChanged(AvaloniaPropertyChangedEventArgs change)
    {
        base.OnPropertyChanged(change);
        if (change.Property == BlocksProperty)
        {
            SubscribeBlocks(Blocks);
            InvalidateVisual();
            SyncAnimationTimer();
        }
        else if (change.Property == IsVisibleProperty)
        {
            SyncAnimationTimer();
        }
    }

    protected override void OnAttachedToVisualTree(VisualTreeAttachmentEventArgs e)
    {
        base.OnAttachedToVisualTree(e);
        isAttached = true;
        SubscribeBlocks(Blocks);
        SyncAnimationTimer();
    }

    protected override void OnDetachedFromVisualTree(VisualTreeAttachmentEventArgs e)
    {
        isAttached = false;
        animationTimer.Stop();
        SubscribeBlocks(null);
        base.OnDetachedFromVisualTree(e);
    }

    protected override void OnPointerMoved(PointerEventArgs e)
    {
        base.OnPointerMoved(e);
        var block = HitTestBlock(e.GetPosition(this));
        if (ReferenceEquals(block, hoveredBlock))
        {
            return;
        }

        hoveredBlock = block;
        ToolTip.SetTip(this, block is null ? null : BuildTooltip(block));
        InvalidateVisual();
    }

    protected override void OnPointerExited(PointerEventArgs e)
    {
        base.OnPointerExited(e);
        hoveredBlock = null;
        ToolTip.SetTip(this, null);
        InvalidateVisual();
    }

    public override void Render(DrawingContext context)
    {
        base.Render(context);
        if (Blocks is null)
        {
            return;
        }

        foreach (var block in Blocks)
        {
            DrawBlock(context, block);
        }
    }

    private void SubscribeBlocks(IEnumerable<RadarBlockViewModel>? blocks)
    {
        if (ReferenceEquals(subscribedBlocks, blocks))
        {
            return;
        }

        UnsubscribeCurrentBlocks();
        subscribedBlocks = blocks;
        subscribedCollection = blocks as INotifyCollectionChanged;
        if (subscribedCollection is not null)
        {
            subscribedCollection.CollectionChanged += BlocksCollectionChanged;
        }

        if (blocks is null)
        {
            return;
        }

        foreach (var block in blocks)
        {
            block.PropertyChanged += BlockPropertyChanged;
        }
    }

    private void UnsubscribeCurrentBlocks()
    {
        if (subscribedCollection is not null)
        {
            subscribedCollection.CollectionChanged -= BlocksCollectionChanged;
            subscribedCollection = null;
        }

        if (subscribedBlocks is not null)
        {
            foreach (var block in subscribedBlocks)
            {
                block.PropertyChanged -= BlockPropertyChanged;
            }
        }

        subscribedBlocks = null;
    }

    private void BlocksCollectionChanged(object? sender, NotifyCollectionChangedEventArgs e)
    {
        if (e.OldItems is not null)
        {
            foreach (RadarBlockViewModel block in e.OldItems)
            {
                block.PropertyChanged -= BlockPropertyChanged;
            }
        }

        if (e.NewItems is not null)
        {
            foreach (RadarBlockViewModel block in e.NewItems)
            {
                block.PropertyChanged += BlockPropertyChanged;
            }
        }

        if (e.Action == NotifyCollectionChangedAction.Reset)
        {
            SubscribeBlocks(null);
            SubscribeBlocks(Blocks);
        }

        hoveredBlock = Blocks?.Contains(hoveredBlock) == true ? hoveredBlock : null;
        InvalidateVisual();
        SyncAnimationTimer();
    }

    private void BlockPropertyChanged(object? sender, PropertyChangedEventArgs e)
    {
        InvalidateVisual();
        SyncAnimationTimer();
    }

    private void SyncAnimationTimer()
    {
        var shouldRun = isAttached
                        && IsVisible
                        && Blocks?.Any(RequiresAnimationTimer) == true;
        if (shouldRun)
        {
            if (!animationTimer.IsEnabled)
            {
                animationTimer.Start();
            }

            return;
        }

        animationTimer.Stop();
    }

    private static bool RequiresAnimationTimer(RadarBlockViewModel block)
    {
        return block.IsBlinkAnimation || block.IsPulseAnimation || block.IsSweepAnimation;
    }

    private void DrawBlock(DrawingContext context, RadarBlockViewModel block)
    {
        var rect = new Rect(block.X, block.Y, block.Width, block.Height);
        if (rect.Width <= 0 || rect.Height <= 0 || !Bounds.Intersects(rect))
        {
            return;
        }

        context.DrawRectangle(block.Brush, new Pen(block.Brush, Math.Max(0.5, block.BorderThickness)), rect);
        if (ReferenceEquals(block, hoveredBlock))
        {
            context.DrawRectangle(null, new Pen(AppThemeCatalog.StatusBrush("Fresh"), 1.2), rect.Deflate(-1));
        }

        if (block.IsAnnouncing)
        {
            DrawAnnouncement(context, block, rect);
        }

        if (block.ShowProblemGlyph && rect.Width >= 9 && rect.Height >= 9)
        {
            DrawKindGlyph(context, block.DisplayKind, CenterRect(rect, Math.Min(rect.Width, 14), Math.Min(rect.Height, 14)), ThemeBrush("PlBgPanelInsetBrush", "#050806"), block.Brush);
        }
    }

    private void DrawAnnouncement(DrawingContext context, RadarBlockViewModel block, Rect rect)
    {
        var tick = animationPhase % 10;
        if (block.IsBlinkAnimation && tick < 5)
        {
            using var blinkOpacity = context.PushOpacity(0.36);
            context.DrawRectangle(block.AnnounceBrush, null, rect);
            return;
        }

        if (block.IsSweepAnimation)
        {
            var x = rect.X + rect.Width * (animationPhase % 14) / 13d;
            context.DrawRectangle(block.AnnounceBrush, null, new Rect(x, rect.Y, Math.Max(1.5, rect.Width * 0.12), rect.Height));
            return;
        }

        if (block.IsOutlineAnimation)
        {
            context.DrawRectangle(null, new Pen(block.AnnounceBrush, 2), rect.Deflate(-1));
            return;
        }

        if (block.IsPulseAnimation || (!block.IsBlinkAnimation && !block.IsSweepAnimation && !block.IsOutlineAnimation))
        {
            var pulse = 0.18 + 0.22 * Math.Abs(Math.Sin(animationPhase * Math.PI / 8d));
            using var pulseOpacity = context.PushOpacity(pulse);
            context.DrawRectangle(block.AnnounceBrush, null, rect);
        }
    }

    private static Control BuildTooltip(RadarBlockViewModel block)
    {
        var stack = new StackPanel { Spacing = 4 };
        stack.Children.Add(new TextBlock
        {
            Text = block.ToolTipTitle,
            Foreground = ThemeBrush("PlGoldBrightBrush", "#D2A246"),
            FontWeight = FontWeight.Bold,
            TextWrapping = TextWrapping.WrapWithOverflow
        });
        stack.Children.Add(new TextBlock
        {
            Text = block.ToolTipNamespace,
            Foreground = ThemeBrush("PlTextMutedBrush", "#9B8F76"),
            TextWrapping = TextWrapping.WrapWithOverflow
        });
        AddTextRow(stack, "Status", block.Resource.Status, AppThemeCatalog.StatusBrush(block.Resource.Status));
        if (block.Resource.HasReadyInfo) AddTextRow(stack, "Ready", block.Resource.Ready, AppThemeCatalog.TextBrush());
        if (block.Resource.HasRestartInfo) AddTextRow(stack, "Restarts", block.Resource.Restarts.ToString(), AppThemeCatalog.TextBrush());
        if (block.Resource.HasCpuMetricBar) AddMetricRow(stack, "CPU", block.Resource.CpuSummaryDisplay, block.Resource.Pulse.CpuPercent);
        else if (block.Resource.HasCpuMetricTextOnly) AddTextRow(stack, "CPU", block.Resource.CpuSummaryDisplay, AppThemeCatalog.TextBrush());
        if (block.Resource.HasMemoryMetricBar) AddMetricRow(stack, "Memory", block.Resource.MemorySummaryDisplay, block.Resource.Pulse.MemoryPercent);
        else if (block.Resource.HasMemoryMetricTextOnly) AddTextRow(stack, "Memory", block.Resource.MemorySummaryDisplay, AppThemeCatalog.TextBrush());
        if (block.Resource.HasStorageMetricBar) AddMetricRow(stack, "Storage", block.Resource.StorageDisplay, block.Resource.Pulse.StoragePercent);
        else if (block.Resource.HasStorageMetricTextOnly) AddTextRow(stack, "Storage", block.Resource.StorageDisplay, AppThemeCatalog.TextBrush());
        if (block.Resource.HasNodeInfo) AddTextRow(stack, "Node", block.Resource.Node, AppThemeCatalog.TextBrush());
        if (block.Resource.HasImageInfo) AddTextRow(stack, "Image", block.Resource.ImageSummary, AppThemeCatalog.TextBrush());
        if (block.Resource.HasOwnerInfo) AddTextRow(stack, "Owner", block.Resource.Owner, AppThemeCatalog.TextBrush());
        if (!string.IsNullOrWhiteSpace(block.Problem)) AddTextRow(stack, "Issue", block.Problem, AppThemeCatalog.StatusBrush("WARNING"));

        return new Border
        {
            MinWidth = 300,
            MaxWidth = 440,
            Padding = new Thickness(10),
            Background = ThemeBrush("PlBgPanelBrush", "#101612"),
            BorderBrush = block.Brush,
            BorderThickness = new Thickness(1),
            Child = stack
        };
    }

    private static void AddTextRow(Panel panel, string key, string? value, IBrush brush)
    {
        if (string.IsNullOrWhiteSpace(value) || value == "-")
        {
            return;
        }

        panel.Children.Add(new Grid
        {
            ColumnDefinitions =
            {
                new ColumnDefinition(76, GridUnitType.Pixel),
                new ColumnDefinition(1, GridUnitType.Star)
            },
            ColumnSpacing = 8,
            Children =
            {
                new TextBlock { Text = key, Foreground = ThemeBrush("PlTextMutedBrush", "#9B8F76") },
                new TextBlock { Text = value, Foreground = brush, TextWrapping = TextWrapping.WrapWithOverflow, [Grid.ColumnProperty] = 1 }
            }
        });
    }

    private static void AddMetricRow(Panel panel, string key, string value, double percent)
    {
        var grid = new Grid
        {
            RowDefinitions =
            {
                new RowDefinition(GridLength.Auto),
                new RowDefinition(GridLength.Auto)
            },
            ColumnDefinitions =
            {
                new ColumnDefinition(76, GridUnitType.Pixel),
                new ColumnDefinition(1, GridUnitType.Star),
                new ColumnDefinition(GridLength.Auto)
            },
            ColumnSpacing = 8
        };
        grid.Children.Add(new TextBlock { Text = key, Foreground = ThemeBrush("PlTextMutedBrush", "#9B8F76") });
        grid.Children.Add(new TextBlock { Text = value, Foreground = AppThemeCatalog.TextBrush(), [Grid.ColumnProperty] = 2 });
        grid.Children.Add(new ProgressBar
        {
            Minimum = 0,
            Maximum = 100,
            Value = percent,
            Height = 6,
            Foreground = MetricBrush(percent),
            [Grid.RowProperty] = 1,
            [Grid.ColumnProperty] = 1,
            [Grid.ColumnSpanProperty] = 2,
            Margin = new Thickness(0, 2, 0, 0)
        });
        panel.Children.Add(grid);
    }

    private static void DrawKindGlyph(DrawingContext context, string kind, Rect rect, IBrush fill, IBrush stroke)
    {
        var pen = new Pen(stroke, Math.Max(1, rect.Width * 0.08));
        switch (kind)
        {
            case "Pod":
                context.DrawGeometry(fill, pen, Polygon(P(rect, 0.5, 0.06), P(rect, 0.88, 0.28), P(rect, 0.88, 0.72), P(rect, 0.5, 0.94), P(rect, 0.12, 0.72), P(rect, 0.12, 0.28)));
                context.DrawEllipse(stroke, null, P(rect, 0.5, 0.5), rect.Width * 0.12, rect.Height * 0.12);
                break;
            case "Event":
                context.DrawGeometry(fill, pen, Polygon(P(rect, 0.58, 0.04), P(rect, 0.18, 0.56), P(rect, 0.48, 0.56), P(rect, 0.36, 0.96), P(rect, 0.82, 0.42), P(rect, 0.52, 0.42)));
                break;
            case "Service":
                context.DrawEllipse(fill, pen, P(rect, 0.5, 0.2), rect.Width * 0.14, rect.Height * 0.14);
                context.DrawEllipse(fill, pen, P(rect, 0.2, 0.75), rect.Width * 0.12, rect.Height * 0.12);
                context.DrawEllipse(fill, pen, P(rect, 0.8, 0.75), rect.Width * 0.12, rect.Height * 0.12);
                context.DrawLine(pen, P(rect, 0.5, 0.34), P(rect, 0.2, 0.63));
                context.DrawLine(pen, P(rect, 0.5, 0.34), P(rect, 0.8, 0.63));
                break;
            case "Namespace":
                context.DrawRectangle(fill, pen, rect);
                context.DrawLine(pen, P(rect, 0.5, 0.1), P(rect, 0.5, 0.9));
                context.DrawLine(pen, P(rect, 0.1, 0.5), P(rect, 0.9, 0.5));
                break;
            case "Node":
                context.DrawRectangle(fill, pen, R(rect, 0.12, 0.32, 0.76, 0.46));
                context.DrawRectangle(stroke, null, R(rect, 0.25, 0.48, 0.12, 0.12));
                context.DrawRectangle(stroke, null, R(rect, 0.44, 0.48, 0.12, 0.12));
                context.DrawRectangle(stroke, null, R(rect, 0.63, 0.48, 0.12, 0.12));
                break;
            case "Deployment":
                context.DrawGeometry(fill, pen, Polygon(P(rect, 0.1, 0.82), P(rect, 0.1, 0.42), P(rect, 0.32, 0.26), P(rect, 0.44, 0.42), P(rect, 0.62, 0.28), P(rect, 0.78, 0.44), P(rect, 0.9, 0.44), P(rect, 0.9, 0.82)));
                break;
            case "Secret":
                context.DrawRectangle(fill, pen, R(rect, 0.18, 0.44, 0.64, 0.42));
                context.DrawLine(pen, P(rect, 0.32, 0.44), P(rect, 0.32, 0.30));
                context.DrawLine(pen, P(rect, 0.68, 0.44), P(rect, 0.68, 0.30));
                context.DrawLine(pen, P(rect, 0.32, 0.30), P(rect, 0.68, 0.30));
                break;
            case "ConfigMap":
                context.DrawRectangle(fill, pen, R(rect, 0.2, 0.08, 0.62, 0.84));
                context.DrawLine(pen, P(rect, 0.32, 0.34), P(rect, 0.72, 0.34));
                context.DrawLine(pen, P(rect, 0.32, 0.54), P(rect, 0.72, 0.54));
                break;
            default:
                context.DrawGeometry(fill, pen, Polygon(P(rect, 0.5, 0.06), P(rect, 0.94, 0.5), P(rect, 0.5, 0.94), P(rect, 0.06, 0.5)));
                break;
        }
    }

    private static Rect CenterRect(Rect rect, double width, double height)
    {
        return new Rect(rect.X + (rect.Width - width) / 2, rect.Y + (rect.Height - height) / 2, width, height);
    }

    private static Point P(Rect r, double x, double y) => new(r.X + r.Width * x, r.Y + r.Height * y);

    private static Rect R(Rect r, double x, double y, double w, double h) => new(r.X + r.Width * x, r.Y + r.Height * y, r.Width * w, r.Height * h);

    private static StreamGeometry Polygon(params Point[] points)
    {
        var geometry = new StreamGeometry();
        using var ctx = geometry.Open();
        ctx.BeginFigure(points[0], isFilled: true);
        for (var i = 1; i < points.Length; i++)
        {
            ctx.LineTo(points[i]);
        }
        ctx.EndFigure(isClosed: true);
        return geometry;
    }

    private static IBrush MetricBrush(double percent)
    {
        return percent switch
        {
            >= 90 => AppThemeCatalog.StatusBrush("CRITICAL"),
            >= 70 => AppThemeCatalog.StatusBrush("WARNING"),
            _ => AppThemeCatalog.StatusBrush("HEALTHY")
        };
    }

    private static IBrush ThemeBrush(string key, string fallback)
    {
        return Application.Current?.Resources.TryGetResource(key, null, out var value) == true
               && value is IBrush brush
            ? brush
            : SolidColorBrush.Parse(fallback);
    }
}
