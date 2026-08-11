using System.Runtime.InteropServices;
using System.Text;

namespace SpaceUnlimited;

internal sealed class AudioManager : IDisposable
{
    [DllImport("winmm.dll", CharSet = CharSet.Unicode)]
    private static extern int mciSendString(string command, StringBuilder? returnValue, int returnLength, IntPtr callback);

    private readonly PlayerSettings _settings;
    private string _currentMusic = string.Empty;
    private bool _disposed;

    public AudioManager(PlayerSettings settings, string assetRoot)
    {
        _settings = settings;
        Open("menuMusic", Path.Combine(assetRoot, "Audio", "menu.wav"));
        Open("gameMusic", Path.Combine(assetRoot, "Audio", "game.wav"));
        Open("laserFx", Path.Combine(assetRoot, "Audio", "laser.wav"));
        Open("explosionFx", Path.Combine(assetRoot, "Audio", "explosion.wav"));
        Open("pickupFx", Path.Combine(assetRoot, "Audio", "pickup.wav"));
        ApplyVolumes();
    }

    public void PlayMenuMusic() => PlayMusic("menuMusic");
    public void PlayGameMusic() => PlayMusic("gameMusic");

    public void PlayLaser() => PlayEffect("laserFx");
    public void PlayExplosion() => PlayEffect("explosionFx");
    public void PlayPickup() => PlayEffect("pickupFx");

    public void ApplyVolumes()
    {
        var music = Math.Clamp(_settings.MusicVolume, 0, 100) * 10;
        var effects = Math.Clamp(_settings.EffectsVolume, 0, 100) * 10;
        foreach (var alias in new[] { "menuMusic", "gameMusic" })
        {
            Send($"setaudio {alias} volume to {music}");
        }

        foreach (var alias in new[] { "laserFx", "explosionFx", "pickupFx" })
        {
            Send($"setaudio {alias} volume to {effects}");
        }
    }

    public void StopAll()
    {
        foreach (var alias in new[] { "menuMusic", "gameMusic", "laserFx", "explosionFx", "pickupFx" })
        {
            Send($"stop {alias}");
        }
        _currentMusic = string.Empty;
    }

    private void PlayMusic(string alias)
    {
        if (_currentMusic == alias)
        {
            return;
        }

        if (_currentMusic.Length > 0)
        {
            Send($"stop {_currentMusic}");
        }

        _currentMusic = alias;
        Send($"seek {alias} to start");
        Send($"play {alias} repeat");
    }

    private static void PlayEffect(string alias)
    {
        Send($"stop {alias}");
        Send($"seek {alias} to start");
        Send($"play {alias}");
    }

    private static void Open(string alias, string path)
    {
        // The Windows MPEGVideo MCI driver also handles PCM WAV files and, unlike
        // the legacy waveaudio driver, supports per-stream setaudio volume.
        Send($"open \"{path}\" type mpegvideo alias {alias}");
    }

    private static void Send(string command)
    {
        try { mciSendString(command, null, 0, IntPtr.Zero); }
        catch { }
    }

    public void Dispose()
    {
        if (_disposed) return;
        StopAll();
        foreach (var alias in new[] { "menuMusic", "gameMusic", "laserFx", "explosionFx", "pickupFx" })
        {
            Send($"close {alias}");
        }
        _disposed = true;
    }
}
