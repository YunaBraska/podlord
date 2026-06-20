using System.Collections.Specialized;
using System.ComponentModel;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Media;

namespace Podlord.App;

public sealed class RadarIdleLayer : Control
{
    public static readonly StyledProperty<IEnumerable<RadarIdleCellViewModel>?> CellsProperty =
        AvaloniaProperty.Register<RadarIdleLayer, IEnumerable<RadarIdleCellViewModel>?>(nameof(Cells));

    private IEnumerable<RadarIdleCellViewModel>? subscribedCells;
    private INotifyCollectionChanged? subscribedCollection;

    static RadarIdleLayer()
    {
        AffectsRender<RadarIdleLayer>(CellsProperty);
    }

    public RadarIdleLayer()
    {
        IsHitTestVisible = false;
        ClipToBounds = true;
    }

    public IEnumerable<RadarIdleCellViewModel>? Cells
    {
        get => GetValue(CellsProperty);
        set => SetValue(CellsProperty, value);
    }

    protected override void OnPropertyChanged(AvaloniaPropertyChangedEventArgs change)
    {
        base.OnPropertyChanged(change);
        if (change.Property == CellsProperty)
        {
            SubscribeCells(Cells);
            InvalidateVisual();
        }
    }

    protected override void OnAttachedToVisualTree(VisualTreeAttachmentEventArgs e)
    {
        base.OnAttachedToVisualTree(e);
        SubscribeCells(Cells);
    }

    protected override void OnDetachedFromVisualTree(VisualTreeAttachmentEventArgs e)
    {
        SubscribeCells(null);
        base.OnDetachedFromVisualTree(e);
    }

    public override void Render(DrawingContext context)
    {
        base.Render(context);
        if (Cells is null)
        {
            return;
        }

        foreach (var cell in Cells)
        {
            var rect = new Rect(cell.X, cell.Y, cell.Width, cell.Height);
            if (rect.Width > 0 && rect.Height > 0 && Bounds.Intersects(rect))
            {
                context.DrawRectangle(cell.Brush, null, rect);
            }
        }
    }

    private void SubscribeCells(IEnumerable<RadarIdleCellViewModel>? cells)
    {
        if (ReferenceEquals(subscribedCells, cells))
        {
            return;
        }

        UnsubscribeCurrentCells();
        subscribedCells = cells;
        subscribedCollection = cells as INotifyCollectionChanged;
        if (subscribedCollection is not null)
        {
            subscribedCollection.CollectionChanged += CellsCollectionChanged;
        }

        if (cells is null)
        {
            return;
        }

        foreach (var cell in cells)
        {
            cell.PropertyChanged += CellPropertyChanged;
        }
    }

    private void UnsubscribeCurrentCells()
    {
        if (subscribedCollection is not null)
        {
            subscribedCollection.CollectionChanged -= CellsCollectionChanged;
            subscribedCollection = null;
        }

        if (subscribedCells is not null)
        {
            foreach (var cell in subscribedCells)
            {
                cell.PropertyChanged -= CellPropertyChanged;
            }
        }

        subscribedCells = null;
    }

    private void CellsCollectionChanged(object? sender, NotifyCollectionChangedEventArgs e)
    {
        if (e.OldItems is not null)
        {
            foreach (RadarIdleCellViewModel cell in e.OldItems)
            {
                cell.PropertyChanged -= CellPropertyChanged;
            }
        }

        if (e.NewItems is not null)
        {
            foreach (RadarIdleCellViewModel cell in e.NewItems)
            {
                cell.PropertyChanged += CellPropertyChanged;
            }
        }

        if (e.Action == NotifyCollectionChangedAction.Reset)
        {
            SubscribeCells(null);
            SubscribeCells(Cells);
        }

        InvalidateVisual();
    }

    private void CellPropertyChanged(object? sender, PropertyChangedEventArgs e)
    {
        InvalidateVisual();
    }
}
