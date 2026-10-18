using System.IO;
using System.Windows;
using BtAudioSplitter.Audio;
using BtAudioSplitter.Setup;

namespace BtAudioSplitter;

public partial class App : Application
{
    private Mutex? instance;
    protected override void OnStartup(StartupEventArgs e)
    {
        base.OnStartup(e);
        if (e.Args is ["--rename-cable", var id])
        {
            try { EndpointNaming.RenameCable(id); Shutdown(0); }
            catch (Exception ex)
            {
                MessageBox.Show("Der Windows-Name konnte nicht geändert werden. Du kannst das Gerät auch in den Windows-Soundeinstellungen umbenennen.\n\n" + ex.Message,
                    "Audio Splitter", MessageBoxButton.OK, MessageBoxImage.Warning);
                Shutdown(1);
            }
            return;
        }
        if (e.Args is ["--diagnose", var diagnosticPath])
        {
            try
            {
                using var backend = new WasapiBackend();
                File.WriteAllText(diagnosticPath, System.Text.Json.JsonSerializer.Serialize(backend.EnumerateOutputs(),
                    new System.Text.Json.JsonSerializerOptions { WriteIndented = true }));
                Shutdown(0);
            }
            catch (Exception ex) { AppLog.Write(ex); Shutdown(1); }
            return;
        }
        if (e.Args is ["--audio-self-test", var reportPath])
        {
            try { Task.Run(() => AudioSelfTest.Run(reportPath)).GetAwaiter().GetResult(); Shutdown(0); }
            catch (Exception ex) { AppLog.Write(ex); Shutdown(1); }
            return;
        }
        var preview = e.Args is ["--preview", _];
        if (!preview)
        {
            instance = new Mutex(true, "Local\\MarcusBeckerle.BtAudioSplitter", out var created);
            if (!created)
            {
                MessageBox.Show("BT Audio Splitter läuft bereits. Öffne das vorhandene Fenster über die Taskleiste.", "BT Audio Splitter");
                instance.Dispose(); instance = null;
                Shutdown(); return;
            }
        }
        DispatcherUnhandledException += (_, args) =>
        {
            AppLog.Write(args.Exception);
            MessageBox.Show(args.Exception.Message, "BT Audio Splitter", MessageBoxButton.OK, MessageBoxImage.Error);
            args.Handled = true;
        };
        var window = new MainWindow(preview ? e.Args[1] : null);
        MainWindow = window;
        window.Show();
    }

    protected override void OnExit(ExitEventArgs e)
    {
        instance?.ReleaseMutex();
        instance?.Dispose();
        base.OnExit(e);
    }
}
