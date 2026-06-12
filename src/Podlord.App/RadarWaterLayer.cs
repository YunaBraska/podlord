using Avalonia;
using Avalonia.Controls;
using Avalonia.Media;
using Avalonia.Threading;

namespace Podlord.App;

public sealed class RadarWaterLayer : Control
{
    public static readonly StyledProperty<double> PanXProperty =
        AvaloniaProperty.Register<RadarWaterLayer, double>(nameof(PanX));

    public static readonly StyledProperty<double> PanYProperty =
        AvaloniaProperty.Register<RadarWaterLayer, double>(nameof(PanY));

    public static readonly StyledProperty<double> ZoomProperty =
        AvaloniaProperty.Register<RadarWaterLayer, double>(nameof(Zoom), 1d);

    public static readonly StyledProperty<int> ActivityRateProperty =
        AvaloniaProperty.Register<RadarWaterLayer, int>(nameof(ActivityRate));

    public static readonly StyledProperty<int> SpeedPercentProperty =
        AvaloniaProperty.Register<RadarWaterLayer, int>(nameof(SpeedPercent), 45);

    public static readonly StyledProperty<bool> PauseAnimationProperty =
        AvaloniaProperty.Register<RadarWaterLayer, bool>(nameof(PauseAnimation));

    private static readonly IBrush DeepWater = SolidColorBrush.Parse("#15071C27");
    private static readonly IBrush MidWater = SolidColorBrush.Parse("#2A205766");
    private static readonly IBrush LightWater = SolidColorBrush.Parse("#302E7282");
    private static readonly IBrush WakeWater = SolidColorBrush.Parse("#36359394");
    private readonly DispatcherTimer timer = new() { Interval = TimeSpan.FromMilliseconds(1_800) };
    private bool isAttached;
    private int phase;

    public RadarWaterLayer()
    {
        IsHitTestVisible = false;
        timer.Tick += (_, _) =>
        {
            phase = (phase + 1) & 0x3ff;
            InvalidateVisual();
        };
    }

    public double PanX
    {
        get => GetValue(PanXProperty);
        set => SetValue(PanXProperty, value);
    }

    public double PanY
    {
        get => GetValue(PanYProperty);
        set => SetValue(PanYProperty, value);
    }

    public double Zoom
    {
        get => GetValue(ZoomProperty);
        set => SetValue(ZoomProperty, value);
    }

    public int ActivityRate
    {
        get => GetValue(ActivityRateProperty);
        set => SetValue(ActivityRateProperty, value);
    }

    public int SpeedPercent
    {
        get => GetValue(SpeedPercentProperty);
        set => SetValue(SpeedPercentProperty, value);
    }

    public bool PauseAnimation
    {
        get => GetValue(PauseAnimationProperty);
        set => SetValue(PauseAnimationProperty, value);
    }

    protected override void OnPropertyChanged(AvaloniaPropertyChangedEventArgs change)
    {
        base.OnPropertyChanged(change);
        if (change.Property == IsVisibleProperty || change.Property == PauseAnimationProperty)
        {
            SyncTimer();
        }
        else if (change.Property == ActivityRateProperty || change.Property == SpeedPercentProperty)
        {
            SyncTimer();
            UpdateTimerInterval();
            InvalidateVisual();
        }
        else if (change.Property == PanXProperty || change.Property == PanYProperty || change.Property == ZoomProperty)
        {
            InvalidateVisual();
        }
    }

    protected override void OnAttachedToVisualTree(VisualTreeAttachmentEventArgs e)
    {
        base.OnAttachedToVisualTree(e);
        isAttached = true;
        SyncTimer();
    }

    protected override void OnDetachedFromVisualTree(VisualTreeAttachmentEventArgs e)
    {
        isAttached = false;
        timer.Stop();
        base.OnDetachedFromVisualTree(e);
    }

    public override void Render(DrawingContext context)
    {
        base.Render(context);
        var width = Bounds.Width;
        var height = Bounds.Height;
        if (width <= 0 || height <= 0)
        {
            return;
        }

        var speedPercent = Math.Clamp(SpeedPercent, 0, 100);
        if (speedPercent <= 0)
        {
            return;
        }

        context.DrawRectangle(DeepWater, null, new Rect(0, 0, width, height));
        foreach (var tile in RadarWaterModel.BuildTiles(width, height, PanX, PanY, Zoom, ActivityRate, speedPercent, phase))
        {
            var brush = tile.Kind switch
            {
                0 => LightWater,
                1 => MidWater,
                _ => WakeWater
            };

            context.DrawRectangle(brush, null, tile.Bounds);
        }
    }

    private void SyncTimer()
    {
        if (IsVisible && isAttached && SpeedPercent > 0 && !PauseAnimation)
        {
            UpdateTimerInterval();
            if (!timer.IsEnabled)
            {
                timer.Start();
            }

            return;
        }

        timer.Stop();
    }

    private void UpdateTimerInterval()
    {
        var next = RadarWaterModel.WaterIntervalFor(ActivityRate, SpeedPercent);
        if (timer.Interval != next)
        {
            timer.Interval = next;
        }
    }
}
