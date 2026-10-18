using System.IO;

namespace BtAudioSplitter;

internal static class AppLog
{
    internal static string DataDirectory { get; set; } = Path.Combine(
        Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "BtAudioSplitter");
    private static readonly object Gate = new();

    internal static void Write(Exception exception)
    {
        lock (Gate)
        {
            try
            {
                Directory.CreateDirectory(DataDirectory);
                var path = Path.Combine(DataDirectory, "app.log");
                if (File.Exists(path) && new FileInfo(path).Length > 1_000_000)
                    File.Move(path, path + ".previous", true);
                File.AppendAllText(path, $"{DateTimeOffset.Now:O} {exception}\n");
            }
            catch (Exception e) when (e is IOException or UnauthorizedAccessException) { /* Logging must not crash the UI. */ }
        }
    }
}
