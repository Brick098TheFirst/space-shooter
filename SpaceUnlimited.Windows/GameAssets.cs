using System.Drawing.Imaging;

namespace SpaceUnlimited;

internal sealed class GameAssets : IDisposable
{
    public string Root { get; }
    public Image Starfield { get; }
    public Image ClassicShip { get; }
    private readonly Bitmap[] _colorizedShips;
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
        _colorizedShips = AccentColors.Select(ColorizeClassicShip).ToArray();
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

    public Image ShipFor(int accentIndex) => _colorizedShips[Math.Clamp(accentIndex, 0, _colorizedShips.Length - 1)];

    private Bitmap ColorizeClassicShip(Color accent)
    {
        // Keep the original Scratch silhouette and neutral cockpit intact, while
        // replacing the warm wing paint with the selected hangar color.
        using var source = new Bitmap(ClassicShip);
        var tinted = new Bitmap(source.Width, source.Height, PixelFormat.Format32bppPArgb);
        for (var y = 0; y < source.Height; y++)
        {
            for (var x = 0; x < source.Width; x++)
            {
                var pixel = source.GetPixel(x, y);
                if (pixel.A == 0)
                {
                    tinted.SetPixel(x, y, Color.Transparent);
                    continue;
                }

                var isWarmPaint = pixel.R > 140 && pixel.G < 245 && pixel.R > pixel.B + 18;
                if (!isWarmPaint)
                {
                    tinted.SetPixel(x, y, pixel);
                    continue;
                }

                var stripe = pixel.G > 120;
                var brightness = stripe ? 1.08f : 0.86f;
                var mapped = Color.FromArgb(
                    pixel.A,
                    Math.Clamp((int)(accent.R * brightness), 0, 255),
                    Math.Clamp((int)(accent.G * brightness), 0, 255),
                    Math.Clamp((int)(accent.B * brightness), 0, 255));
                tinted.SetPixel(x, y, mapped);
            }
        }
        return tinted;
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
        foreach (var ship in _colorizedShips) ship.Dispose();
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
