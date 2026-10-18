namespace BtAudioSplitter.Core;

/// <summary>
/// Reconciles persisted user intent with transient Windows endpoints. Unplugging never changes
/// Enabled. Pausing/removing explicitly does, so a subsequent device arrival cannot undo it.
/// Call all methods on the same worker thread. Only FanOut executes on a capture thread.
/// </summary>
public sealed class Router(IAudioBackend backend, SettingsStore store) : IDisposable
{
    public const int MaximumOutputs = 5;
    private readonly Dictionary<string, IAudioSink> outputs = [];
    private IAudioSink[] fanOut = [];
    private IAudioSource? source;
    private AudioEndpoint[] devices = [];
    private string? error;
    private string? saveError;
    private bool suspended;
    public AppSettings Settings { get; private set; } = store.Load();
    public RouterSnapshot Snapshot { get; private set; } = new(new(), [], [], "Wird vorbereitet …", null);

    public void Add(string id)
    {
        if (Settings.Outputs.Any(o => o.Id == id)) return;
        if (Settings.Outputs.Length >= MaximumOutputs) throw new InvalidOperationException("Maximal fünf Ausgabegeräte.");
        var device = devices.Single(d => d.Id == id);
        if (device.IsCable || device.IsHandsFree || id == Settings.Source?.Id)
            throw new InvalidOperationException("Bitte einen Stereo-Kopfhörer oder Lautsprecher auswählen.");
        Change(Settings with { Outputs = [.. Settings.Outputs, new(id, device.Name, 70, true)] });
    }

    public void SetEnabled(string id, bool enabled)
    {
        Change(Settings with { Outputs = Settings.Outputs.Select(o => o.Id == id ? o with { Enabled = enabled } : o).ToArray() });
        if (!enabled) StopOutput(id);
    }

    public void SetVolume(string id, int value)
    {
        var volume = Math.Clamp(value, 0, 100);
        Change(Settings with { Outputs = Settings.Outputs.Select(o => o.Id == id ? o with { Volume = volume } : o).ToArray() });
        if (outputs.TryGetValue(id, out var output)) output.Volume = volume;
    }

    public void Remove(string id)
    {
        Change(Settings with { Outputs = Settings.Outputs.Where(o => o.Id != id).ToArray() });
        StopOutput(id);
    }

    public void SetSource(string id)
    {
        var device = devices.Single(d => d.Id == id);
        if (!device.IsCable) throw new InvalidOperationException("Als Eingang wird ein VB-CABLE-Ausgabegerät benötigt.");
        StopAudio();
        Change(Settings with { Source = new(id, device.Name, true) });
    }

    /// <summary>Temporarily close streams for driver maintenance without changing saved intent.</summary>
    public void Suspend(bool value)
    {
        suspended = value;
        if (value) StopAudio();
    }

    private void Change(AppSettings settings)
    {
        Settings = settings;
        try { store.Save(settings); saveError = null; }
        catch (Exception e) when (e is IOException or UnauthorizedAccessException)
        { saveError = "Einstellungen nicht gespeichert: " + e.Message; }
    }

    public void Tick()
    {
        error = null;
        try { devices = backend.EnumerateOutputs(); }
        catch (Exception e)
        {
            StopAudio();
            Snapshot = new(Settings, devices, Settings.Outputs.Select(o => new OutputStatus(o,
                o.Enabled ? "Windows-Audio nicht verfügbar" : "Pausiert", "Neuer Versuch folgt automatisch.", false)).ToArray(),
                "Windows-Audio nicht verfügbar", e.Message);
            return;
        }

        // VB-CABLE has multiple render pins. Prefer its ordinary stereo pin, never a physical output.
        if (Settings.Source is null)
        {
            var cable = devices.FirstOrDefault(d => d.IsCable && d.Available &&
                !d.Name.Contains("16", StringComparison.OrdinalIgnoreCase));
            if (cable is not null) Change(Settings with { Source = new(cable.Id, cable.Name, true) });
        }

        var sourceDevice = devices.FirstOrDefault(d => d.Id == Settings.Source?.Id);
        var canCapture = !suspended && sourceDevice is { Available: true, IsCable: true };
        var wantsAudio = Settings.Outputs.Any(o => o.Enabled && devices.Any(d =>
            d.Id == o.Id && d.Available && !d.IsCable && !d.IsHandsFree));

        if (!canCapture || !wantsAudio || source?.Failed == true)
        {
            if (source?.Error is { } captureError) error = captureError;
            StopAudio();
        }
        if (source is null && canCapture && wantsAudio)
        {
            try
            {
                source = backend.OpenSource(sourceDevice!.Id);
                source.Data += FanOut;
                source.Start();
            }
            catch (Exception e) { error = "Audioeingang: " + e.Message; StopAudio(); }
        }

        var statuses = new List<OutputStatus>();
        foreach (var preference in Settings.Outputs)
        {
            var device = devices.FirstOrDefault(d => d.Id == preference.Id);
            var action = RoutePolicy.Decide(preference, Settings.Source,
                device is { Available: true }, source is not null && !source.Failed && !suspended);
            if (device is { IsCable: true } && preference.Enabled) action = RouteAction.FeedbackBlocked;
            if (device is { IsHandsFree: true } && preference.Enabled) action = RouteAction.WaitingForDevice;
            var status = action switch
            {
                RouteAction.Paused => "Pausiert",
                RouteAction.FeedbackBlocked => "Audio-Schleife verhindert",
                RouteAction.WaitingForDevice => "Wartet auf Kopfhörer",
                RouteAction.WaitingForSource => "Wartet auf Audioeingang",
                _ => "Verbunden"
            };
            var detail = action switch
            {
                RouteAction.Paused => "Bleibt pausiert, bis du auf Verbinden klickst.",
                RouteAction.WaitingForDevice => "Setzt nach dem Verbinden durch Windows automatisch fort.",
                RouteAction.FeedbackBlocked => "Der virtuelle Eingang darf kein Ausgabeziel sein.",
                _ => ""
            };
            if (action != RouteAction.Play) StopOutput(preference.Id);
            else
            {
                if (outputs.TryGetValue(preference.Id, out var existing) && existing.Failed)
                    StopOutput(preference.Id);
                if (!outputs.ContainsKey(preference.Id))
                {
                    try
                    {
                        var output = backend.OpenOutput(preference.Id, source!.Format, preference.Volume);
                        outputs.Add(preference.Id, output);
                        Volatile.Write(ref fanOut, outputs.Values.ToArray());
                    }
                    catch (Exception e) { status = "Verbindung wird erneut versucht"; detail = e.Message; }
                }
            }
            if (device?.IsHandsFree == true) detail = "Freisprechprofil erkannt. Bitte die Stereo-Ausgabe in Windows wählen.";
            var display = preference with { Name = device?.Name ?? preference.Name };
            statuses.Add(new(display, status, detail, outputs.TryGetValue(preference.Id, out var sink) && !sink.Failed));
        }
        var sourceStatus = suspended ? "Einrichtung läuft" : sourceDevice is null ? "Treiber / Eingang fehlt" :
            !sourceDevice.Available ? "Eingang nicht verfügbar" : source is null && wantsAudio ? "Verbindung wird erneut versucht" : "Bereit";
        Snapshot = new(Settings, devices, statuses.ToArray(), sourceStatus, saveError ?? error ?? store.Warning);
    }

    private void FanOut(object? sender, NAudio.Wave.WaveInEventArgs args)
    {
        foreach (var output in Volatile.Read(ref fanOut)) output.Write(args.Buffer, args.BytesRecorded);
    }

    private void StopOutput(string id)
    {
        if (!outputs.Remove(id, out var output)) return;
        Volatile.Write(ref fanOut, outputs.Values.ToArray());
        try { output.Dispose(); } catch (Exception e) { error = e.Message; }
    }

    private void StopAudio()
    {
        // Detach callbacks before releasing sinks. A callback already in flight sees closed queues.
        var oldSource = source;
        source = null;
        if (oldSource is not null)
        {
            oldSource.Data -= FanOut;
            try { oldSource.Dispose(); } catch (Exception e) { error = e.Message; }
        }
        foreach (var id in outputs.Keys.ToArray()) StopOutput(id);
    }

    public void Dispose() { StopAudio(); backend.Dispose(); }
}
