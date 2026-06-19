namespace Podlord.App;

public sealed record AlertSoundDefinition(
    string Id,
    string Name,
    string Purpose,
    string Author,
    string License,
    string SourceUrl,
    string Asset,
    bool IsMusic = false)
{
    public string Attribution => Author;

    public string Label => $"{Name} ({License})";

    public bool Matches(string search)
    {
        if (string.IsNullOrWhiteSpace(search))
        {
            return true;
        }

        return new[] { Id, Name, Purpose, Author, License, SourceUrl, Asset, IsMusic ? "music" : "sound" }
            .Any(value => value.Contains(search.Trim(), StringComparison.OrdinalIgnoreCase));
    }
}

public static class AlertSoundCatalog
{
    private const string KenneyAuthor = "Kenney";
    private const string KenneyLicense = "CC0-1.0";
    private const string KenneyUiAudioUrl = "https://kenney.nl/assets/ui-audio";
    private const string KenneyInterfaceUrl = "https://kenney.nl/assets/interface-sounds";
    private const string KenneySciFiUrl = "https://kenney.nl/assets/sci-fi-sounds";
    private const string KenneyMusicJinglesUrl = "https://kenney.nl/assets/music-jingles";
    private const string KenneyDigitalAudioUrl = "https://kenney.nl/assets/digital-audio";
    private const string KenneyImpactSoundsUrl = "https://kenney.nl/assets/impact-sounds";
    private const string KenneyRpgAudioUrl = "https://kenney.nl/assets/rpg-audio";

    private static readonly string[] KenneyInterfaceFiles =
    [
        "back_001.ogg",
        "back_002.ogg",
        "back_003.ogg",
        "back_004.ogg",
        "bong_001.ogg",
        "click_001.ogg",
        "click_002.ogg",
        "click_003.ogg",
        "click_004.ogg",
        "click_005.ogg",
        "close_001.ogg",
        "close_002.ogg",
        "close_003.ogg",
        "close_004.ogg",
        "confirmation_001.ogg",
        "confirmation_002.ogg",
        "confirmation_003.ogg",
        "confirmation_004.ogg",
        "drop_001.ogg",
        "drop_002.ogg",
        "drop_003.ogg",
        "drop_004.ogg",
        "error_001.ogg",
        "error_002.ogg",
        "error_003.ogg",
        "error_004.ogg",
        "error_005.ogg",
        "error_006.ogg",
        "error_007.ogg",
        "error_008.ogg",
        "glass_001.ogg",
        "glass_002.ogg",
        "glass_003.ogg",
        "glass_004.ogg",
        "glass_005.ogg",
        "glass_006.ogg",
        "glitch_001.ogg",
        "glitch_002.ogg",
        "glitch_003.ogg",
        "glitch_004.ogg",
        "maximize_001.ogg",
        "maximize_002.ogg",
        "maximize_003.ogg",
        "maximize_004.ogg",
        "maximize_005.ogg",
        "maximize_006.ogg",
        "maximize_007.ogg",
        "maximize_008.ogg",
        "maximize_009.ogg",
        "minimize_001.ogg",
        "minimize_002.ogg",
        "minimize_003.ogg",
        "minimize_004.ogg",
        "minimize_005.ogg",
        "minimize_006.ogg",
        "minimize_007.ogg",
        "minimize_008.ogg",
        "minimize_009.ogg",
        "open_001.ogg",
        "open_002.ogg",
        "open_003.ogg",
        "open_004.ogg",
        "pluck_001.ogg",
        "pluck_002.ogg",
        "question_001.ogg",
        "question_002.ogg",
        "question_003.ogg",
        "question_004.ogg",
        "scratch_001.ogg",
        "scratch_002.ogg",
        "scratch_003.ogg",
        "scratch_004.ogg",
        "scratch_005.ogg",
        "scroll_001.ogg",
        "scroll_002.ogg",
        "scroll_003.ogg",
        "scroll_004.ogg",
        "scroll_005.ogg",
        "select_001.ogg",
        "select_002.ogg",
        "select_003.ogg",
        "select_004.ogg",
        "select_005.ogg",
        "select_006.ogg",
        "select_007.ogg",
        "select_008.ogg",
        "switch_001.ogg",
        "switch_002.ogg",
        "switch_003.ogg",
        "switch_004.ogg",
        "switch_005.ogg",
        "switch_006.ogg",
        "switch_007.ogg",
        "tick_001.ogg",
        "tick_002.ogg",
        "tick_004.ogg",
        "toggle_001.ogg",
        "toggle_002.ogg",
        "toggle_003.ogg",
        "toggle_004.ogg"
    ];

    public static IReadOnlyList<AlertSoundDefinition> BuiltIn { get; } = CuratedSounds()
        .Concat(KenneyInterfaceSounds())
        .ToList();

    private static IReadOnlyList<AlertSoundDefinition> CuratedSounds() =>
    [
        new(
            "none",
            "No sound",
            "Silent alert action.",
            "Podlord project",
            KenneyLicense,
            "https://github.com/YunaBraska/podlord",
            "none"),
        new(
            "panel-segment-load",
            "Panel segment load",
            "Short low-power tick for health bar and panel segment changes.",
            KenneyAuthor,
            KenneyLicense,
            KenneyUiAudioUrl,
            "Assets/Audio/ui/panel-segment-load.ogg"),
        new(
            "radar-activated",
            "Radar activated",
            "Scanner wake-up sound when the radar leaves idle screensaver mode.",
            KenneyAuthor,
            KenneyLicense,
            KenneyInterfaceUrl,
            "Assets/Audio/events/radar-activated.ogg"),
        new(
            "activity-tick",
            "Activity tick",
            "Small non-alarming activity cue for recent changes.",
            KenneyAuthor,
            KenneyLicense,
            KenneyInterfaceUrl,
            "Assets/Audio/events/activity-tick.ogg"),
        new(
            "warning-ping",
            "Warning ping",
            "Amber tactical warning cue.",
            KenneyAuthor,
            KenneyLicense,
            KenneyInterfaceUrl,
            "Assets/Audio/alerts/warning-ping.ogg"),
        new(
            "electro-warning",
            "Electro warning",
            "Bright electronic warning chirp.",
            KenneyAuthor,
            KenneyLicense,
            KenneyDigitalAudioUrl,
            "Assets/Audio/alerts/electro-warning.ogg"),
        new(
            "bell-alert",
            "Bell alert",
            "Metallic command bell for notable changes.",
            KenneyAuthor,
            KenneyLicense,
            KenneyImpactSoundsUrl,
            "Assets/Audio/alerts/bell-alert.ogg"),
        new(
            "metal-impact",
            "Metal impact",
            "Industrial metal impact for heavy alerts.",
            KenneyAuthor,
            KenneyLicense,
            KenneyImpactSoundsUrl,
            "Assets/Audio/alerts/metal-impact.ogg"),
        new(
            "critical-klaxon",
            "Critical klaxon",
            "Short red alert cue for critical matches.",
            KenneyAuthor,
            KenneyLicense,
            KenneySciFiUrl,
            "Assets/Audio/alerts/critical-klaxon.ogg"),
        new(
            "power-up",
            "Power up",
            "Positive tactical activation sound.",
            KenneyAuthor,
            KenneyLicense,
            KenneyDigitalAudioUrl,
            "Assets/Audio/events/power-up.ogg"),
        new(
            "power-down",
            "Power down",
            "Low descending tactical cue.",
            KenneyAuthor,
            KenneyLicense,
            KenneyDigitalAudioUrl,
            "Assets/Audio/events/power-down.ogg"),
        new(
            "three-tone",
            "Three tone",
            "Neutral three-tone system notification.",
            KenneyAuthor,
            KenneyLicense,
            KenneyDigitalAudioUrl,
            "Assets/Audio/events/three-tone.ogg"),
        new(
            "metal-click",
            "Metal click",
            "Tiny mechanical UI click.",
            KenneyAuthor,
            KenneyLicense,
            KenneyRpgAudioUrl,
            "Assets/Audio/ui/metal-click.ogg"),
        new(
            "book-open",
            "Book open",
            "Soft fantasy war-room page cue.",
            KenneyAuthor,
            KenneyLicense,
            KenneyRpgAudioUrl,
            "Assets/Audio/fantasy/book-open.ogg"),
        new(
            "command-ambient-loop",
            "Command jingle",
            "Short optional retro command jingle for demos.",
            KenneyAuthor,
            KenneyLicense,
            KenneyMusicJinglesUrl,
            "Assets/Audio/music/energetic/command-jingle.ogg",
            IsMusic: true),
        new(
            "steel-command-jingle",
            "Steel command jingle",
            "Short metallic command jingle.",
            KenneyAuthor,
            KenneyLicense,
            KenneyMusicJinglesUrl,
            "Assets/Audio/music/energetic/steel-command.ogg",
            IsMusic: true),
        new(
            "bit-command-jingle",
            "8-bit command jingle",
            "Short retro digital command jingle.",
            KenneyAuthor,
            KenneyLicense,
            KenneyMusicJinglesUrl,
            "Assets/Audio/music/energetic/bit-command.ogg",
            IsMusic: true)
    ];

    private static IEnumerable<AlertSoundDefinition> KenneyInterfaceSounds()
    {
        return KenneyInterfaceFiles.Select(file =>
        {
            var id = $"kenney-interface-{Path.GetFileNameWithoutExtension(file).Replace('_', '-')}";
            var name = Humanize(Path.GetFileNameWithoutExtension(file));
            return new AlertSoundDefinition(
                id,
                name,
                "Interface command sound from Kenney Interface Sounds.",
                KenneyAuthor,
                KenneyLicense,
                KenneyInterfaceUrl,
                $"Assets/Audio/interface/{file}");
        });
    }

    private static string Humanize(string value)
    {
        var parts = value.Split('_', StringSplitOptions.RemoveEmptyEntries);
        return string.Join(" ", parts.Select(part => part.Length == 0 ? part : char.ToUpperInvariant(part[0]) + part[1..]));
    }

    public static AlertSoundDefinition Resolve(string? id)
    {
        return BuiltIn.FirstOrDefault(sound => sound.Id.Equals(id, StringComparison.OrdinalIgnoreCase))
               ?? BuiltIn[0];
    }
}
