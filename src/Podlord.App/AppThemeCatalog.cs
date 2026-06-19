using Avalonia;
using Avalonia.Media;
using Avalonia.Styling;
using Avalonia.Threading;
using System.Diagnostics.CodeAnalysis;

namespace Podlord.App;

public static class AppThemeCatalog
{
    public const string DefaultTheme = "Sirocco Command";
    public const string DefaultIntensity = "subtle";
    public const string DefaultVariant = "dark";

    public static IReadOnlyList<string> ThemeNames => themeNames.Value;

    public static IReadOnlyList<string> ThemeIntensityNames { get; } = ["subtle", "medium", "arcade"];

    public static IReadOnlyList<string> ThemeVariantNames { get; } = ["dark", "light"];

    private static readonly Lazy<IReadOnlyList<string>> themeNames = new(() => Themes!.Select(theme => theme.Name).ToList());

    public static string Normalize(string? theme)
    {
        return ThemeNames.FirstOrDefault(name => name.Equals(theme, StringComparison.OrdinalIgnoreCase))
               ?? LegacyAliases.FirstOrDefault(pair => pair.Key.Equals(theme, StringComparison.OrdinalIgnoreCase)).Value
               ?? DefaultTheme;
    }

    public static string NormalizeVariant(string? variant)
    {
        return ThemeVariantNames.FirstOrDefault(name => name.Equals(variant, StringComparison.OrdinalIgnoreCase))
               ?? DefaultVariant;
    }

    public static string IntensityName(byte pixelEffectIntensity)
    {
        return pixelEffectIntensity switch
        {
            >= 80 => "arcade",
            >= 45 => "medium",
            _ => DefaultIntensity
        };
    }

    public static byte PixelEffectIntensity(string? intensity)
    {
        return intensity?.Trim().ToLowerInvariant() switch
        {
            "arcade" => 86,
            "medium" => 56,
            _ => 18
        };
    }

    public static void Apply(string? theme)
    {
        Apply(theme, PixelEffectIntensity(DefaultIntensity), DefaultVariant);
    }

    public static void Apply(string? theme, byte pixelEffectIntensity)
    {
        Apply(theme, pixelEffectIntensity, DefaultVariant);
    }

    public static void Apply(string? theme, byte pixelEffectIntensity, string? variant)
    {
        var normalized = Normalize(theme);
        var normalizedVariant = NormalizeVariant(variant);
        var themeEntry = Themes.FirstOrDefault(candidate => candidate.Name.Equals(normalized, StringComparison.Ordinal))
                         ?? Themes[0];
        var palette = normalizedVariant == "light"
            ? themeEntry.Light
            : themeEntry.Dark;
        currentPalette = palette;
        MutateApplication(application =>
            application.RequestedThemeVariant = normalizedVariant == "light"
                ? ThemeVariant.Light
                : ThemeVariant.Dark);

        var noiseAlpha = pixelEffectIntensity switch
        {
            >= 80 => 38,
            >= 45 => 22,
            _ => 10
        };
        var scratchAlpha = pixelEffectIntensity switch
        {
            >= 80 => 24,
            >= 45 => 12,
            _ => 5
        };
        var glowAlpha = pixelEffectIntensity switch
        {
            >= 80 => 184,
            >= 45 => 120,
            _ => 72
        };

        SetBrush("PlBgAppBrush", palette.BgApp);
        SetBrush("PlBgPanelBrush", palette.BgPanel);
        SetBrush("PlBgPanelRaisedBrush", palette.BgRaised);
        SetBrush("PlBgPanelInsetBrush", palette.BgInset);
        SetBrush("PlInspectorBgBrush", palette.InspectorBg);
        SetBrush("PlSidebarBgBrush", palette.SidebarBg);
        SetBrush("PlRadarBgBrush", palette.RadarBg);
        SetBrush("PlTableBgBrush", palette.TableBg);
        SetBrush("PlTableHeaderBgBrush", palette.TableHeaderBg);
        SetBrush("PlRowHoverBrush", palette.RowHover);
        SetBrush("PlBorderSubtleBrush", palette.BorderSubtle);
        SetBrush("PlBorderStrongBrush", palette.BorderStrong);
        SetBrush("PlAccentBrush", palette.Accent);
        SetBrush("PlAccentMutedBrush", palette.AccentMuted);
        SetBrush("PlAccentGlowBrush", WithAlpha(palette.AccentGlow, glowAlpha));
        SetBrush("PlTextBrush", palette.TextMain);
        SetBrush("PlTextMutedBrush", palette.TextMuted);
        SetBrush("PlTextFaintBrush", palette.TextFaint);
        SetBrush("PlStatusSuccessBrush", palette.StatusSuccess);
        SetBrush("PlWarningBrush", palette.StatusWarning);
        SetBrush("PlDangerBrush", palette.StatusDanger);
        SetBrush("PlStatusUnknownBrush", palette.StatusUnknown);
        SetBrush("PlProgressTrackBrush", palette.ProgressTrack);
        SetBrush("PlProgressFillBrush", palette.ProgressFill);
        SetBrush("PlTextureOverlayBrush", WithAlpha(palette.TextureBase, noiseAlpha));
        SetBrush("PlShadowSoftBrush", WithAlpha("#000000", normalizedVariant == "light" ? (byte)42 : (byte)86));
        SetBrush("PlShadowHardBrush", WithAlpha("#000000", normalizedVariant == "light" ? (byte)82 : (byte)138));

        SetBrush("PlBgDeepBrush", palette.BgApp);
        SetBrush("PlBgMetalBrush", palette.BgRaised);
        SetBrush("PlBlueGlowBrush", palette.AccentGlow);
        SetBrush("PlGoldBrightBrush", palette.Accent);
        SetBrush("PlBronzeBrush", palette.BorderSubtle);
        SetBrush("PlGoldBorderBrush", palette.BorderStrong);
        SetBrush("PlTextureGrainBrush", WithAlpha(palette.TextureBase, noiseAlpha));
        SetBrush("PlTextureScratchBrush", WithAlpha("#000000", scratchAlpha));
        SetBrush("PlInsetBrush", palette.BgInset);
        SetBrush("PlInsetAltBrush", palette.Selection);
        SetBrush("PlPlaqueBrush", palette.BgRaised);
        SetBrush("PlPlaqueEdgeBrush", palette.BorderStrong);
        SetBrush("PlRadarShellBrush", palette.RadarShell);
        SetBrush("PlRadarGlassBrush", palette.RadarGlass);
        SetBrush("PlInputBgBrush", palette.BgInset);
        SetBrush("PlGridBgBrush", palette.TableBg);
        SetBrush("PlGridHeaderBrush", palette.TableHeaderBg);
        SetBrush("PlGridLineBrush", palette.BorderSubtle);
        SetBrush("PlSelectedRowBrush", palette.Selection);
        SetBrush("PlTooltipBgBrush", palette.BgPanel);
        SetBrush("PlLogoSurfaceBrush", WithAlpha(palette.TextMain, normalizedVariant == "light" ? (byte)28 : (byte)42));

        SetGradient("PlButtonBrush", [palette.ButtonTop, palette.ButtonBottom]);
        SetGradient("PlButtonHoverBrush", [palette.HoverTop, palette.HoverBottom]);
        SetGradient("PlButtonPressedBrush", [palette.PressedTop, palette.PressedBottom]);
        SetGradient("PlPanelBrush", [palette.BgPanel, palette.BgInset]);
        SetGradient("PlPanelRaisedBrush", [palette.BgRaised, palette.BgPanel]);
        SetGradient("PlPanelInsetBrush", [palette.BgInset, palette.BgApp]);
        SetGradient("PlHeaderPlaqueBrush", [palette.HeaderTop, palette.HeaderBottom]);
        SetGradient("PlDetailBrush", [palette.DetailTop, palette.DetailBottom]);
        SetGradient("PlBackdropBrush", [palette.BgApp, palette.BackdropLow]);
        SetGradient("PlPanelMaterialBrush", [palette.BgPanel, palette.BgInset]);
        SetGradient("PlInsetMaterialBrush", [palette.BgInset, palette.BgApp]);
        SetGradient("PlInspectorMaterialBrush", [palette.InspectorBg, palette.BgInset]);
        SetGradient("PlSidebarMaterialBrush", [palette.SidebarBg, palette.BgPanel]);
        SetGradient("PlRadarMaterialBrush", [palette.RadarShell, palette.RadarBg]);
        SetGradient("PlTableHeaderBrush", [palette.TableHeaderBg, palette.BgPanel]);

        SetBrush("SystemAccentColorDark1Brush", palette.AccentMuted);
        SetBrush("SystemAccentColorLight1Brush", palette.Accent);
    }

    public static IBrush StatusBrush(string state)
    {
        var palette = currentPalette ?? Themes.First(theme => theme.Name == DefaultTheme).Dark;
        return ResourceBrush(StatusBrushKey(state)) ?? SolidColorBrush.Parse(state switch
        {
            "HEALTHY" or "success" => palette.StatusSuccess,
            "WARNING" or "warning" => palette.StatusWarning,
            "CRITICAL" or "danger" => palette.StatusDanger,
            _ => palette.StatusUnknown
        });
    }

    public static IBrush TextBrush()
    {
        var palette = currentPalette ?? Themes.First(theme => theme.Name == DefaultTheme).Dark;
        return ResourceBrush("PlTextBrush") ?? SolidColorBrush.Parse(palette.TextMain);
    }

    public static IBrush IdentityBrush(string? value)
    {
        if (string.IsNullOrWhiteSpace(value) || value == "-")
        {
            return Brushes.Transparent;
        }

        var palette = currentPalette ?? Themes.First(theme => theme.Name == DefaultTheme).Dark;
        var hash = 17;
        foreach (var ch in value)
        {
            hash = unchecked(hash * 31 + ch);
        }

        var index = (hash & 0x7FFFFFFF) % palette.IdentityColors.Count;
        return SolidColorBrush.Parse(palette.IdentityColors[index]);
    }

    [ExcludeFromCodeCoverage(Justification = "Avalonia Application.Current is framework-owned; XAML compilation verifies resource keys, pure palette behavior is tested.")]
    private static IBrush? ResourceBrush(string key)
    {
        return Application.Current?.Resources.TryGetResource(key, null, out var value) == true
               && value is IBrush brush
            ? brush
            : null;
    }

    private static string StatusBrushKey(string state)
    {
        return state switch
        {
            "HEALTHY" or "success" => "PlStatusSuccessBrush",
            "WARNING" or "warning" => "PlWarningBrush",
            "CRITICAL" or "danger" => "PlDangerBrush",
            _ => "PlStatusUnknownBrush"
        };
    }

    private static IReadOnlyDictionary<string, string> LegacyAliases { get; } = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase)
    {
        ["dark-command"] = DefaultTheme,
        ["default"] = DefaultTheme,
        ["graphite"] = "Nocturne Basic",
        ["minimal-dark"] = "Nocturne Basic",
        ["minimal-light"] = "Daylight Basic"
    };

    private static Palette? currentPalette;

    private static IReadOnlyList<ThemeEntry> Themes { get; } =
    [
        Theme(
            "Imperial Ledger",
            "imperial-ledger",
            "IMP",
            BuildPalette("#12100C", "#1B1711", "#241D14", "#090806", "#4A4132", "#A7834C", "#D6B16A", "#7E633D", "#E8C77B", "#EFE2C9", "#AA9D86", "#4FC07E", "#D1A24E", "#D7654C", "#7B7468", "#17150F", "#090D08"),
            BuildPalette("#F4EBD8", "#E6D7B8", "#F0E3C7", "#D9C79D", "#B89A61", "#7C5B2F", "#80551E", "#A77636", "#C38A34", "#332716", "#6D5B40", "#16835A", "#A5681F", "#B94432", "#897A63", "#E1D0AB", "#F7F0DF")),
        Theme(
            "Sirocco Command",
            "sirocco-command",
            "SIR",
            BuildPalette("#08090A", "#101315", "#171A1D", "#050606", "#3A3328", "#8B6B3B", "#C08C42", "#725536", "#E4A74A", "#E3D0AA", "#A18E6E", "#55C57C", "#DDB54C", "#E46548", "#76736A", "#131615", "#060A08"),
            BuildPalette("#F0E6D0", "#DDD0B7", "#E8DAC0", "#CCB895", "#AC8D58", "#76552D", "#845316", "#9F7540", "#BD842A", "#312717", "#69583E", "#1A8B5F", "#A8731E", "#B74A33", "#847763", "#D6C6A7", "#F4EAD6")),
        Theme(
            "Ironwood Warroom",
            "ironwood-warroom",
            "IRN",
            BuildPalette("#0D0D0B", "#171411", "#211A15", "#080706", "#4B3F34", "#806747", "#C99B58", "#735A3D", "#E3BF71", "#E7D8B8", "#A59476", "#65C36C", "#D4A64D", "#D95D4A", "#787166", "#15150F", "#090D07"),
            BuildPalette("#EFE2C9", "#DFCBAA", "#EAD7B5", "#CAB18B", "#A6845D", "#64472D", "#785025", "#956F44", "#B98443", "#302515", "#6A5741", "#2E8555", "#9D6B24", "#A94635", "#837666", "#D5BE9B", "#F3E8D3")),
        Theme(
            "Gunmetal Sector",
            "gunmetal-sector",
            "GUN",
            BuildPalette("#05090D", "#0B1218", "#121C24", "#030608", "#273C4D", "#4D86A2", "#70D4E8", "#477D8E", "#8CE9FF", "#D8F1F5", "#84A8B2", "#58D890", "#D7C74A", "#EF5C50", "#64727A", "#07141A", "#02090D"),
            BuildPalette("#E9F0F3", "#D6E3E8", "#E1EDF0", "#C5D4DA", "#79A1AE", "#34677B", "#146E86", "#3F879A", "#10A8C8", "#1D2B31", "#5C737C", "#228766", "#9A7D1F", "#B43B35", "#727F85", "#C9DADE", "#F2F7F8")),
        Theme(
            "Chitin Brood",
            "chitin-brood",
            "CHI",
            BuildPalette("#09060C", "#130D17", "#1D1321", "#050307", "#3F3044", "#76558A", "#B78DDE", "#705291", "#D5A7FF", "#E3D5EA", "#9A83AA", "#71CF60", "#D0B44A", "#E36061", "#766A78", "#100B15", "#050309"),
            BuildPalette("#EEE5EF", "#DED0E2", "#E9DBEE", "#CBB7D2", "#A481B0", "#654274", "#6F3F8C", "#8C61A2", "#A86CCC", "#281D30", "#675271", "#308943", "#9B7421", "#B34149", "#817284", "#D6C5DC", "#F4ECF5")),
        Theme(
            "Prism Ascendant",
            "prism-ascendant",
            "PRI",
            BuildPalette("#070712", "#101122", "#191B33", "#03040A", "#36395D", "#776EA5", "#D8B95F", "#8D7743", "#F1D77E", "#E4E5F0", "#9697B5", "#5FD0A3", "#D8C05B", "#E56452", "#6D7084", "#0B0E18", "#04050D"),
            BuildPalette("#ECECF6", "#D9DBEE", "#E5E6F5", "#C4C7E2", "#858BB7", "#58528A", "#806127", "#8F7543", "#BB8D25", "#24243B", "#62648A", "#228A6B", "#927A22", "#B84837", "#73768B", "#D1D4EC", "#F5F5FB")),
        Theme(
            "Ion Front",
            "ion-front",
            "ION",
            BuildPalette("#030703", "#071007", "#0E1A0D", "#010301", "#214020", "#4E8A3B", "#9BD653", "#598940", "#C8F36A", "#DCEBC9", "#779A69", "#68D954", "#D8CC4A", "#E65B40", "#596657", "#041004", "#010601"),
            BuildPalette("#EDF7E8", "#D5E6CE", "#E1EFD9", "#BCD1B4", "#79A071", "#447534", "#3A7B1E", "#5E8B45", "#75B22D", "#1D2B19", "#5B7552", "#248932", "#918321", "#B4402C", "#6D7867", "#CCE1C3", "#F4FAF0")),
        Theme(
            "Crimson Bunker",
            "crimson-bunker",
            "CRB",
            BuildPalette("#08090B", "#111316", "#1B1F23", "#050608", "#373B43", "#764C4E", "#D65A5A", "#844D52", "#F47A73", "#DCE1E6", "#8D949B", "#58C88A", "#D7B84C", "#E5554B", "#6D737A", "#121719", "#070A0C"),
            BuildPalette("#ECEFF2", "#DCE2E6", "#E8ECEF", "#C7CED4", "#929AA4", "#8D5559", "#9B2F33", "#9A5B60", "#C83F42", "#24292E", "#666E76", "#21835E", "#9A7921", "#B43631", "#777F86", "#D1D8DD", "#F5F7F8")),
        Theme(
            "Machine Wargrid",
            "machine-wargrid",
            "MWG",
            BuildPalette("#070A0E", "#10161D", "#18222C", "#05080A", "#324353", "#617E96", "#9FC7DD", "#6E8CA1", "#B5E7F6", "#D9E2EA", "#8798A5", "#57C889", "#D2BA57", "#E06150", "#68747E", "#0B1217", "#05090C"),
            BuildPalette("#E9EEF2", "#D5DDE4", "#E1E8EE", "#C3CCD5", "#8495A4", "#577187", "#336981", "#6A8295", "#4A9FBE", "#202930", "#62707A", "#21835E", "#927B27", "#B44335", "#747E86", "#CAD4DC", "#F4F7F9")),
        Theme(
            "Emerald Frontline",
            "emerald-frontline",
            "EMR",
            BuildPalette("#020602", "#071007", "#0E1B0D", "#010301", "#254225", "#5B9144", "#B5E86C", "#638C44", "#D3FF7A", "#B8F68D", "#638D4A", "#63D85B", "#F0C36A", "#E45D42", "#53604D", "#041004", "#010501"),
            BuildPalette("#EDF7E7", "#D4E8CC", "#E0F0D7", "#BBD4B2", "#7BA273", "#4B7D38", "#4F861F", "#688D46", "#78B42C", "#203015", "#60764F", "#248A32", "#9B8326", "#B5412F", "#6B7564", "#CBE4C1", "#F4FAEE")),
        Theme(
            "Steel Lance",
            "steel-lance",
            "STL",
            BuildPalette("#090B08", "#11150F", "#1B2117", "#050604", "#363E2A", "#716934", "#E2843A", "#87633B", "#F0A05F", "#DEE3C5", "#8E9673", "#75D36B", "#D0C14D", "#E16045", "#686E5C", "#0E120B", "#050805"),
            BuildPalette("#ECEFDF", "#DADFCA", "#E5E9D6", "#C5CCB3", "#888E65", "#6D622D", "#9B5822", "#8B6840", "#C16A2A", "#292D1E", "#687056", "#367F38", "#8C7E25", "#AB4230", "#747A68", "#D0D8C0", "#F4F6EA")),
        Theme(
            "Neon Directorate",
            "neon-directorate",
            "NEO",
            BuildPalette("#02080A", "#071216", "#0E1E24", "#010406", "#1E4149", "#377D86", "#EF4E70", "#8E435D", "#FF7C9A", "#CFE6EA", "#6C9399", "#58D0A4", "#D4BF4A", "#EF4E70", "#607174", "#051015", "#020608"),
            BuildPalette("#E7F1F2", "#D0E3E6", "#DBEEF0", "#B8D3D8", "#6EA0A9", "#336F78", "#A72C4B", "#91576B", "#CA466A", "#1E3034", "#58757C", "#1D8E72", "#927C22", "#B52E4C", "#718084", "#C5DCE0", "#F2F8F9")),
        Theme(
            "Xeno Bureau",
            "xeno-bureau",
            "XEN",
            BuildPalette("#080B10", "#111720", "#1B2430", "#05070B", "#313E4D", "#647F98", "#D6B94B", "#8D7B45", "#F0D56A", "#D9E2EA", "#8493A4", "#57C88B", "#D6BA4E", "#E05D51", "#68737D", "#0A1017", "#04080D"),
            BuildPalette("#E9EEF4", "#D5DEE8", "#E1E9F1", "#C2CEDB", "#8798A9", "#5C748D", "#8B741F", "#857144", "#B69423", "#202933", "#617181", "#21855F", "#927A22", "#B44237", "#737E88", "#CAD5DF", "#F4F7FA")),
        Theme(
            "Atomic Terminal",
            "atomic-terminal",
            "ATM",
            BuildPalette("#111008", "#1B1810", "#262116", "#090806", "#554A38", "#9A7E4B", "#D5AC68", "#7D633E", "#E8C883", "#E8DCC2", "#A99C85", "#54C183", "#CE9C4C", "#D65B45", "#767269", "#17170F", "#090C07"),
            BuildPalette("#F3EBD7", "#E4D6BA", "#EFE2C7", "#D2BF9C", "#B69B68", "#7C5E36", "#805520", "#A3763F", "#BF8B35", "#332818", "#6D5E46", "#22825C", "#9A6924", "#B44634", "#837868", "#DCC9A8", "#F7F1E1")),
        Theme(
            "Metrogrid Classic",
            "metrogrid-classic",
            "MET",
            BuildPalette("#090C12", "#121722", "#1D2532", "#05070B", "#344354", "#6B8197", "#D2BC72", "#81724D", "#EBD58B", "#DCE3EA", "#8D9AAA", "#56C990", "#D5B84F", "#DF604E", "#6B737D", "#0B1017", "#04070C"),
            BuildPalette("#E8EEF3", "#D5DDE7", "#E1E8F0", "#C2CDDA", "#8798AA", "#60778E", "#876B2C", "#88714F", "#B7953C", "#212B35", "#627181", "#21865F", "#987B22", "#B44235", "#767F88", "#CAD5DF", "#F3F7FA")),
        Theme(
            "Railnet Ledger",
            "railnet-ledger",
            "RNL",
            BuildPalette("#090D09", "#11170F", "#1B2418", "#050704", "#34462F", "#6D865F", "#CAD16D", "#7F8651", "#E2E987", "#D9E4CC", "#879473", "#5EC77B", "#D0C854", "#DF614D", "#65705E", "#0B120A", "#050805"),
            BuildPalette("#EAF1E4", "#D6E2CE", "#E2ECD9", "#C1D0B5", "#829B70", "#607C52", "#7C8427", "#718255", "#A2AA38", "#23301D", "#627356", "#27804A", "#918827", "#B44336", "#727B68", "#CEDCC3", "#F5F8F0")),
        Theme(
            "Stellar Senate",
            "stellar-senate",
            "STR",
            BuildPalette("#050616", "#0C0E25", "#141837", "#02030A", "#303664", "#5E66B0", "#A99DFF", "#7470B8", "#C7BCFF", "#DAD8F2", "#8A88B4", "#5ECBA2", "#D2BE58", "#E06058", "#6B6B83", "#080B1D", "#03040C"),
            BuildPalette("#ECECFA", "#D8DAF0", "#E5E6F7", "#C2C5E5", "#8287BD", "#555CA0", "#665DAD", "#7470B0", "#8C82CF", "#22233F", "#62638A", "#208869", "#907B26", "#B4423C", "#73748D", "#D0D2EC", "#F5F5FC")),
        Theme(
            "Nocturne Basic",
            "nocturne-basic",
            "NOC",
            BuildPalette("#06080B", "#0D1116", "#151B22", "#030507", "#243244", "#4C7193", "#65C6EA", "#45758E", "#80DFFF", "#DAE7EF", "#8496A4", "#55C989", "#D2B653", "#E05C50", "#68727C", "#071018", "#02070B"),
            BuildPalette("#EAF0F4", "#D8E1E8", "#E5ECF2", "#C8D3DC", "#8498AA", "#52718C", "#1F7393", "#4A7D94", "#219BC2", "#21303A", "#607080", "#20845E", "#947B25", "#B44337", "#737E88", "#D0DCE4", "#F6F8FA")),
        Theme(
            "Daylight Basic",
            "daylight-basic",
            "DAY",
            BuildPalette("#0B0C0E", "#13161A", "#1C2228", "#060708", "#303C48", "#65819A", "#A9CBE5", "#6F8EA4", "#BDE6FF", "#E3E7EC", "#8996A2", "#55C989", "#D1B650", "#E05C50", "#69737B", "#0D131A", "#05080C"),
            BuildPalette("#F5F7F8", "#E8EDF1", "#F0F3F6", "#D7DFE5", "#9EADB9", "#6D8394", "#2E6F94", "#66869B", "#3C9CC7", "#24313A", "#687783", "#20855F", "#967C24", "#B44337", "#76818A", "#DCE5EB", "#FFFFFF"))
    ];

    private static ThemeEntry Theme(string name, string id, string shortCode, Palette dark, Palette light)
    {
        return new ThemeEntry(name, id, shortCode, dark, light);
    }

    private static Palette BuildPalette(
        string bgApp,
        string bgPanel,
        string bgRaised,
        string bgInset,
        string borderSubtle,
        string borderStrong,
        string accent,
        string accentMuted,
        string accentGlow,
        string textMain,
        string textMuted,
        string success,
        string warning,
        string danger,
        string unknown,
        string radarShell,
        string radarGlass)
    {
        return new Palette(
            BgApp: bgApp,
            BackdropLow: bgInset,
            BgPanel: bgPanel,
            BgRaised: bgRaised,
            BgInset: bgInset,
            InspectorBg: bgPanel,
            SidebarBg: bgPanel,
            RadarBg: radarGlass,
            TableBg: bgInset,
            TableHeaderBg: bgRaised,
            BorderSubtle: borderSubtle,
            BorderStrong: borderStrong,
            Accent: accent,
            AccentMuted: accentMuted,
            AccentGlow: accentGlow,
            TextMain: textMain,
            TextMuted: textMuted,
            TextFaint: WithAlpha(textMuted, 176),
            StatusSuccess: success,
            StatusWarning: warning,
            StatusDanger: danger,
            StatusUnknown: unknown,
            RadarShell: radarShell,
            RadarGlass: radarGlass,
            ProgressTrack: WithAlpha(borderSubtle, 82),
            ProgressFill: accent,
            RowHover: WithAlpha(accentMuted, 42),
            Selection: WithAlpha(accent, 38),
            TextureBase: textMain,
            IdentityColors: [accentGlow, accent, success, textMuted, borderStrong, accentMuted],
            ButtonTop: bgRaised,
            ButtonBottom: bgPanel,
            HoverTop: WithAlpha(accentMuted, 128),
            HoverBottom: bgRaised,
            PressedTop: bgInset,
            PressedBottom: bgPanel,
            HeaderTop: bgRaised,
            HeaderBottom: bgPanel,
            DetailTop: bgPanel,
            DetailBottom: bgInset);
    }

    [ExcludeFromCodeCoverage(Justification = "Avalonia Application.Current is framework-owned; XAML compilation verifies resource keys, pure palette behavior is tested.")]
    private static void SetBrush(string key, string color)
    {
        MutateApplication(application =>
        {
            if (application.Resources.TryGetResource(key, null, out var value)
                && value is SolidColorBrush brush)
            {
                brush.Color = Color.Parse(color);
            }
        });
    }

    [ExcludeFromCodeCoverage(Justification = "Avalonia Application.Current is framework-owned; XAML compilation verifies resource keys, pure palette behavior is tested.")]
    private static void SetGradient(string key, IReadOnlyList<string> colors)
    {
        MutateApplication(application =>
        {
            if (!application.Resources.TryGetResource(key, null, out var value)
                || value is not LinearGradientBrush brush)
            {
                return;
            }

            brush.GradientStops.Clear();
            if (colors.Count == 0)
            {
                return;
            }

            if (colors.Count == 1)
            {
                brush.GradientStops.Add(new GradientStop(Color.Parse(colors[0]), 0));
                return;
            }

            for (var index = 0; index < colors.Count; index++)
            {
                brush.GradientStops.Add(new GradientStop(
                    Color.Parse(colors[index]),
                    index / (double)(colors.Count - 1)));
            }
        });
    }

    private static void MutateApplication(Action<Application> mutation)
    {
        var application = Application.Current;
        if (application is null)
        {
            return;
        }

        if (Dispatcher.UIThread.CheckAccess())
        {
            mutation(application);
            return;
        }

        Dispatcher.UIThread.Post(() =>
        {
            if (Application.Current is { } current)
            {
                mutation(current);
            }
        });
    }

    private static string WithAlpha(string color, int alpha)
    {
        var parsed = Color.Parse(color);
        var normalized = Math.Clamp(alpha, 0, 255);
        return $"#{normalized:X2}{parsed.R:X2}{parsed.G:X2}{parsed.B:X2}";
    }

    private sealed record ThemeEntry(string Name, string Id, string ShortCode, Palette Dark, Palette Light);

    private sealed record Palette(
        string BgApp,
        string BackdropLow,
        string BgPanel,
        string BgRaised,
        string BgInset,
        string InspectorBg,
        string SidebarBg,
        string RadarBg,
        string TableBg,
        string TableHeaderBg,
        string BorderSubtle,
        string BorderStrong,
        string Accent,
        string AccentMuted,
        string AccentGlow,
        string TextMain,
        string TextMuted,
        string TextFaint,
        string StatusSuccess,
        string StatusWarning,
        string StatusDanger,
        string StatusUnknown,
        string RadarShell,
        string RadarGlass,
        string ProgressTrack,
        string ProgressFill,
        string RowHover,
        string Selection,
        string TextureBase,
        IReadOnlyList<string> IdentityColors,
        string ButtonTop,
        string ButtonBottom,
        string HoverTop,
        string HoverBottom,
        string PressedTop,
        string PressedBottom,
        string HeaderTop,
        string HeaderBottom,
        string DetailTop,
        string DetailBottom);
}
