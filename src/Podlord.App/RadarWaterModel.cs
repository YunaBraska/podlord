using Avalonia;

namespace Podlord.App;

internal static class RadarWaterModel
{
    public static IReadOnlyList<RadarWaterTile> BuildTiles(
        double width,
        double height,
        double panX,
        double panY,
        double zoomValue,
        int activityRate,
        int speedPercentValue,
        int phaseValue)
    {
        if (width <= 0 || height <= 0)
        {
            return [];
        }

        const double cell = 18;
        var zoom = Math.Clamp(zoomValue, 0.2, 6);
        var rate = Math.Clamp(activityRate, 0, 240);
        var speedPercent = Math.Clamp(speedPercentValue, 0, 100);
        if (speedPercent <= 0)
        {
            return [];
        }

        var speed = speedPercent / 100d;
        var activity = rate / 240d;
        var drift = phaseValue * (0.08 + speed * 0.38) * (0.65 + activity * 0.8);
        var offsetX = PositiveMod(panX * zoom * 0.65 + drift, cell);
        var offsetY = PositiveMod(panY * zoom * 0.35 + drift * 0.45, cell);
        var columns = (int)Math.Ceiling(width / cell) + 2;
        var rows = (int)Math.Ceiling(height / cell) + 2;
        var tiles = new List<RadarWaterTile>(rows * columns / 8);

        for (var row = 0; row < rows; row++)
        {
            var y = row * cell + offsetY - cell;
            for (var column = 0; column < columns; column++)
            {
                var x = column * cell + offsetX - cell;
                var worldColumn = (int)Math.Floor(((x - width / 2) / zoom - panX) / cell);
                var worldRow = (int)Math.Floor(((y - height / 2) / zoom - panY) / cell);
                var value = StableWaterValue(worldColumn, worldRow, phaseValue);
                if (value is not (0 or 1 or 2))
                {
                    continue;
                }

                var inset = value == 2 ? 6.1 : 5.2;
                tiles.Add(new RadarWaterTile(
                    new Rect(x + inset, y + inset, Math.Max(1, cell - inset * 2), Math.Max(1, cell - inset * 2)),
                    value));
            }
        }

        return tiles;
    }

    public static TimeSpan WaterIntervalFor(int activityRate, int speedPercent)
    {
        if (speedPercent <= 0)
        {
            return TimeSpan.FromMilliseconds(3_000);
        }

        var speed = Math.Clamp(speedPercent, 0, 100) / 100d;
        var activity = Math.Clamp(activityRate, 0, 240) / 240d;
        var milliseconds = 2_800 - speed * 1_450 - activity * 520;
        return TimeSpan.FromMilliseconds(Math.Clamp(milliseconds, 850, 2_800));
    }

    public static int StableWaterValue(int column, int row, int phase)
    {
        var drift = (column * 17 + row * 29 + phase * 3) & 63;
        var ripple = (column + phase + (row % 5) * 2) % 17 == 0 ? 2 : 9;
        return drift switch
        {
            0 => 0,
            11 or 37 => 1,
            _ => ripple
        };
    }

    public static double PositiveMod(double value, double modulo)
    {
        var result = value % modulo;
        return result < 0 ? result + modulo : result;
    }
}

internal readonly record struct RadarWaterTile(Rect Bounds, int Kind);
