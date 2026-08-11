using System.Runtime.Versioning;

namespace SpaceUnlimited;

internal static class Program
{
    [STAThread]
    [SupportedOSPlatform("windows")]
    private static void Main()
    {
        ApplicationConfiguration.Initialize();
        Application.Run(new GameForm());
    }
}
