using System.Collections.Concurrent;
using BtAudioSplitter.Core;

namespace BtAudioSplitter.Audio;

/// <summary>Serializes COM lifecycle work off the UI thread, including reconnects and shutdown.</summary>
internal sealed class AudioWorker : IAsyncDisposable
{
    private readonly ConcurrentQueue<(Action<Router> Action, TaskCompletionSource Completion)> commands = new();
    private readonly AutoResetEvent wake = new(false);
    private readonly Thread thread;
    private readonly TaskCompletionSource stopped = new(TaskCreationOptions.RunContinuationsAsynchronously);
    private volatile bool stopping;
    private RouterSnapshot snapshot = new(new(), [], [], "Wird vorbereitet …", null);
    public RouterSnapshot Snapshot => Volatile.Read(ref snapshot);

    public AudioWorker(SettingsStore store)
    {
        thread = new Thread(() => Run(store)) { IsBackground = true, Name = "Audio router" };
        thread.SetApartmentState(ApartmentState.MTA);
        thread.Start();
    }

    public Task Invoke(Action<Router> action)
    {
        if (stopping) return Task.FromException(new ObjectDisposedException(nameof(AudioWorker)));
        var completion = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        commands.Enqueue((action, completion));
        wake.Set();
        return completion.Task;
    }

    private void Run(SettingsStore store)
    {
        try
        {
            using var router = new Router(new WasapiBackend(), store);
            while (!stopping)
            {
                while (commands.TryDequeue(out var command))
                {
                    try { command.Action(router); command.Completion.SetResult(); }
                    catch (Exception e) { command.Completion.SetException(e); }
                }
                try { router.Tick(); Volatile.Write(ref snapshot, router.Snapshot); }
                catch (Exception e)
                {
                    Volatile.Write(ref snapshot, snapshot with { Error = "Audiofehler: " + e.Message });
                    AppLog.Write(e);
                }
                wake.WaitOne(1000);
            }
        }
        catch (Exception e)
        {
            AppLog.Write(e);
            Volatile.Write(ref snapshot, snapshot with { Error = "Windows-Audio konnte nicht gestartet werden: " + e.Message });
        }
        finally
        {
            stopping = true;
            while (commands.TryDequeue(out var command))
                command.Completion.TrySetException(new ObjectDisposedException(nameof(AudioWorker)));
            stopped.TrySetResult();
        }
    }

    public async ValueTask DisposeAsync()
    {
        stopping = true;
        wake.Set();
        await stopped.Task;
        wake.Dispose();
    }
}
