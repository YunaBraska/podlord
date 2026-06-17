using System.Globalization;
using Avalonia.Data.Converters;
using Avalonia.Media;
using Podlord.Core;

namespace Podlord.App;

public sealed class StatusBrushConverter : IValueConverter
{
    public object Convert(object? value, Type targetType, object? parameter, CultureInfo culture)
    {
        var status = value?.ToString() ?? string.Empty;
        return status switch
        {
            "Available" or "Complete" or "Ready" or "Running" or "Succeeded" or "Observed" => AppThemeCatalog.StatusBrush("HEALTHY"),
            "Pending" or "Terminating" or "Suspended" or "Warning" => AppThemeCatalog.StatusBrush("WARNING"),
            "CrashLoopBackOff" or "CreateContainerConfigError" or "CreateContainerError" or "ErrImagePull" or "Error" or "Failed" or "ImagePullBackOff" or "NotReady" or "OOMKilled" or "Unavailable" => AppThemeCatalog.StatusBrush("CRITICAL"),
            _ => AppThemeCatalog.StatusBrush("UNKNOWN")
        };
    }

    public object ConvertBack(object? value, Type targetType, object? parameter, CultureInfo culture)
    {
        throw new NotSupportedException("Status brush conversion is one-way.");
    }
}

public sealed class ProblemBrushConverter : IValueConverter
{
    public object Convert(object? value, Type targetType, object? parameter, CultureInfo culture)
    {
        if (value is not FlatResourceRow row)
        {
            return AppThemeCatalog.StatusBrush("HEALTHY");
        }

        var problem = ResourceFilterMatcher.ProblemReason(row);
        if (problem.Length == 0)
        {
            return AppThemeCatalog.StatusBrush("HEALTHY");
        }

        return IsCritical(row, problem)
            ? AppThemeCatalog.StatusBrush("CRITICAL")
            : AppThemeCatalog.StatusBrush("WARNING");
    }

    public object ConvertBack(object? value, Type targetType, object? parameter, CultureInfo culture)
    {
        throw new NotSupportedException("Problem brush conversion is one-way.");
    }

    private static bool IsCritical(FlatResourceRow row, string problem)
    {
        return row.Status is "CrashLoopBackOff" or "CreateContainerConfigError" or "CreateContainerError" or "ErrImagePull" or "Error" or "Failed" or "ImagePullBackOff" or "NotReady" or "OOMKilled" or "Unavailable"
               || problem.Contains("Crash", StringComparison.OrdinalIgnoreCase)
               || problem.Contains("Error", StringComparison.OrdinalIgnoreCase)
               || problem.Contains("Failed", StringComparison.OrdinalIgnoreCase)
               || problem.Contains("Unavailable", StringComparison.OrdinalIgnoreCase);
    }
}

public sealed class RestartBrushConverter : IValueConverter
{
    public object Convert(object? value, Type targetType, object? parameter, CultureInfo culture)
    {
        var restarts = value switch
        {
            int number => number,
            string text when int.TryParse(text, NumberStyles.Integer, CultureInfo.InvariantCulture, out var parsed) => parsed,
            _ => 0
        };

        return restarts > ResourceFilterMatcher.DefaultRestartOutlierThreshold
            ? AppThemeCatalog.StatusBrush("WARNING")
            : AppThemeCatalog.TextBrush();
    }

    public object ConvertBack(object? value, Type targetType, object? parameter, CultureInfo culture)
    {
        throw new NotSupportedException("Restart brush conversion is one-way.");
    }
}

public sealed class MetricHealthBrushConverter : IValueConverter
{
    public object Convert(object? value, Type targetType, object? parameter, CultureInfo culture)
    {
        var percent = value switch
        {
            double number => number,
            float number => number,
            int number => number,
            string text when double.TryParse(text.TrimEnd('%'), NumberStyles.Float, CultureInfo.InvariantCulture, out var parsed) => parsed,
            _ => 0d
        };

        return percent switch
        {
            >= 90 => AppThemeCatalog.StatusBrush("CRITICAL"),
            >= 70 => AppThemeCatalog.StatusBrush("WARNING"),
            _ => AppThemeCatalog.StatusBrush("HEALTHY")
        };
    }

    public object ConvertBack(object? value, Type targetType, object? parameter, CultureInfo culture)
    {
        throw new NotSupportedException("Metric health brush conversion is one-way.");
    }
}

public sealed class ProblemReasonConverter : IValueConverter
{
    public object Convert(object? value, Type targetType, object? parameter, CultureInfo culture)
    {
        return value is FlatResourceRow row ? ResourceFilterMatcher.ProblemReason(row) : string.Empty;
    }

    public object ConvertBack(object? value, Type targetType, object? parameter, CultureInfo culture)
    {
        throw new NotSupportedException("Problem reason conversion is one-way.");
    }
}

public sealed class DeterministicBrushConverter : IValueConverter
{
    public object Convert(object? value, Type targetType, object? parameter, CultureInfo culture)
    {
        var text = value?.ToString();
        if (string.IsNullOrWhiteSpace(text) || text == "-")
        {
            return Brushes.Transparent;
        }

        return BrushFrom(text);
    }

    public object ConvertBack(object? value, Type targetType, object? parameter, CultureInfo culture)
    {
        throw new NotSupportedException("Deterministic brush conversion is one-way.");
    }

    internal static IBrush BrushFrom(string value)
    {
        return AppThemeCatalog.IdentityBrush(value);
    }
}

public sealed class HasValueConverter : IValueConverter
{
    public object Convert(object? value, Type targetType, object? parameter, CultureInfo culture)
    {
        return value is string text && !string.IsNullOrWhiteSpace(text);
    }

    public object ConvertBack(object? value, Type targetType, object? parameter, CultureInfo culture)
    {
        throw new NotSupportedException("HasValueConverter is one-way.");
    }
}

public sealed class NodeReferenceConverter : IValueConverter
{
    public object Convert(object? value, Type targetType, object? parameter, CultureInfo culture)
    {
        if (value is string text && !string.IsNullOrWhiteSpace(text) && text != "-")
        {
            return $"Node/{text}";
        }
        return string.Empty;
    }

    public object ConvertBack(object? value, Type targetType, object? parameter, CultureInfo culture)
    {
        throw new NotSupportedException("NodeReferenceConverter is one-way.");
    }
}

public sealed class RadarBrushConverter : IValueConverter
{
    public object Convert(object? value, Type targetType, object? parameter, CultureInfo culture)
    {
        if (value is not FlatResourceRow row)
        {
            return Brushes.Transparent;
        }

        if (!string.IsNullOrWhiteSpace(ResourceFilterMatcher.ProblemReason(row)))
        {
            return row.Status is "CrashLoopBackOff" or "CreateContainerConfigError" or "CreateContainerError" or "ErrImagePull" or "Error" or "Failed" or "ImagePullBackOff" or "NotReady" or "OOMKilled" or "Unavailable"
                ? AppThemeCatalog.StatusBrush("CRITICAL")
                : AppThemeCatalog.StatusBrush("WARNING");
        }

        return DeterministicBrushConverter.BrushFrom($"{row.Kind}:{row.Namespace ?? "cluster"}");
    }

    public object ConvertBack(object? value, Type targetType, object? parameter, CultureInfo culture)
    {
        throw new NotSupportedException("Radar brush conversion is one-way.");
    }
}

public sealed class PortForwardBadgeConverter : IMultiValueConverter
{
    public object Convert(IList<object?> values, Type targetType, object? parameter, CultureInfo culture)
    {
        var row = values.OfType<FlatResourceRow>().FirstOrDefault();
        var forwards = values.OfType<IEnumerable<PortForwardTaskViewModel>>().FirstOrDefault();
        if (row is null || forwards is null)
        {
            return string.Empty;
        }

        var task = forwards.FirstOrDefault(candidate =>
            candidate.Kind.Equals(row.Kind, StringComparison.Ordinal)
            && candidate.Name.Equals(row.Name, StringComparison.Ordinal)
            && candidate.Namespace.Equals(row.Namespace ?? string.Empty, StringComparison.Ordinal)
            && candidate.IsRunning);
        return task is null ? string.Empty : task.LocalPort.ToString(CultureInfo.InvariantCulture);
    }
}

public sealed class PortForwardEligibilityConverter : IValueConverter
{
    public object Convert(object? value, Type targetType, object? parameter, CultureInfo culture)
    {
        if (value is not FlatResourceRow row || string.IsNullOrWhiteSpace(row.Namespace))
        {
            return false;
        }

        return row.Kind switch
        {
            "Pod" => row.Status.Equals("Running", StringComparison.OrdinalIgnoreCase),
            "Service" => true,
            _ => false
        };
    }

    public object ConvertBack(object? value, Type targetType, object? parameter, CultureInfo culture)
    {
        throw new NotSupportedException("Port forward eligibility conversion is one-way.");
    }
}
