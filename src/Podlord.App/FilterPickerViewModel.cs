using System.Collections.ObjectModel;
using System.ComponentModel;
using System.Runtime.CompilerServices;
using System.Windows.Input;
using Podlord.Core;

namespace Podlord.App;

public sealed class FilterPickerViewModel : INotifyPropertyChanged
{
    private readonly Action filterChanged;
    private string search = string.Empty;

    public FilterPickerViewModel(string glyphKind, string label, Action filterChanged)
    {
        GlyphKind = glyphKind;
        Label = label;
        this.filterChanged = filterChanged;
        ClearCommand = new RelayCommand(Clear);
        AddSearchCommand = new RelayCommand(AddSearchAsCustom);
        IsKindPicker = string.Equals(label, "Kind", StringComparison.Ordinal);
    }

    public bool IsKindPicker { get; }

    public string GlyphKind { get; }

    public string Label { get; }

    public ObservableCollection<FilterOptionViewModel> Options { get; } = [];

    public ObservableCollection<CustomFilterValueViewModel> CustomValues { get; } = [];

    public ICommand ClearCommand { get; }

    public ICommand AddSearchCommand { get; }

    public event PropertyChangedEventHandler? PropertyChanged;

    public string Search
    {
        get => search;
        set
        {
            if (SetField(ref search, value))
            {
                foreach (var option in Options)
                {
                    option.ApplySearch(search);
                }
            }
        }
    }

    public string Expression
    {
        get
        {
            var selected = Options
                .Where(option => option.IsSelected)
                .Select(option => $"\"{option.Value.Replace("\"", "\\\"", StringComparison.Ordinal)}\"");
            var custom = CustomValues
                .Where(value => value.IsSelected)
                .Select(value => value.Value);
            return string.Join(" ", selected.Concat(custom).Where(value => value.Length > 0));
        }
    }

    public string Summary
    {
        get
        {
            var selected = Options.Where(option => option.IsSelected).Select(option => option.Value).ToList();
            var customCount = CustomValues.Count(value => value.IsSelected);
            return selected.Count switch
            {
                0 when customCount == 0 => Label,
                0 => $"{Label}: {customCount} custom",
                1 when customCount == 0 => $"{Label}: {selected[0]}",
                _ when customCount == 0 => $"{Label}: {selected.Count} selected",
                _ => $"{Label}: {selected.Count} + {customCount}"
            };
        }
    }

    public void AddSearchAsCustom()
    {
        var value = Search.Trim();
        if (value.Length == 0)
        {
            return;
        }

        if (CustomValues.Any(existing => existing.Value.Equals(value, StringComparison.Ordinal)))
        {
            foreach (var existing in CustomValues.Where(existing => existing.Value.Equals(value, StringComparison.Ordinal)))
            {
                existing.IsSelected = true;
            }

            Search = string.Empty;
            return;
        }

        CustomValues.Add(new CustomFilterValueViewModel(value, RemoveCustomValue, NotifyExpressionChanged));
        Search = string.Empty;
        NotifyExpressionChanged();
    }

    public void ReplaceOptions(IEnumerable<string> values)
    {
        var selected = Options
            .Where(option => option.IsSelected)
            .Select(option => option.Value)
            .ToHashSet(StringComparer.Ordinal);
        var normalized = values
            .Where(value => !string.IsNullOrWhiteSpace(value))
            .Select(value => value.Trim())
            .Distinct(StringComparer.Ordinal)
            .Order(StringComparer.Ordinal)
            .ToList();

        if (Options.Select(option => option.Value).SequenceEqual(normalized, StringComparer.Ordinal))
        {
            foreach (var option in Options)
            {
                option.SetSelectedSilently(selected.Contains(option.Value));
                option.ApplySearch(Search);
            }

            OnPropertyChanged(nameof(Summary));
            OnPropertyChanged(nameof(Expression));
            return;
        }

        Options.Clear();
        foreach (var value in normalized)
        {
            var option = new FilterOptionViewModel(value, selected.Contains(value), NotifyExpressionChanged)
            {
                IconKind = IsKindPicker ? value : null
            };
            option.ApplySearch(Search);
            Options.Add(option);
        }

        OnPropertyChanged(nameof(Summary));
        OnPropertyChanged(nameof(Expression));
    }

    public void SetExpression(string expression)
    {
        CustomValues.Clear();
        var exactTerms = ResourceFilterMatcher.ExactTerms(expression).ToHashSet(StringComparer.Ordinal);

        foreach (var option in Options)
        {
            option.SetSelectedSilently(exactTerms.Contains(option.Value));
        }

        var selectedKnownOptions = Options
            .Where(option => option.IsSelected)
            .Select(option => option.Value)
            .ToHashSet(StringComparer.Ordinal);
        if (!string.IsNullOrWhiteSpace(expression) && !ExpressionOnlySelectsKnownOptions(expression, selectedKnownOptions))
        {
            CustomValues.Add(new CustomFilterValueViewModel(expression.Trim(), RemoveCustomValue, NotifyExpressionChanged));
        }

        OnPropertyChanged(nameof(Summary));
        OnPropertyChanged(nameof(Expression));
        filterChanged();
    }

    private void Clear()
    {
        var changed = CustomValues.Count > 0 || Options.Any(option => option.IsSelected);
        CustomValues.Clear();
        foreach (var option in Options)
        {
            option.SetSelectedSilently(false);
        }

        OnPropertyChanged(nameof(Summary));
        OnPropertyChanged(nameof(Expression));
        if (changed)
        {
            filterChanged();
        }
    }

    private void RemoveCustomValue(CustomFilterValueViewModel value)
    {
        if (CustomValues.Remove(value))
        {
            NotifyExpressionChanged();
        }
    }

    private bool ExpressionOnlySelectsKnownOptions(string expression, ISet<string> selected)
    {
        var trimmed = expression.Trim();
        var exactTerms = ResourceFilterMatcher.ExactTerms(trimmed);
        if (exactTerms.Count == 0)
        {
            return false;
        }

        var rebuilt = string.Join(" ", exactTerms.Select(term => $"\"{term.Replace("\"", "\\\"", StringComparison.Ordinal)}\""));
        return rebuilt.Equals(trimmed, StringComparison.Ordinal) && exactTerms.All(selected.Contains);
    }

    private void NotifyExpressionChanged()
    {
        OnPropertyChanged(nameof(Summary));
        OnPropertyChanged(nameof(Expression));
        filterChanged();
    }

    private bool SetField(ref string field, string value, [CallerMemberName] string? propertyName = null)
    {
        if (field == value)
        {
            return false;
        }

        field = value;
        OnPropertyChanged(propertyName);
        return true;
    }

    private void OnPropertyChanged([CallerMemberName] string? propertyName = null)
    {
        PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(propertyName));
    }
}

public sealed class FilterOptionViewModel : INotifyPropertyChanged
{
    private readonly Action changed;
    private bool isSelected;
    private bool isVisible = true;

    public FilterOptionViewModel(string value, bool isSelected, Action changed)
    {
        Value = value;
        this.isSelected = isSelected;
        this.changed = changed;
    }

    public string Value { get; }

    public string? IconKind { get; init; }

    public event PropertyChangedEventHandler? PropertyChanged;

    public bool IsSelected
    {
        get => isSelected;
        set
        {
            if (isSelected == value)
            {
                return;
            }

            isSelected = value;
            OnPropertyChanged();
            changed();
        }
    }

    public bool IsVisible
    {
        get => isVisible;
        private set
        {
            if (isVisible == value)
            {
                return;
            }

            isVisible = value;
            OnPropertyChanged();
        }
    }

    public void ApplySearch(string expression)
    {
        IsVisible = string.IsNullOrWhiteSpace(expression)
                    || Value.Contains(expression.Trim(), StringComparison.OrdinalIgnoreCase);
    }

    public void SetSelectedSilently(bool selected)
    {
        if (isSelected == selected)
        {
            return;
        }

        isSelected = selected;
        OnPropertyChanged(nameof(IsSelected));
    }

    private void OnPropertyChanged([CallerMemberName] string? propertyName = null)
    {
        PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(propertyName));
    }
}

public sealed class CustomFilterValueViewModel : INotifyPropertyChanged
{
    private readonly Action changed;
    private bool isSelected = true;

    public CustomFilterValueViewModel(string value, Action<CustomFilterValueViewModel> remove, Action changed)
    {
        Value = value;
        this.changed = changed;
        RemoveCommand = new RelayCommand(() => remove(this));
    }

    public string Value { get; }

    public ICommand RemoveCommand { get; }

    public event PropertyChangedEventHandler? PropertyChanged;

    public bool IsSelected
    {
        get => isSelected;
        set
        {
            if (isSelected == value)
            {
                return;
            }

            isSelected = value;
            PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(nameof(IsSelected)));
            changed();
        }
    }
}

public sealed class RelayCommand(Action execute) : ICommand
{
    public event EventHandler? CanExecuteChanged;

    public bool CanExecute(object? parameter)
    {
        return true;
    }

    public void Execute(object? parameter)
    {
        execute();
    }

    public void RaiseCanExecuteChanged()
    {
        CanExecuteChanged?.Invoke(this, EventArgs.Empty);
    }
}
