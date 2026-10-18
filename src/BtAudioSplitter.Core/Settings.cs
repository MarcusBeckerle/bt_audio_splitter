using System.Text.Json;

namespace BtAudioSplitter.Core;

public sealed record OutputPreference(string Id, string Name, int Volume = 70, bool Enabled = false);
public sealed record SourcePreference(string Id, string Name, bool Loopback);
public sealed record AppSettings
{
    public SourcePreference? Source { get; init; }
    public OutputPreference[] Outputs { get; init; } = [];
}

public sealed class SettingsStore(string path)
{
    private static readonly JsonSerializerOptions JsonOptions = new() { WriteIndented = true };
    public string? Warning { get; private set; }

    public AppSettings Load()
    {
        if (!File.Exists(path)) return new();
        try
        {
            var settings = JsonSerializer.Deserialize<AppSettings>(File.ReadAllText(path))
                ?? throw new JsonException("Leere Einstellungsdatei.");
            if (settings.Outputs is null || settings.Outputs.Any(o => o is null ||
                string.IsNullOrWhiteSpace(o.Id) || string.IsNullOrWhiteSpace(o.Name)) ||
                settings.Source is { } s && (string.IsNullOrWhiteSpace(s.Id) || string.IsNullOrWhiteSpace(s.Name)))
                throw new JsonException("Ungültige Geräteeinstellungen.");
            return settings with
            {
                Outputs = settings.Outputs.DistinctBy(o => o.Id).Take(Router.MaximumOutputs)
                    .Select(o => o with { Volume = Math.Clamp(o.Volume, 0, 100) }).ToArray()
            };
        }
        catch (Exception e) when (e is IOException or UnauthorizedAccessException or JsonException)
        {
            // Preserve the original before a subsequent save replaces it.
            var backup = path + ".invalid-" + DateTime.UtcNow.ToString("yyyyMMddHHmmssfff");
            try { File.Copy(path, backup); }
            catch (Exception copyError) when (copyError is IOException or UnauthorizedAccessException)
            { Warning = "Einstellungen konnten nicht gelesen oder gesichert werden: " + e.Message; return new(); }
            Warning = "Einstellungen konnten nicht gelesen werden. Sicherung: " + backup;
            return new();
        }
    }

    public void Save(AppSettings settings)
    {
        Directory.CreateDirectory(Path.GetDirectoryName(Path.GetFullPath(path))!);
        var temporary = path + ".tmp";
        // Flush before the atomic rename so a crash cannot leave a truncated JSON file.
        using (var stream = new FileStream(temporary, FileMode.Create, FileAccess.Write, FileShare.None))
        {
            JsonSerializer.Serialize(stream, settings, JsonOptions);
            stream.Flush(true);
        }
        File.Move(temporary, path, true);
    }
}
