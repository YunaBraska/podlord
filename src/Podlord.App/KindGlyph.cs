using Avalonia;
using Avalonia.Controls;
using Avalonia.Media;

namespace Podlord.App;

public sealed class KindGlyph : Control
{
    public static readonly StyledProperty<string> KindProperty =
        AvaloniaProperty.Register<KindGlyph, string>(nameof(Kind), string.Empty);

    public static readonly StyledProperty<IBrush> FillProperty =
        AvaloniaProperty.Register<KindGlyph, IBrush>(nameof(Fill), Brushes.LimeGreen);

    public static readonly StyledProperty<IBrush> StrokeProperty =
        AvaloniaProperty.Register<KindGlyph, IBrush>(nameof(Stroke), SolidColorBrush.Parse("#050806"));

    static KindGlyph()
    {
        AffectsRender<KindGlyph>(KindProperty, FillProperty, StrokeProperty);
    }

    public string Kind
    {
        get => GetValue(KindProperty);
        set => SetValue(KindProperty, value);
    }

    public IBrush Fill
    {
        get => GetValue(FillProperty);
        set => SetValue(FillProperty, value);
    }

    public IBrush Stroke
    {
        get => GetValue(StrokeProperty);
        set => SetValue(StrokeProperty, value);
    }

    private AvaloniaObject? observedFill;
    private AvaloniaObject? observedStroke;

    /// <summary>
    /// Theme switching mutates the color of shared brush instances in place (e.g. <see cref="SolidColorBrush.Color"/>)
    /// rather than swapping the brush reference, so a bound <see cref="Fill"/> brush raises no change on this control.
    /// We observe the brush object's property changes and repaint so the glyph follows the active light/dark palette.
    /// </summary>
    protected override void OnPropertyChanged(AvaloniaPropertyChangedEventArgs change)
    {
        base.OnPropertyChanged(change);
        if (change.Property == FillProperty)
        {
            Resubscribe(ref observedFill, Fill);
        }
        else if (change.Property == StrokeProperty)
        {
            Resubscribe(ref observedStroke, Stroke);
        }
    }

    protected override void OnAttachedToVisualTree(VisualTreeAttachmentEventArgs e)
    {
        base.OnAttachedToVisualTree(e);
        Resubscribe(ref observedFill, Fill);
        Resubscribe(ref observedStroke, Stroke);
    }

    protected override void OnDetachedFromVisualTree(VisualTreeAttachmentEventArgs e)
    {
        base.OnDetachedFromVisualTree(e);
        Resubscribe(ref observedFill, null);
        Resubscribe(ref observedStroke, null);
    }

    private void Resubscribe(ref AvaloniaObject? current, IBrush? brush)
    {
        if (current is not null)
        {
            current.PropertyChanged -= OnBrushPropertyChanged;
        }

        current = brush as AvaloniaObject;
        if (current is not null)
        {
            current.PropertyChanged += OnBrushPropertyChanged;
        }
    }

    private void OnBrushPropertyChanged(object? sender, AvaloniaPropertyChangedEventArgs e)
    {
        InvalidateVisual();
    }

    public override void Render(DrawingContext context)
    {
        base.Render(context);
        var side = Math.Min(Bounds.Width, Bounds.Height);
        if (side <= 0)
        {
            return;
        }

        var rect = new Rect((Bounds.Width - side) / 2, (Bounds.Height - side) / 2, side, side).Deflate(side * 0.08);
        var pen = new Pen(Stroke, Math.Max(1, side * 0.07));
        switch (Kind)
        {
            case "Cluster":
                DrawCluster(context, rect, pen);
                break;
            case "Namespace":
                DrawNamespace(context, rect, pen);
                break;
            case "Node":
                DrawNode(context, rect, pen);
                break;
            case "Pod":
                DrawPod(context, rect, pen);
                break;
            case "Deployment":
                DrawFactory(context, rect, pen);
                break;
            case "ReplicaSet":
                DrawStack(context, rect, pen);
                break;
            case "StatefulSet":
            case "PersistentVolume":
            case "PersistentVolumeClaim":
                DrawDisks(context, rect, pen);
                break;
            case "DaemonSet":
                DrawDaemon(context, rect, pen);
                break;
            case "Job":
                DrawJob(context, rect, pen);
                break;
            case "CronJob":
                DrawClock(context, rect, pen);
                break;
            case "Service":
                DrawRelay(context, rect, pen);
                break;
            case "Ingress":
            case "Gateway":
            case "HTTPRoute":
            case "GRPCRoute":
                DrawGate(context, rect, pen);
                break;
            case "EndpointSlice":
                DrawEndpointSlice(context, rect, pen);
                break;
            case "NetworkPolicy":
                DrawShield(context, rect, pen);
                break;
            case "ConfigMap":
                DrawDocument(context, rect, pen);
                break;
            case "Secret":
                DrawLock(context, rect, pen);
                break;
            case "ServiceAccount":
                DrawUser(context, rect, pen);
                break;
            case "Event":
                DrawBolt(context, rect, pen);
                break;
            case "CustomResourceDefinition":
                DrawCube(context, rect, pen);
                break;
            case "Trash":
                DrawTrash(context, rect, pen);
                break;
            case "Pencil":
                DrawPencil(context, rect, pen);
                break;
            case "Close":
                DrawClose(context, rect, pen);
                break;
            case "Swap":
                DrawSwap(context, rect, pen);
                break;
            case "Visible":
                DrawVisible(context, rect, pen);
                break;
            case "Hidden":
                DrawHidden(context, rect, pen);
                break;
            case "Play":
                DrawPlay(context, rect, pen);
                break;
            case "Add":
                DrawAdd(context, rect);
                break;
            case "Sound":
                DrawSound(context, rect, pen);
                break;
            case "Open":
                DrawOpen(context, rect);
                break;
            default:
                DrawDiamond(context, rect, pen);
                break;
        }
    }

    private void DrawNamespace(DrawingContext context, Rect r, Pen pen)
    {
        context.DrawRectangle(Fill, pen, r);
        context.DrawLine(pen, P(r, 0.5, 0.1), P(r, 0.5, 0.9));
        context.DrawLine(pen, P(r, 0.1, 0.5), P(r, 0.9, 0.5));
    }

    private void DrawCluster(DrawingContext context, Rect r, Pen pen)
    {
        context.DrawEllipse(Fill, pen, P(r, 0.5, 0.5), r.Width * 0.38, r.Height * 0.38);
        context.DrawLine(pen, P(r, 0.5, 0.12), P(r, 0.5, 0.88));
        context.DrawLine(pen, P(r, 0.12, 0.5), P(r, 0.88, 0.5));
        context.DrawEllipse(Stroke, null, P(r, 0.5, 0.5), r.Width * 0.09, r.Height * 0.09);
    }

    private void DrawNode(DrawingContext context, Rect r, Pen pen)
    {
        context.DrawRectangle(Fill, pen, R(r, 0.12, 0.34, 0.76, 0.44));
        context.DrawRectangle(Stroke, null, R(r, 0.24, 0.48, 0.12, 0.12));
        context.DrawRectangle(Stroke, null, R(r, 0.44, 0.48, 0.12, 0.12));
        context.DrawRectangle(Stroke, null, R(r, 0.64, 0.48, 0.12, 0.12));
        context.DrawLine(pen, P(r, 0.5, 0.34), P(r, 0.5, 0.12));
        context.DrawLine(pen, P(r, 0.38, 0.18), P(r, 0.5, 0.12));
        context.DrawLine(pen, P(r, 0.62, 0.18), P(r, 0.5, 0.12));
    }

    private void DrawPod(DrawingContext context, Rect r, Pen pen)
    {
        context.DrawGeometry(Fill, pen, Polygon(
            P(r, 0.5, 0.06),
            P(r, 0.88, 0.28),
            P(r, 0.88, 0.72),
            P(r, 0.5, 0.94),
            P(r, 0.12, 0.72),
            P(r, 0.12, 0.28)));
        context.DrawEllipse(Stroke, null, P(r, 0.5, 0.5), r.Width * 0.12, r.Height * 0.12);
    }

    private void DrawFactory(DrawingContext context, Rect r, Pen pen)
    {
        context.DrawGeometry(Fill, pen, Polygon(
            P(r, 0.1, 0.82),
            P(r, 0.1, 0.42),
            P(r, 0.32, 0.26),
            P(r, 0.44, 0.42),
            P(r, 0.62, 0.28),
            P(r, 0.78, 0.44),
            P(r, 0.9, 0.44),
            P(r, 0.9, 0.82)));
        context.DrawRectangle(Fill, pen, R(r, 0.68, 0.12, 0.16, 0.28));
        context.DrawRectangle(Stroke, null, R(r, 0.24, 0.58, 0.14, 0.14));
        context.DrawRectangle(Stroke, null, R(r, 0.48, 0.58, 0.14, 0.14));
    }

    private void DrawStack(DrawingContext context, Rect r, Pen pen)
    {
        context.DrawRectangle(Fill, pen, R(r, 0.2, 0.16, 0.6, 0.2));
        context.DrawRectangle(Fill, pen, R(r, 0.16, 0.4, 0.68, 0.2));
        context.DrawRectangle(Fill, pen, R(r, 0.12, 0.64, 0.76, 0.2));
    }

    private void DrawDisks(DrawingContext context, Rect r, Pen pen)
    {
        context.DrawEllipse(Fill, pen, P(r, 0.5, 0.26), r.Width * 0.34, r.Height * 0.14);
        context.DrawRectangle(Fill, null, R(r, 0.16, 0.26, 0.68, 0.44));
        context.DrawLine(pen, P(r, 0.16, 0.26), P(r, 0.16, 0.7));
        context.DrawLine(pen, P(r, 0.84, 0.26), P(r, 0.84, 0.7));
        context.DrawEllipse(Fill, pen, P(r, 0.5, 0.7), r.Width * 0.34, r.Height * 0.14);
        context.DrawLine(pen, P(r, 0.2, 0.48), P(r, 0.8, 0.48));
    }

    private void DrawDaemon(DrawingContext context, Rect r, Pen pen)
    {
        DrawDiamond(context, r, pen);
        context.DrawLine(pen, P(r, 0.5, 0.22), P(r, 0.5, 0.78));
        context.DrawLine(pen, P(r, 0.22, 0.5), P(r, 0.78, 0.5));
    }

    private void DrawJob(DrawingContext context, Rect r, Pen pen)
    {
        DrawDiamond(context, r, pen);
        context.DrawEllipse(Stroke, null, P(r, 0.5, 0.5), r.Width * 0.1, r.Height * 0.1);
    }

    private void DrawClock(DrawingContext context, Rect r, Pen pen)
    {
        context.DrawEllipse(Fill, pen, P(r, 0.5, 0.5), r.Width * 0.38, r.Height * 0.38);
        context.DrawLine(pen, P(r, 0.5, 0.5), P(r, 0.5, 0.24));
        context.DrawLine(pen, P(r, 0.5, 0.5), P(r, 0.7, 0.62));
    }

    private void DrawRelay(DrawingContext context, Rect r, Pen pen)
    {
        context.DrawEllipse(Fill, pen, P(r, 0.5, 0.5), r.Width * 0.18, r.Height * 0.18);
        context.DrawLine(pen, P(r, 0.5, 0.32), P(r, 0.5, 0.08));
        context.DrawLine(pen, P(r, 0.38, 0.62), P(r, 0.16, 0.86));
        context.DrawLine(pen, P(r, 0.62, 0.62), P(r, 0.84, 0.86));
        context.DrawEllipse(Fill, pen, P(r, 0.5, 0.08), r.Width * 0.08, r.Height * 0.08);
        context.DrawEllipse(Fill, pen, P(r, 0.16, 0.86), r.Width * 0.08, r.Height * 0.08);
        context.DrawEllipse(Fill, pen, P(r, 0.84, 0.86), r.Width * 0.08, r.Height * 0.08);
    }

    private void DrawGate(DrawingContext context, Rect r, Pen pen)
    {
        context.DrawRectangle(Fill, pen, R(r, 0.14, 0.42, 0.72, 0.42));
        context.DrawRectangle(Stroke, null, R(r, 0.34, 0.56, 0.32, 0.28));
        context.DrawLine(pen, P(r, 0.2, 0.42), P(r, 0.5, 0.14));
        context.DrawLine(pen, P(r, 0.8, 0.42), P(r, 0.5, 0.14));
    }

    private void DrawEndpointSlice(DrawingContext context, Rect r, Pen pen)
    {
        context.DrawLine(pen, P(r, 0.18, 0.5), P(r, 0.5, 0.22));
        context.DrawLine(pen, P(r, 0.5, 0.22), P(r, 0.82, 0.5));
        context.DrawLine(pen, P(r, 0.5, 0.22), P(r, 0.5, 0.82));
        context.DrawEllipse(Fill, pen, P(r, 0.18, 0.5), r.Width * 0.11, r.Height * 0.11);
        context.DrawEllipse(Fill, pen, P(r, 0.5, 0.22), r.Width * 0.11, r.Height * 0.11);
        context.DrawEllipse(Fill, pen, P(r, 0.82, 0.5), r.Width * 0.11, r.Height * 0.11);
        context.DrawEllipse(Fill, pen, P(r, 0.5, 0.82), r.Width * 0.11, r.Height * 0.11);
    }

    private void DrawShield(DrawingContext context, Rect r, Pen pen)
    {
        context.DrawGeometry(Fill, pen, Polygon(
            P(r, 0.5, 0.08),
            P(r, 0.86, 0.22),
            P(r, 0.78, 0.68),
            P(r, 0.5, 0.92),
            P(r, 0.22, 0.68),
            P(r, 0.14, 0.22)));
        context.DrawLine(pen, P(r, 0.5, 0.18), P(r, 0.5, 0.78));
    }

    private void DrawDocument(DrawingContext context, Rect r, Pen pen)
    {
        context.DrawGeometry(Fill, pen, Polygon(
            P(r, 0.22, 0.1),
            P(r, 0.62, 0.1),
            P(r, 0.82, 0.3),
            P(r, 0.82, 0.9),
            P(r, 0.22, 0.9)));
        context.DrawLine(pen, P(r, 0.62, 0.1), P(r, 0.62, 0.3));
        context.DrawLine(pen, P(r, 0.62, 0.3), P(r, 0.82, 0.3));
        context.DrawLine(pen, P(r, 0.34, 0.48), P(r, 0.7, 0.48));
        context.DrawLine(pen, P(r, 0.34, 0.64), P(r, 0.7, 0.64));
    }

    private void DrawLock(DrawingContext context, Rect r, Pen pen)
    {
        context.DrawRectangle(Fill, pen, R(r, 0.18, 0.46, 0.64, 0.4));
        context.DrawGeometry(null, pen, Polygon(
            P(r, 0.32, 0.46),
            P(r, 0.32, 0.28),
            P(r, 0.5, 0.14),
            P(r, 0.68, 0.28),
            P(r, 0.68, 0.46)));
        context.DrawRectangle(Stroke, null, R(r, 0.46, 0.62, 0.08, 0.12));
    }

    private void DrawUser(DrawingContext context, Rect r, Pen pen)
    {
        context.DrawEllipse(Fill, pen, P(r, 0.5, 0.28), r.Width * 0.17, r.Height * 0.17);
        context.DrawGeometry(Fill, pen, Polygon(
            P(r, 0.18, 0.88),
            P(r, 0.28, 0.56),
            P(r, 0.72, 0.56),
            P(r, 0.82, 0.88)));
    }

    private void DrawBolt(DrawingContext context, Rect r, Pen pen)
    {
        context.DrawGeometry(Fill, pen, Polygon(
            P(r, 0.58, 0.04),
            P(r, 0.24, 0.5),
            P(r, 0.5, 0.5),
            P(r, 0.38, 0.96),
            P(r, 0.78, 0.4),
            P(r, 0.52, 0.4)));
    }

    private void DrawCube(DrawingContext context, Rect r, Pen pen)
    {
        context.DrawGeometry(Fill, pen, Polygon(P(r, 0.5, 0.08), P(r, 0.86, 0.28), P(r, 0.5, 0.48), P(r, 0.14, 0.28)));
        context.DrawGeometry(Fill, pen, Polygon(P(r, 0.14, 0.28), P(r, 0.5, 0.48), P(r, 0.5, 0.9), P(r, 0.14, 0.7)));
        context.DrawGeometry(Fill, pen, Polygon(P(r, 0.86, 0.28), P(r, 0.5, 0.48), P(r, 0.5, 0.9), P(r, 0.86, 0.7)));
    }

    private void DrawDiamond(DrawingContext context, Rect r, Pen pen)
    {
        context.DrawGeometry(Fill, pen, Polygon(P(r, 0.5, 0.06), P(r, 0.94, 0.5), P(r, 0.5, 0.94), P(r, 0.06, 0.5)));
    }

    private void DrawTrash(DrawingContext context, Rect r, Pen pen)
    {
        // Lid and handle.
        context.DrawLine(pen, P(r, 0.16, 0.26), P(r, 0.84, 0.26));
        context.DrawLine(pen, P(r, 0.4, 0.26), P(r, 0.44, 0.14));
        context.DrawLine(pen, P(r, 0.44, 0.14), P(r, 0.56, 0.14));
        context.DrawLine(pen, P(r, 0.56, 0.14), P(r, 0.6, 0.26));
        // Can body as a tapered bin.
        context.DrawGeometry(Fill, pen, Polygon(
            P(r, 0.24, 0.3),
            P(r, 0.76, 0.3),
            P(r, 0.7, 0.9),
            P(r, 0.3, 0.9)));
        // Vertical ribs.
        context.DrawLine(pen, P(r, 0.42, 0.38), P(r, 0.42, 0.82));
        context.DrawLine(pen, P(r, 0.58, 0.38), P(r, 0.58, 0.82));
    }

    private void DrawPencil(DrawingContext context, Rect r, Pen pen)
    {
        // Diagonal pencil body from top-right to bottom-left.
        context.DrawGeometry(Fill, pen, Polygon(
            P(r, 0.66, 0.12),
            P(r, 0.88, 0.34),
            P(r, 0.34, 0.88),
            P(r, 0.12, 0.66)));
        // Tip.
        context.DrawGeometry(Stroke, pen, Polygon(
            P(r, 0.12, 0.66),
            P(r, 0.34, 0.88),
            P(r, 0.12, 0.88)));
    }

    private void DrawClose(DrawingContext context, Rect r, Pen pen)
    {
        // Line-only glyph: stroke with the Fill colour so it stays visible in both light and dark themes
        // (the default Stroke is near-black and disappears on dark surfaces).
        var stroke = FillPen(r);
        context.DrawLine(stroke, P(r, 0.22, 0.22), P(r, 0.78, 0.78));
        context.DrawLine(stroke, P(r, 0.78, 0.22), P(r, 0.22, 0.78));
    }

    private void DrawSwap(DrawingContext context, Rect r, Pen pen)
    {
        var stroke = FillPen(r);
        // Top arrow pointing right.
        context.DrawLine(stroke, P(r, 0.16, 0.34), P(r, 0.84, 0.34));
        context.DrawLine(stroke, P(r, 0.7, 0.2), P(r, 0.84, 0.34));
        context.DrawLine(stroke, P(r, 0.7, 0.48), P(r, 0.84, 0.34));
        // Bottom arrow pointing left.
        context.DrawLine(stroke, P(r, 0.84, 0.66), P(r, 0.16, 0.66));
        context.DrawLine(stroke, P(r, 0.3, 0.52), P(r, 0.16, 0.66));
        context.DrawLine(stroke, P(r, 0.3, 0.8), P(r, 0.16, 0.66));
    }

    private void DrawVisible(DrawingContext context, Rect r, Pen pen)
    {
        var stroke = FillPen(r);
        context.DrawGeometry(null, stroke, Polygon(P(r, 0.08, 0.5), P(r, 0.28, 0.28), P(r, 0.5, 0.2), P(r, 0.72, 0.28), P(r, 0.92, 0.5), P(r, 0.72, 0.72), P(r, 0.5, 0.8), P(r, 0.28, 0.72)));
        context.DrawEllipse(Fill, null, P(r, 0.5, 0.5), r.Width * 0.13, r.Height * 0.13);
    }

    private void DrawHidden(DrawingContext context, Rect r, Pen pen)
    {
        DrawVisible(context, r, pen);
        var stroke = new Pen(Stroke, Math.Max(1.4, Math.Min(r.Width, r.Height) * 0.12), lineCap: PenLineCap.Round);
        context.DrawLine(stroke, P(r, 0.16, 0.84), P(r, 0.84, 0.16));
    }

    private void DrawPlay(DrawingContext context, Rect r, Pen pen)
    {
        context.DrawGeometry(Fill, pen, Polygon(P(r, 0.28, 0.16), P(r, 0.82, 0.5), P(r, 0.28, 0.84)));
    }

    private void DrawAdd(DrawingContext context, Rect r)
    {
        var stroke = FillPen(r);
        context.DrawLine(stroke, P(r, 0.5, 0.18), P(r, 0.5, 0.82));
        context.DrawLine(stroke, P(r, 0.18, 0.5), P(r, 0.82, 0.5));
    }

    private void DrawSound(DrawingContext context, Rect r, Pen pen)
    {
        context.DrawGeometry(Fill, pen, Polygon(P(r, 0.12, 0.42), P(r, 0.32, 0.42), P(r, 0.56, 0.22), P(r, 0.56, 0.78), P(r, 0.32, 0.58), P(r, 0.12, 0.58)));
        var stroke = FillPen(r);
        context.DrawLine(stroke, P(r, 0.66, 0.34), P(r, 0.76, 0.42));
        context.DrawLine(stroke, P(r, 0.76, 0.42), P(r, 0.76, 0.58));
        context.DrawLine(stroke, P(r, 0.76, 0.58), P(r, 0.66, 0.66));
        context.DrawLine(stroke, P(r, 0.82, 0.24), P(r, 0.92, 0.36));
        context.DrawLine(stroke, P(r, 0.92, 0.36), P(r, 0.92, 0.64));
        context.DrawLine(stroke, P(r, 0.92, 0.64), P(r, 0.82, 0.76));
    }

    private void DrawOpen(DrawingContext context, Rect r)
    {
        var stroke = FillPen(r);
        context.DrawRectangle(null, stroke, R(r, 0.16, 0.3, 0.5, 0.54));
        context.DrawLine(stroke, P(r, 0.48, 0.18), P(r, 0.84, 0.18));
        context.DrawLine(stroke, P(r, 0.84, 0.18), P(r, 0.84, 0.54));
        context.DrawLine(stroke, P(r, 0.84, 0.18), P(r, 0.44, 0.58));
    }

    private Pen FillPen(Rect r)
    {
        return new Pen(Fill, Math.Max(1.4, Math.Min(r.Width, r.Height) * 0.1), lineCap: PenLineCap.Round);
    }

    private static Point P(Rect r, double x, double y)
    {
        return new Point(r.X + r.Width * x, r.Y + r.Height * y);
    }

    private static Rect R(Rect r, double x, double y, double width, double height)
    {
        return new Rect(r.X + r.Width * x, r.Y + r.Height * y, r.Width * width, r.Height * height);
    }

    private static StreamGeometry Polygon(params Point[] points)
    {
        var geometry = new StreamGeometry();
        using var context = geometry.Open();
        context.BeginFigure(points[0], true);
        foreach (var point in points.Skip(1))
        {
            context.LineTo(point);
        }

        context.EndFigure(true);
        return geometry;
    }
}
