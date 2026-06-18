using Avalonia;
using Avalonia.Controls;
using Avalonia.Layout;
using Avalonia.Media;

namespace Podlord.App;

/// <summary>
/// Button preset for sortable DataGrid column headers.
/// Auto-applies the <c>columnPlaque</c> style class and composes the
/// repeated KindGlyph + Label layout. Pure data properties keep XAML
/// call sites to a single line.
/// </summary>
public sealed class ColumnPlaqueHeader : Button
{
    public static readonly StyledProperty<string?> KindProperty =
        AvaloniaProperty.Register<ColumnPlaqueHeader, string?>(nameof(Kind));

    public static readonly StyledProperty<IBrush?> GlyphFillProperty =
        AvaloniaProperty.Register<ColumnPlaqueHeader, IBrush?>(nameof(GlyphFill));

    public static readonly StyledProperty<string?> LabelProperty =
        AvaloniaProperty.Register<ColumnPlaqueHeader, string?>(nameof(Label));

    public static readonly StyledProperty<double> GlyphSizeProperty =
        AvaloniaProperty.Register<ColumnPlaqueHeader, double>(nameof(GlyphSize), 18d);

    private readonly KindGlyph glyph;
    private readonly TextBlock labelText;

    public ColumnPlaqueHeader()
    {
        Classes.Add("columnPlaque");
        glyph = new KindGlyph
        {
            Width = GlyphSize,
            Height = GlyphSize,
            IsVisible = false
        };
        labelText = new TextBlock();
        var stack = new StackPanel { Orientation = Orientation.Horizontal, Spacing = 5 };
        stack.Children.Add(glyph);
        stack.Children.Add(labelText);
        Content = stack;
    }

    public string? Kind
    {
        get => GetValue(KindProperty);
        set => SetValue(KindProperty, value);
    }

    public IBrush? GlyphFill
    {
        get => GetValue(GlyphFillProperty);
        set => SetValue(GlyphFillProperty, value);
    }

    public string? Label
    {
        get => GetValue(LabelProperty);
        set => SetValue(LabelProperty, value);
    }

    public double GlyphSize
    {
        get => GetValue(GlyphSizeProperty);
        set => SetValue(GlyphSizeProperty, value);
    }

    protected override void OnPropertyChanged(AvaloniaPropertyChangedEventArgs change)
    {
        base.OnPropertyChanged(change);
        if (change.Property == KindProperty || change.Property == GlyphFillProperty)
        {
            ApplyGlyph();
        }
        else if (change.Property == LabelProperty)
        {
            labelText.Text = Label ?? string.Empty;
        }
        else if (change.Property == GlyphSizeProperty)
        {
            glyph.Width = GlyphSize;
            glyph.Height = GlyphSize;
        }
    }

    private void ApplyGlyph()
    {
        var hasKind = !string.IsNullOrEmpty(Kind);
        glyph.IsVisible = hasKind;
        if (!hasKind)
        {
            return;
        }
        glyph.Kind = Kind!;
        if (GlyphFill is not null)
        {
            glyph.Fill = GlyphFill;
        }
    }
}
