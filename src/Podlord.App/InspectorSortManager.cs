using Avalonia.Controls;
using Avalonia.Data;
using System.Collections;

namespace Podlord.App;

internal sealed class InspectorSortManager
{
    public enum SortDir { None, Ascending, Descending }

    private readonly Dictionary<DataGrid, (string Column, SortDir Direction)> state = new();

    public bool TryGetSort(DataGrid grid, out (string Column, SortDir Direction) value) =>
        state.TryGetValue(grid, out value);

    public void Reset(DataGrid grid) => state.Remove(grid);

    public bool CycleSort(DataGrid grid, string column)
    {
        var current = state.GetValueOrDefault(grid);
        var next = current.Column == column
            ? Advance(current.Direction)
            : SortDir.Ascending;

        if (next == SortDir.None)
        {
            state.Remove(grid);
            ApplySort(grid, ResolveDefaultSortPath(grid), SortDir.Ascending);
            return true;
        }

        var path = ResolveSortPath(grid, column);
        if (path is null)
        {
            return false;
        }
        state[grid] = (column, next);
        ApplySort(grid, path, next);
        return true;
    }

    public static SortDir Advance(SortDir current) =>
        current switch
        {
            SortDir.Ascending => SortDir.Descending,
            SortDir.Descending => SortDir.None,
            _ => SortDir.Ascending
        };

    public static string GlyphFor(SortDir direction) =>
        direction switch
        {
            SortDir.Ascending => "▲",
            SortDir.Descending => "▼",
            _ => string.Empty
        };

    public static string? ResolveSortPath(DataGrid grid, string column)
    {
        var match = grid.Columns.FirstOrDefault(candidate => MainWindow.HeaderText(candidate.Header).Equals(column, StringComparison.Ordinal));
        if (match is null)
        {
            return null;
        }
        if (!string.IsNullOrWhiteSpace(match.SortMemberPath))
        {
            return match.SortMemberPath;
        }
        if (match is DataGridBoundColumn bound && bound.Binding is Binding binding && !string.IsNullOrWhiteSpace(binding.Path))
        {
            return binding.Path;
        }
        return null;
    }

    public static string? ResolveDefaultSortPath(DataGrid grid)
    {
        foreach (var column in grid.Columns.OrderBy(c => c.DisplayIndex))
        {
            if (!string.IsNullOrWhiteSpace(column.SortMemberPath))
            {
                return column.SortMemberPath;
            }
            if (column is DataGridBoundColumn bound && bound.Binding is Binding binding && !string.IsNullOrWhiteSpace(binding.Path))
            {
                return binding.Path;
            }
        }
        return null;
    }

    public static void ApplySort(DataGrid grid, string? path, SortDir direction)
    {
        if (path is null || grid.ItemsSource is not IList list || list.Count < 2)
        {
            return;
        }
        var items = new List<object?>(list.Count);
        foreach (var item in list)
        {
            items.Add(item);
        }
        var sample = items.FirstOrDefault(entry => entry is not null);
        if (sample is null)
        {
            return;
        }
        var property = sample.GetType().GetProperty(path);
        if (property is null)
        {
            return;
        }
        items.Sort((left, right) =>
        {
            var leftValue = left is null ? null : property.GetValue(left);
            var rightValue = right is null ? null : property.GetValue(right);
            if (leftValue is null && rightValue is null) return 0;
            if (leftValue is null) return -1;
            if (rightValue is null) return 1;
            if (leftValue is IComparable comparable)
            {
                return comparable.CompareTo(rightValue);
            }
            return string.Compare(leftValue.ToString(), rightValue.ToString(), StringComparison.OrdinalIgnoreCase);
        });
        if (direction == SortDir.Descending)
        {
            items.Reverse();
        }
        list.Clear();
        foreach (var item in items)
        {
            list.Add(item);
        }
    }
}
