using System.IO;
using System.Text.Json;

namespace BtAudioSplitter.Audio;

/// <summary>
/// Optional local smoke test: opens a stereo render endpoint, writes only zero samples at zero
/// route gain, and starts loopback on that same endpoint. No microphone or audio file is used.
/// This validates native stream setup, not headphone audibility, Bluetooth stability, or VB-CABLE.
/// </summary>
internal static class AudioSelfTest
{
    public static void Run(string reportPath)
    {
        using var backend = new WasapiBackend();
        var device = backend.EnumerateOutputs().FirstOrDefault(d => d.Available && !d.IsHandsFree)
            ?? throw new InvalidOperationException("Kein aktives Stereo-Ausgabegerät für den Selbsttest.");
        using var source = backend.OpenSource(device.Id);
        using var sink = backend.OpenOutput(device.Id, source.Format, 0);
        var resamplingFormat = NAudio.Wave.WaveFormat.CreateIeeeFloatWaveFormat(
            source.Format.SampleRate == 44100 ? 48000 : 44100, 2);
        using var resamplingSink = backend.OpenOutput(device.Id, resamplingFormat, 0);
        long captured = 0;
        source.Data += (_, args) => Interlocked.Add(ref captured, args.BytesRecorded);
        source.Start();
        var silence = new byte[source.Format.SampleRate / 100 * source.Format.BlockAlign];
        var resamplingSilence = new byte[resamplingFormat.SampleRate / 100 * resamplingFormat.BlockAlign];
        for (var i = 0; i < 100; i++)
        {
            sink.Write(silence, silence.Length);
            resamplingSink.Write(resamplingSilence, resamplingSilence.Length);
            Thread.Sleep(10);
        }
        var success = !source.Failed && !sink.Failed && !resamplingSink.Failed;
        File.WriteAllText(reportPath, JsonSerializer.Serialize(new
        {
            Success = success, Device = device.Name, Format = source.Format.ToString(),
            CapturedBytes = Interlocked.Read(ref captured), SourceError = source.Error, OutputError = sink.Error,
            ResamplingInput = resamplingFormat.ToString(), ResamplingError = resamplingSink.Error,
            Scope = "Silent native stream initialization only; no Bluetooth audibility or VB-CABLE test."
        }, new JsonSerializerOptions { WriteIndented = true }));
        if (!success) throw new IOException(source.Error ?? sink.Error ?? resamplingSink.Error ?? "Audio-Selbsttest fehlgeschlagen.");
    }
}
