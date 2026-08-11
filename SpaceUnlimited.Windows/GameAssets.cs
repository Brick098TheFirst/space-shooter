namespace SpaceUnlimited;

internal sealed class GameAssets : IDisposable
{
    public string Root { get; }
    public Image Starfield { get; }
    public Image ClassicShip { get; }
    public Image AsteroidLarge { get; }
    public Image AsteroidMediumA { get; }
    public Image AsteroidMediumB { get; }
    public Image AsteroidSmall { get; }
    public Image AsteroidTiny { get; }
    public Image Shield { get; }
    public Image Laser { get; }
    public IReadOnlyList<Image> ExplosionFrames { get; }

    public static readonly Color[] AccentColors =
    [
        Color.FromArgb(255, 113, 67),
        Color.FromArgb(35, 214, 255),
        Color.FromArgb(174, 103, 255),
        Color.FromArgb(104, 238, 148),
        Color.FromArgb(255, 210, 74)
    ];

    public static readonly Color[] TrailColors =
    [
        Color.FromArgb(255, 120, 56),
        Color.FromArgb(42, 214, 255),
        Color.FromArgb(188, 92, 255),
        Color.FromArgb(102, 255, 184)
    ];

    public GameAssets()
    {
        Root = Path.Combine(AppContext.BaseDirectory, "Assets");
        var imageRoot = Path.Combine(Root, "Images");
        Starfield = Load(imageRoot, "starfield.png");
        ClassicShip = Load(imageRoot, "classic-ship.png");
        AsteroidLarge = Load(imageRoot, "asteroid-large.png");
        AsteroidMediumA = Load(imageRoot, "asteroid-medium-a.png");
        AsteroidMediumB = Load(imageRoot, "asteroid-medium-b.png");
        AsteroidSmall = Load(imageRoot, "asteroid-small.png");
        AsteroidTiny = Load(imageRoot, "asteroid-tiny.png");
        Shield = Load(imageRoot, "shield.png");
        Laser = Load(imageRoot, "laser.png");
        ExplosionFrames = Enumerable.Range(0, 9)
            .Select(i => Load(imageRoot, $"explosion-{i}.png"))
            .ToArray();
    }

    public Image AsteroidFor(float radius, int variant)
    {
        if (radius >= 36) return AsteroidLarge;
        if (radius >= 22) return variant % 2 == 0 ? AsteroidMediumA : AsteroidMediumB;
        if (radius >= 13) return AsteroidSmall;
        return AsteroidTiny;
    }

    private static Image Load(string root, string name)
    {
        var path = Path.Combine(root, name);
        if (!File.Exists(path))
        {
            throw new FileNotFoundException($"Required game asset was not found: {path}");
        }
        return Image.FromFile(path);
    }

    public void Dispose()
    {
        Starfield.Dispose();
        ClassicShip.Dispose();
        AsteroidLarge.Dispose();
        AsteroidMediumA.Dispose();
        AsteroidMediumB.Dispose();
        AsteroidSmall.Dispose();
        AsteroidTiny.Dispose();
        Shield.Dispose();
        Laser.Dispose();
        foreach (var frame in ExplosionFrames) frame.Dispose();
    }
}
