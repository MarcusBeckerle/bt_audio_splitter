using System.Diagnostics;
using System.IO;
using System.IO.Compression;
using System.Net.Http;
using System.Security.Cryptography;

namespace BtAudioSplitter.Setup;

/// <summary>
/// Downloads an unchanged vendor package on explicit user action. Both install and uninstall use
/// the documented vendor UI; no undocumented silent switches or automatic reboot are used.
/// </summary>
internal static class DriverSetup
{
    public const string PackageUrl = "https://download.vb-audio.com/Download_CABLE/VBCABLE_Driver_Pack45.zip";
    public const string PackageHash = "B950E39F01AF1D04EA623C8F6D8EB9B6EA5C477C637295FABF20631C85116BFB";

    public static async Task RunAsync(IProgress<string> progress, CancellationToken cancellationToken)
    {
        var cache = Path.Combine(AppLog.DataDirectory, "DriverCache");
        Directory.CreateDirectory(cache);
        var archive = Path.Combine(cache, "VBCABLE_Driver_Pack45.zip");
        if (!File.Exists(archive) || !await MatchesHash(archive, cancellationToken))
        {
            progress.Report("VB-CABLE wird vom Hersteller geladen …");
            using var client = new HttpClient { Timeout = TimeSpan.FromSeconds(90) };
            client.DefaultRequestHeaders.UserAgent.ParseAdd("BtAudioSplitter/0.1.0");
            using var response = await client.GetAsync(PackageUrl, HttpCompletionOption.ResponseHeadersRead, cancellationToken);
            response.EnsureSuccessStatusCode();
            var staging = archive + ".download";
            try
            {
                await using (var output = File.Create(staging))
                await using (var input = await response.Content.ReadAsStreamAsync(cancellationToken))
                {
                    var chunk = new byte[65536];
                    long total = 0;
                    int read;
                    while ((read = await input.ReadAsync(chunk, cancellationToken)) != 0)
                    {
                        total += read;
                        if (total > 8_000_000) throw new InvalidDataException("Unerwartete Größe des Treiberpakets.");
                        await output.WriteAsync(chunk.AsMemory(0, read), cancellationToken);
                    }
                }
                if (!await MatchesHash(staging, cancellationToken))
                    throw new InvalidDataException("Das Treiberpaket stimmt nicht mit der geprüften Version überein. Bitte die App aktualisieren.");
                File.Move(staging, archive, true);
            }
            finally { if (File.Exists(staging)) File.Delete(staging); }
        }

        // Use a fresh extraction every time; never execute an old, possibly modified cached EXE.
        var extraction = Path.Combine(cache, "setup-" + Guid.NewGuid().ToString("N"));
        ZipFile.ExtractToDirectory(archive, extraction);
        try
        {
            cancellationToken.ThrowIfCancellationRequested();
            progress.Report("Im VB-CABLE-Fenster „Install Driver“ bzw. „Remove Driver“ wählen.");
            using var process = Process.Start(new ProcessStartInfo
            {
                FileName = Path.Combine(extraction, "VBCABLE_Setup_x64.exe"),
                WorkingDirectory = extraction,
                UseShellExecute = true,
                Verb = "runas"
            }) ?? throw new IOException("Das Installationsprogramm konnte nicht gestartet werden.");
            // Cancellation must not delete files while the elevated installer is still using them.
            await process.WaitForExitAsync();
            progress.Report("Setup geschlossen. Nach Installation/Entfernung Windows neu starten.");
        }
        finally
        {
            try { Directory.Delete(extraction, true); }
            catch (Exception e) when (e is IOException or UnauthorizedAccessException) { AppLog.Write(e); }
        }
    }

    private static async Task<bool> MatchesHash(string path, CancellationToken cancellationToken)
    {
        await using var stream = File.OpenRead(path);
        return Convert.ToHexString(await SHA256.HashDataAsync(stream, cancellationToken)) == PackageHash;
    }
}
