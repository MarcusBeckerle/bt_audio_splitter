using NAudio.Wave;

namespace BtAudioSplitter.Core;

public sealed record AudioEndpoint(string Id, string Name, bool Available, bool IsCable, bool IsHandsFree)
{
    public string ShortId => Id.Length > 12 ? Id[^12..].TrimEnd('}') : Id;
    public string DisplayName => $"{Name}{(Available ? "" : " · Offline")} · {ShortId}";
}

/// <summary>All lifecycle calls belong to one worker thread; captured frames arrive on an audio thread.</summary>
public interface IAudioBackend : IDisposable
{
    AudioEndpoint[] EnumerateOutputs();
    IAudioSource OpenSource(string endpointId);
    IAudioSink OpenOutput(string endpointId, WaveFormat format, int volume);
}

public interface IAudioSource : IDisposable
{
    WaveFormat Format { get; }
    bool Failed { get; }
    string? Error { get; }
    event EventHandler<WaveInEventArgs>? Data;
    void Start();
}

public interface IAudioSink : IDisposable
{
    bool Failed { get; }
    string? Error { get; }
    int Volume { set; }
    void Write(byte[] data, int count);
}

public sealed record OutputStatus(OutputPreference Preference, string Status, string Detail, bool Playing);
public sealed record RouterSnapshot(AppSettings Settings, AudioEndpoint[] Devices, OutputStatus[] Outputs,
    string SourceStatus, string? Error);
