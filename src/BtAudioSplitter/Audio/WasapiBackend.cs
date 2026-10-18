using BtAudioSplitter.Core;
using NAudio.CoreAudioApi;
using NAudio.Wave;

namespace BtAudioSplitter.Audio;

/// <summary>Windows shared-mode WASAPI. Never opens a microphone or changes the default device.</summary>
internal sealed class WasapiBackend : IAudioBackend
{
    private readonly MMDeviceEnumerator enumerator = new();

    public AudioEndpoint[] EnumerateOutputs()
    {
        var result = new List<AudioEndpoint>();
        foreach (var device in enumerator.EnumerateAudioEndPoints(DataFlow.Render, DeviceState.All))
        {
            using (device)
            {
                try
                {
                    var name = device.FriendlyName;
                    // The interface name survives a user renaming the endpoint to Audio Splitter.
                    var hardware = device.DeviceFriendlyName;
                    var cable = hardware.Contains("VB-Audio Virtual Cable", StringComparison.OrdinalIgnoreCase);
                    var handsFree = name.Contains("Hands-Free", StringComparison.OrdinalIgnoreCase) ||
                        name.Contains("Handsfree", StringComparison.OrdinalIgnoreCase) ||
                        name.Contains("Freisprech", StringComparison.OrdinalIgnoreCase) ||
                        name.Contains("AG Audio", StringComparison.OrdinalIgnoreCase);
                    result.Add(new(device.ID, name, device.State == DeviceState.Active, cable, handsFree));
                }
                catch (System.Runtime.InteropServices.COMException)
                {
                    // Device removal can race enumeration. The next reconciliation will retry it.
                }
            }
        }
        return result.OrderByDescending(d => d.Available).ThenBy(d => d.Name).ThenBy(d => d.Id).ToArray();
    }

    public IAudioSource OpenSource(string endpointId)
    {
        var capture = new LoopbackSource(enumerator.GetDevice(endpointId));
        if (capture.Format.Channels != 2)
        {
            capture.Dispose();
            throw new InvalidOperationException("Bitte den Audioeingang in Windows auf Stereo (2 Kanäle) stellen.");
        }
        return capture;
    }
    public IAudioSink OpenOutput(string endpointId, WaveFormat format, int volume) =>
        new OutputSink(enumerator.GetDevice(endpointId), format, volume);
    public void Dispose() => enumerator.Dispose();

    private sealed class LoopbackSource : IAudioSource
    {
        private readonly MMDevice device;
        private readonly WasapiLoopbackCapture capture;
        private volatile bool failed;
        public bool Failed => failed;
        public string? Error { get; private set; }
        public WaveFormat Format => capture.WaveFormat;
        public event EventHandler<WaveInEventArgs>? Data;

        public LoopbackSource(MMDevice device)
        {
            this.device = device;
            try
            {
                capture = new WasapiLoopbackCapture(device);
                capture.DataAvailable += OnData;
                capture.RecordingStopped += OnStopped;
            }
            catch { device.Dispose(); throw; }
        }

        private void OnData(object? sender, WaveInEventArgs e) => Data?.Invoke(this, e);
        private void OnStopped(object? sender, StoppedEventArgs e) { Error = e.Exception?.Message; failed = true; }
        public void Start() => capture.StartRecording();
        public void Dispose()
        {
            capture.DataAvailable -= OnData;
            capture.RecordingStopped -= OnStopped;
            try { capture.Dispose(); } finally { device.Dispose(); }
        }
    }

    private sealed class OutputSink : IAudioSink
    {
        private readonly MMDevice device;
        private readonly LiveBuffer buffer;
        private readonly GainProvider gain;
        private readonly WasapiOut player;
        private volatile bool failed;
        public bool Failed => failed;
        public string? Error { get; private set; }
        public int Volume { set => gain.Gain = Math.Clamp(value, 0, 100) / 100f; }

        public OutputSink(MMDevice device, WaveFormat format, int volume)
        {
            this.device = device;
            buffer = new LiveBuffer(format);
            gain = new GainProvider(buffer.ToSampleProvider());
            Volume = volume;
            WasapiOut? initializing = null;
            try
            {
                // WASAPI/NAudio converts to each endpoint's negotiated format independently.
                initializing = new WasapiOut(device, AudioClientShareMode.Shared, true, 80);
                initializing.PlaybackStopped += OnStopped;
                initializing.Init(gain.ToWaveProvider());
                initializing.Play();
                player = initializing;
            }
            catch
            {
                buffer.Close();
                initializing?.Dispose();
                device.Dispose();
                throw;
            }
        }

        private void OnStopped(object? sender, StoppedEventArgs e) { Error = e.Exception?.Message; failed = true; }
        public void Write(byte[] data, int count) => buffer.Add(data, 0, count);
        public void Dispose()
        {
            buffer.Close();
            player.PlaybackStopped -= OnStopped;
            try { player.Dispose(); } finally { device.Dispose(); }
        }
    }

    /// <summary>Per-route float gain, independent of Windows master volume and other applications.</summary>
    private sealed class GainProvider(ISampleProvider source) : ISampleProvider
    {
        private float gain = 1;
        public float Gain { set => Volatile.Write(ref gain, value); }
        public WaveFormat WaveFormat => source.WaveFormat;
        public int Read(float[] buffer, int offset, int count)
        {
            var read = source.Read(buffer, offset, count);
            var current = Volatile.Read(ref gain);
            for (var i = offset; i < offset + read; i++) buffer[i] *= current;
            return read;
        }
    }
}
