using System.Text.Json;

namespace SpaceUnlimited;

internal enum Difficulty
{
    Cadet,
    Pilot,
    Ace
}

internal enum ShipStyle
{
    Classic,
    Interceptor,
    Aegis
}

internal enum WeaponRig
{
    Focused,
    Twin,
    Spread
}

internal sealed class PlayerSettings
{
    public Difficulty Difficulty { get; set; } = Difficulty.Pilot;
    public int MusicVolume { get; set; } = 60;
    public int EffectsVolume { get; set; } = 75;
    public bool ScreenShake { get; set; } = true;
    public bool Fullscreen { get; set; }
    // The original Scratch ship is the only hull. The enum remains for settings
    // compatibility with earlier builds, but rendering always uses the classic art.
    public ShipStyle ShipStyle { get; set; } = ShipStyle.Classic;
    public WeaponRig WeaponRig { get; set; } = WeaponRig.Twin;
    public int AccentIndex { get; set; } = 1;
    public int TrailIndex { get; set; } = 1;
    public int HighScore { get; set; }
}

internal static class SettingsStore
{
    private static readonly JsonSerializerOptions Options = new()
    {
        WriteIndented = true
    };

    private static readonly string Folder = Path.Combine(
        Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),
        "SpaceUnlimitedRecharged");

    private static readonly string FilePath = Path.Combine(Folder, "settings.json");

    public static PlayerSettings Load()
    {
        try
        {
            if (File.Exists(FilePath))
            {
                return JsonSerializer.Deserialize<PlayerSettings>(File.ReadAllText(FilePath), Options)
                    ?? new PlayerSettings();
            }
        }
        catch
        {
            // A damaged settings file should never prevent the game from opening.
        }

        return new PlayerSettings();
    }

    public static void Save(PlayerSettings settings)
    {
        try
        {
            Directory.CreateDirectory(Folder);
            File.WriteAllText(FilePath, JsonSerializer.Serialize(settings, Options));
        }
        catch
        {
            // The game remains playable on read-only or managed Windows profiles.
        }
    }
}
