using BtAudioSplitter.Core;
using NAudio.Wave;

var tests = new (string Name, Action Run)[]
{
    ("Saved intent reconnects after device power cycle", () =>
    {
        using var c = new Context();
        c.Router.Add("a"); c.Router.Tick();
        Check(c.Backend.Sinks.Count == 1, "initial output");
        c.Backend.Available = false; c.Router.Tick();
        Check(c.Backend.Sinks[0].Disposed, "disconnected output disposed");
        Check(c.Router.Settings.Outputs[0].Enabled, "intent survives disconnect");
        c.Backend.Available = true; c.Router.Tick();
        Check(c.Backend.Sinks.Count == 2, "automatic reconnect");
    }),
    ("Pause survives power cycle and application restart", () =>
    {
        using var c = new Context();
        c.Router.Add("a"); c.Router.Tick(); c.Router.SetEnabled("a", false);
        c.Backend.Available = false; c.Router.Tick(); c.Backend.Available = true; c.Router.Tick();
        Check(c.Backend.Sinks.Count == 1, "paused output not reopened");
        Check(File.Exists(c.Path), "settings file missing: " + c.Router.Snapshot.Error);
        using var backend = new FakeBackend();
        using var restored = new Router(backend, c.Store); restored.Tick();
        Check(!restored.Settings.Outputs[0].Enabled && backend.Sinks.Count == 0, "pause persisted");
    }),
    ("Removing a device clears saved intent and audio", () =>
    {
        using var c = new Context(); c.Router.Add("a"); c.Router.Tick();
        c.Router.Remove("a"); c.Router.Tick();
        Check(c.Backend.Sinks[0].Disposed, "removed sink closed");
        Check(c.Store.Load().Outputs.Length == 0, "removed from disk");
        c.Router.Tick(); Check(c.Backend.Sinks.Count == 1, "not resurrected");
    }),
    ("Five outputs work; a sixth is rejected", () =>
    {
        using var c = new Context();
        foreach (var id in new[] { "a", "b", "c", "d", "e" }) c.Router.Add(id);
        c.Router.Tick(); Check(c.Backend.Sinks.Count == 5, "five active sinks");
        Throws<InvalidOperationException>(() => c.Router.Add("f"));
    }),
    ("Two devices with the same name keep independent volumes", () =>
    {
        using var c = new Context(); c.Router.Add("a"); c.Router.Add("b"); c.Router.Tick();
        c.Router.SetVolume("a", 21); c.Router.SetVolume("b", 84);
        var saved = c.Store.Load();
        c.Router.Tick();
        Check(saved.Outputs.Length == 2, "settings missing: " + c.Router.Snapshot.Error + " / " + c.Store.Warning);
        Check(saved.Outputs[0].Volume == 21 && saved.Outputs[1].Volume == 84, "independent persistence");
        Check(c.Backend.Sinks[0].Volume == 21 && c.Backend.Sinks[1].Volume == 84, "independent live gain");
    }),
    ("One output failure leaves the other stream running", () =>
    {
        using var c = new Context(); c.Router.Add("a"); c.Router.Add("b"); c.Router.Tick();
        var good = c.Backend.Sinks[1]; c.Backend.Sinks[0].Failed = true; c.Router.Tick();
        Check(!good.Disposed && c.Backend.Sinks.Count == 3, "only failed sink replaced");
    }),
    ("Capture failure rebuilds routes without losing preferences", () =>
    {
        using var c = new Context(); c.Router.Add("a"); c.Router.Tick();
        c.Backend.Sources[0].Failed = true; c.Router.Tick();
        Check(c.Backend.Sources.Count == 2 && c.Backend.Sinks.Count == 2, "capture and output rebuilt");
        Check(c.Backend.Sinks[0].Disposed, "old sink closed");
    }),
    ("Missing driver waits and resumes when available", () =>
    {
        using var c = new Context(); c.Backend.CableAvailable = false; c.Router.Add("a"); c.Router.Tick();
        Check(c.Backend.Sinks.Count == 0, "no capture without source");
        c.Backend.CableAvailable = true; c.Router.Tick(); Check(c.Backend.Sinks.Count == 1, "source returned");
    }),
    ("Enumeration error closes audio and recovers", () =>
    {
        using var c = new Context(); c.Router.Add("a"); c.Router.Tick();
        c.Backend.EnumerationFails = true; c.Router.Tick();
        Check(c.Backend.Sinks[0].Disposed && c.Router.Snapshot.Error is not null, "safe error state");
        c.Backend.EnumerationFails = false; c.Router.Tick(); Check(c.Backend.Sinks.Count == 2, "recovered");
    }),
    ("Fan-out delivers the same frames to all outputs", () =>
    {
        using var c = new Context(); c.Router.Add("a"); c.Router.Add("b"); c.Router.Tick();
        c.Backend.Sources[0].Emit(); Check(c.Backend.Sinks.All(s => s.Bytes == 16), "identical fan-out");
    }),
    ("Driver maintenance suspends and restores without changing intent", () =>
    {
        using var c = new Context(); c.Router.Add("a"); c.Router.Tick();
        c.Router.Suspend(true); c.Router.Tick(); Check(c.Backend.Sinks.Count == 1 && c.Backend.Sinks[0].Disposed, "suspended");
        c.Router.Suspend(false); c.Router.Tick(); Check(c.Backend.Sinks.Count == 2, "resumed");
    }),
    ("Feedback and hands-free output choices are rejected", () =>
    {
        using var c = new Context(); Throws<InvalidOperationException>(() => c.Router.Add("cable"));
        Throws<InvalidOperationException>(() => c.Router.Add("handsfree"));
        Check(RoutePolicy.Decide(new("same", "same", 70, true), new("same", "same", true), true, true) == RouteAction.FeedbackBlocked, "feedback policy");
    }),
    ("Live queue preserves newest complete frames under overrun", () =>
    {
        var buffer = new LiveBuffer(new WaveFormat(1000, 16, 1), 4);
        buffer.Add([1, 2, 3, 4, 5, 6], 0, 6); buffer.Add([7, 8, 9, 10, 11, 12], 0, 6);
        var result = new byte[8]; buffer.Read(result, 0, 8);
        Check(result.SequenceEqual(new byte[] { 5, 6, 7, 8, 9, 10, 11, 12 }), "latest frames retained");
        Check(buffer.DroppedBytes == 4, "overrun counted");
    }),
    ("Underruns return silence; closed queues reject late capture", () =>
    {
        var buffer = new LiveBuffer(new WaveFormat(48000, 16, 2));
        buffer.Add([1, 2, 3, 4], 0, 4); var result = new byte[8]; buffer.Read(result, 0, 8);
        Check(result.SequenceEqual(new byte[] { 1, 2, 3, 4, 0, 0, 0, 0 }), "silence padding");
        buffer.Close(); buffer.Add([1, 2, 3, 4], 0, 4); buffer.Read(result, 0, 8);
        Check(result.All(b => b == 0), "late callback ignored");
    }),
    ("Oversized packets and invalid frame lengths are handled", () =>
    {
        var buffer = new LiveBuffer(new WaveFormat(1000, 16, 1), 2);
        buffer.Add([1, 2, 3, 4, 5, 6], 0, 6); var result = new byte[4]; buffer.Read(result, 0, 4);
        Check(result.SequenceEqual(new byte[] { 3, 4, 5, 6 }), "oversized newest tail");
        Throws<ArgumentException>(() => buffer.Add([1, 2, 3], 0, 3));
    }),
    ("Concurrent capture and playback keep queue bounded", () =>
    {
        var buffer = new LiveBuffer(new WaveFormat(48000, 16, 2));
        Parallel.Invoke(() => { for (var i = 0; i < 10000; i++) buffer.Add(new byte[192], 0, 192); },
            () => { var read = new byte[192]; for (var i = 0; i < 10000; i++) buffer.Read(read, 0, read.Length); });
        Check(buffer.BufferedBytes <= 34560 && buffer.BufferedBytes % 4 == 0, "bounded and frame aligned");
    }),
    ("Corrupt settings are backed up and recover safely", () =>
    {
        using var c = new Context(); File.WriteAllText(c.Path, "{bad json");
        var store = new SettingsStore(c.Path); Check(store.Load().Outputs.Length == 0, "safe defaults");
        Check(store.Warning is not null && Directory.GetFiles(c.Directory, "*.invalid-*").Length == 1, "backup and warning");
    })
};

var failures = 0;
foreach (var test in tests)
{
    try { test.Run(); Console.WriteLine("PASS " + test.Name); }
    catch (Exception ex) { failures++; Console.Error.WriteLine("FAIL " + test.Name + ": " + ex); }
}
Console.WriteLine($"{tests.Length - failures}/{tests.Length} passed");
return failures == 0 ? 0 : 1;

static void Check(bool value, string message) { if (!value) throw new Exception(message); }
static void Throws<T>(Action action) where T : Exception
{
    try { action(); } catch (T) { return; }
    throw new Exception("Expected " + typeof(T).Name);
}

sealed class Context : IDisposable
{
    public string Directory { get; } = System.IO.Path.Combine(System.IO.Path.GetTempPath(), "BtAudioSplitter-tests-" + Guid.NewGuid().ToString("N"));
    public string Path => System.IO.Path.Combine(Directory, "settings.json");
    public SettingsStore Store { get; }
    public FakeBackend Backend { get; } = new();
    public Router Router { get; }
    public Context() { Store = new(Path); Router = new(Backend, Store); Router.Tick(); }
    public void Dispose() { Router.Dispose(); if (System.IO.Directory.Exists(Directory)) System.IO.Directory.Delete(Directory, true); }
}
sealed class FakeBackend : IAudioBackend
{
    public bool Available = true, CableAvailable = true, EnumerationFails;
    public List<FakeSource> Sources { get; } = [];
    public List<FakeSink> Sinks { get; } = [];
    public AudioEndpoint[] EnumerateOutputs()
    {
        if (EnumerationFails) throw new IOException("Device service unavailable");
        return [new("cable", "CABLE Input", CableAvailable, true, false), new("handsfree", "Hands-Free", true, false, true),
            .. new[] { "a", "b", "c", "d", "e", "f" }.Select(id => new AudioEndpoint(id, "Same name", Available, false, false))];
    }
    public IAudioSource OpenSource(string endpointId) { var source = new FakeSource(); Sources.Add(source); return source; }
    public IAudioSink OpenOutput(string endpointId, WaveFormat format, int volume) { var sink = new FakeSink { Volume = volume }; Sinks.Add(sink); return sink; }
    public void Dispose() { }
}
sealed class FakeSource : IAudioSource
{
    public WaveFormat Format => WaveFormat.CreateIeeeFloatWaveFormat(48000, 2);
    public bool Failed { get; set; }
    public string? Error => Failed ? "Capture disconnected" : null;
    public event EventHandler<WaveInEventArgs>? Data;
    public void Emit() => Data?.Invoke(this, new WaveInEventArgs(new byte[16], 16));
    public void Start() { }
    public void Dispose() { }
}
sealed class FakeSink : IAudioSink
{
    public bool Failed { get; set; }
    public string? Error => Failed ? "Output disconnected" : null;
    public int Volume { get; set; }
    public int Bytes { get; private set; }
    public bool Disposed { get; private set; }
    public void Write(byte[] data, int count) { if (Disposed) throw new Exception("Written after dispose"); Bytes += count; }
    public void Dispose() => Disposed = true;
}
