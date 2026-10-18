using System.Collections.ObjectModel;
using System.ComponentModel;
using System.Diagnostics;
using System.IO;
using System.Reflection;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Input;
using System.Windows.Media;
using System.Windows.Media.Imaging;
using System.Windows.Navigation;
using System.Windows.Threading;
using BtAudioSplitter.Audio;
using BtAudioSplitter.Core;
using BtAudioSplitter.Setup;

namespace BtAudioSplitter;

public partial class MainWindow : Window
{
    private readonly AudioWorker? worker;
    private readonly ObservableCollection<OutputViewModel> rows = [];
    private readonly Dictionary<string, int> pendingVolumes = [];
    private readonly DispatcherTimer refresh = new() { Interval = TimeSpan.FromMilliseconds(400) };
    private readonly DispatcherTimer volumeSave = new() { Interval = TimeSpan.FromMilliseconds(120) };
    private bool updating;
    private bool closing;
    private bool closed;
    private bool driverBusy;
    private bool firstRefresh = true;

    public MainWindow(string? previewPath = null)
    {
        InitializeComponent();
        Height = Math.Min(820, SystemParameters.WorkArea.Height - 60);
        OutputList.ItemsSource = rows;
        if (previewPath is not null)
        {
            Height = 1040;
            ShowActivated = false;
            PopulatePreview();
            Loaded += async (_, _) =>
            {
                await Dispatcher.InvokeAsync(() => { }, DispatcherPriority.ApplicationIdle);
                UpdateLayout();
                var visual = (FrameworkElement)Content;
                var bitmap = new RenderTargetBitmap((int)Math.Ceiling(visual.ActualWidth), (int)Math.Ceiling(visual.ActualHeight), 96, 96, PixelFormats.Pbgra32);
                bitmap.Render(visual);
                var encoder = new PngBitmapEncoder();
                encoder.Frames.Add(BitmapFrame.Create(bitmap));
                using (var output = File.Create(previewPath)) encoder.Save(output);
                closed = true; Close();
            };
            return;
        }
        worker = new AudioWorker(new SettingsStore(Path.Combine(AppLog.DataDirectory, "settings.json")));
        refresh.Tick += (_, _) => Refresh();
        volumeSave.Tick += async (_, _) => { volumeSave.Stop(); await FlushVolumes(); };
        refresh.Start();
    }

    private void Refresh()
    {
        if (worker is null || closing) return;
        var state = worker.Snapshot;
        updating = true;
        try
        {
            foreach (var row in rows.Where(r => !state.Outputs.Any(s => s.Preference.Id == r.Id)).ToArray()) rows.Remove(row);
            foreach (var status in state.Outputs)
            {
                var id = status.Preference.Id;
                if (pendingVolumes.TryGetValue(id, out var pending) && pending == status.Preference.Volume) pendingVolumes.Remove(id);
                var row = rows.FirstOrDefault(r => r.Id == id);
                if (row is null) rows.Add(new(status));
                else row.Update(status, pendingVolumes.TryGetValue(id, out pending) ? pending : null);
            }
            OutputCount.Text = $"Kopfhörer · {state.Settings.Outputs.Length} von {Router.MaximumOutputs}";
            EmptyHint.Visibility = rows.Count == 0 ? Visibility.Visible : Visibility.Collapsed;
            AddButton.IsEnabled = rows.Count < Router.MaximumOutputs && !driverBusy;
            SourceStatus.Text = state.SourceStatus;
            var source = state.Devices.FirstOrDefault(d => d.Id == state.Settings.Source?.Id);
            SourceHint.Text = source is null ? (state.Devices.Any(d => d.IsCable) ?
                "Wähle den vorhandenen Eingang unter Einrichtung → Eingang wählen." :
                "Installiere zuerst den virtuellen Audiotreiber unter Einrichtung.") :
                source.Name == "Audio Splitter" ? "Im Player oder in Windows als Ausgabe wählen." : "Windows-Ausgabe: " + source.Name;
            var installed = state.Devices.Any(d => d.IsCable);
            DriverStatus.Text = "Virtueller Audiotreiber: " + (installed ? "Installiert" : "Nicht erkannt");
            DriverButton.Content = installed ? "Treiber entfernen" : "Treiber installieren";
            RenameButton.IsEnabled = source is not null && source.Name != "Audio Splitter" && !driverBusy;
            SourceButton.IsEnabled = installed && !driverBusy;
            DriverButton.IsEnabled = !driverBusy;
            ErrorBox.Visibility = state.Error is null ? Visibility.Collapsed : Visibility.Visible;
            ErrorText.Text = state.Error ?? "";
            SavedLabel.Text = state.Error?.Contains("nicht gespeichert", StringComparison.OrdinalIgnoreCase) == true ?
                "Einstellungen konnten nicht gespeichert werden." : "Lautstärke und Verbindungsstatus werden gespeichert.";
            if (firstRefresh && state.SourceStatus != "Wird vorbereitet …")
            {
                SetupExpander.IsExpanded = !installed;
                firstRefresh = false;
            }
        }
        finally { updating = false; }
    }

    private async Task Execute(Action<Router> action)
    {
        if (worker is null) return;
        try { await worker.Invoke(action); }
        catch (Exception ex) { AppLog.Write(ex); MessageBox.Show(this, ex.Message, "Audio Splitter", MessageBoxButton.OK, MessageBoxImage.Warning); }
    }

    private async void AddClick(object sender, RoutedEventArgs e)
    {
        if (worker is null) return;
        var state = worker.Snapshot;
        var candidates = state.Devices.Where(d => !d.IsCable && !d.IsHandsFree &&
            !state.Settings.Outputs.Any(o => o.Id == d.Id)).ToArray();
        var selected = SelectDevice("Kopfhörer hinzufügen", candidates,
            "Wähle ein Windows-Ausgabegerät. Neue Bluetooth-Kopfhörer zuerst in Windows koppeln.");
        if (selected is not null) await Execute(r => r.Add(selected.Id));
    }

    private async void SourceClick(object sender, RoutedEventArgs e)
    {
        if (worker is null) return;
        var selected = SelectDevice("Virtuellen Eingang wählen", worker.Snapshot.Devices.Where(d => d.IsCable).ToArray(),
            "Wähle den VB-CABLE-Eingang, den du auch im Player als Ausgabe verwendest. Für Filme Stereo wählen.");
        if (selected is not null) await Execute(r => r.SetSource(selected.Id));
    }

    private AudioEndpoint? SelectDevice(string title, AudioEndpoint[] devices, string hint)
    {
        var dialog = new Window { Owner = this, Title = title, Width = 570, Height = 350, MinWidth = 450, MinHeight = 280, WindowStartupLocation = WindowStartupLocation.CenterOwner };
        var panel = new DockPanel { Margin = new Thickness(20) };
        var description = new TextBlock { Text = hint, TextWrapping = TextWrapping.Wrap, Margin = new Thickness(0, 0, 0, 14) };
        DockPanel.SetDock(description, Dock.Top); panel.Children.Add(description);
        var buttons = new StackPanel { Orientation = Orientation.Horizontal, HorizontalAlignment = HorizontalAlignment.Right, Margin = new Thickness(0, 14, 0, 0) };
        var cancel = new Button { Content = "Abbrechen", IsCancel = true, Margin = new Thickness(0, 0, 8, 0) };
        var accept = new Button { Content = "Auswählen", IsDefault = true, IsEnabled = devices.Length > 0 };
        buttons.Children.Add(cancel); buttons.Children.Add(accept); DockPanel.SetDock(buttons, Dock.Bottom); panel.Children.Add(buttons);
        var list = new ListBox { ItemsSource = devices, DisplayMemberPath = nameof(AudioEndpoint.DisplayName), SelectedIndex = devices.Length > 0 ? 0 : -1 };
        panel.Children.Add(list);
        if (devices.Length == 0) description.Text += "\n\nKeine weiteren passenden Geräte gefunden.";
        accept.Click += (_, _) => { if (list.SelectedItem is not null) dialog.DialogResult = true; };
        dialog.Content = panel;
        return dialog.ShowDialog() == true ? list.SelectedItem as AudioEndpoint : null;
    }

    private async void ToggleClick(object sender, RoutedEventArgs e)
    {
        if (((FrameworkElement)sender).DataContext is OutputViewModel row) await Execute(r => r.SetEnabled(row.Id, !row.Enabled));
    }

    private void MenuClick(object sender, RoutedEventArgs e)
    {
        if (((FrameworkElement)sender).DataContext is not OutputViewModel row) return;
        var menu = new ContextMenu();
        var remove = new MenuItem { Header = "Gerät entfernen" };
        remove.Click += async (_, _) => { pendingVolumes.Remove(row.Id); await Execute(r => r.Remove(row.Id)); };
        var info = new MenuItem { Header = "Geräteinformationen" };
        info.Click += (_, _) => MessageBox.Show(this, row.DeviceInfo, "Gerät", MessageBoxButton.OK);
        menu.Items.Add(info); menu.Items.Add(remove);
        menu.PlacementTarget = (UIElement)sender;
        menu.IsOpen = true;
    }

    private void VolumeChanged(object sender, RoutedPropertyChangedEventArgs<double> e)
    {
        if (updating || worker is null || sender is not Slider { DataContext: OutputViewModel row }) return;
        pendingVolumes[row.Id] = (int)Math.Round(e.NewValue);
        // Throttle rather than debounce: volume remains responsive throughout a long drag.
        if (!volumeSave.IsEnabled) volumeSave.Start();
    }
    private async void VolumeReleased(object sender, MouseButtonEventArgs e) => await FlushVolumes();
    private async void VolumeFocusLost(object sender, KeyboardFocusChangedEventArgs e) => await FlushVolumes();
    private Task FlushVolumes()
    {
        var changes = pendingVolumes.ToArray();
        return changes.Length == 0 ? Task.CompletedTask : Execute(r => { foreach (var (id, volume) in changes) r.SetVolume(id, volume); });
    }

    private async void DriverClick(object sender, RoutedEventArgs e)
    {
        if (driverBusy || worker is null) return;
        var installed = worker.Snapshot.Devices.Any(d => d.IsCable);
        if (installed && MessageBox.Show(this, "VB-CABLE wird systemweit entfernt und kann auch von anderen Apps verwendet werden.\n\nIm folgenden Herstellerfenster „Remove Driver“ wählen. Fortfahren?",
            "Audiotreiber entfernen", MessageBoxButton.OKCancel, MessageBoxImage.Warning) != MessageBoxResult.OK) return;
        driverBusy = true;
        Refresh();
        try
        {
            await worker.Invoke(r => r.Suspend(true));
            await DriverSetup.RunAsync(new Progress<string>(message => SetupMessage.Text = message), CancellationToken.None);
        }
        catch (Win32Exception ex) when (ex.NativeErrorCode == 1223) { SetupMessage.Text = "Administratorfreigabe abgebrochen."; }
        catch (Exception ex) { AppLog.Write(ex); SetupMessage.Text = ex.Message; }
        finally { await Execute(r => r.Suspend(false)); driverBusy = false; Refresh(); }
    }

    private async void RenameClick(object sender, RoutedEventArgs e)
    {
        var id = worker?.Snapshot.Settings.Source?.Id;
        if (id is null || driverBusy) return;
        driverBusy = true; Refresh();
        try
        {
            var start = new ProcessStartInfo(Environment.ProcessPath!) { UseShellExecute = true, Verb = "runas" };
            start.ArgumentList.Add("--rename-cable"); start.ArgumentList.Add(id);
            using var process = Process.Start(start) ?? throw new IOException("Umbenennung konnte nicht gestartet werden.");
            await process.WaitForExitAsync();
            SetupMessage.Text = process.ExitCode == 0 ? "Windows-Gerätename auf „Audio Splitter“ gesetzt." : "Umbenennung nicht durchgeführt.";
        }
        catch (Win32Exception ex) when (ex.NativeErrorCode == 1223) { SetupMessage.Text = "Administratorfreigabe abgebrochen."; }
        catch (Exception ex) { AppLog.Write(ex); SetupMessage.Text = ex.Message; }
        finally { driverBusy = false; Refresh(); }
    }

    private void BluetoothClick(object sender, RoutedEventArgs e) => OpenLink("ms-settings:bluetooth");
    private void SoundClick(object sender, RoutedEventArgs e) => OpenLink("ms-settings:sound");
    private void LinkNavigate(object sender, RequestNavigateEventArgs e) { OpenLink(e.Uri.AbsoluteUri); e.Handled = true; }
    private void OpenLink(string target)
    {
        try { Process.Start(new ProcessStartInfo(target) { UseShellExecute = true }); }
        catch (Exception ex) { MessageBox.Show(this, ex.Message, "Link öffnen"); }
    }

    private void AboutClick(object sender, RoutedEventArgs e)
    {
        var assembly = Assembly.GetExecutingAssembly();
        var text = "BT Audio Splitter 0.1.0\nCopyright (c) 2026 Marcus Beckerle\n" +
            "https://github.com/MarcusBeckerle/bt_audio_splitter\n\n" +
            "Nichtkommerzielle Nutzung: PolyForm Noncommercial 1.0.0.\nKommerzielle Lizenz auf Anfrage.\n\n" +
            "VB-CABLE stammt von VB-Audio: https://vb-audio.com/Cable/\nDonationware – Unterstützung willkommen; eigene Lizenzbedingungen.\n\n";
        foreach (var name in assembly.GetManifestResourceNames().Where(n => n.StartsWith("Legal.")).Order())
        {
            using var reader = new StreamReader(assembly.GetManifestResourceStream(name)!);
            text += "\n──── " + name[6..] + " ────\n\n" + reader.ReadToEnd() + "\n";
        }
        new Window
        {
            Owner = this, Title = "Über Audio Splitter · Lizenzen", Width = 700, Height = 600,
            WindowStartupLocation = WindowStartupLocation.CenterOwner,
            Content = new TextBox { Text = text, IsReadOnly = true, TextWrapping = TextWrapping.Wrap, VerticalScrollBarVisibility = ScrollBarVisibility.Auto, Margin = new Thickness(16), Padding = new Thickness(12) }
        }.ShowDialog();
    }

    private async void WindowClosing(object? sender, CancelEventArgs e)
    {
        if (closed || worker is null) return;
        e.Cancel = true;
        if (driverBusy) { SetupMessage.Text = "Bitte zuerst die laufende Treiber-Einrichtung abschließen."; return; }
        if (closing) return;
        closing = true;
        refresh.Stop(); volumeSave.Stop(); IsEnabled = false;
        await FlushVolumes();
        await worker.DisposeAsync();
        closed = true; Close();
    }

    private void PopulatePreview()
    {
        SourceStatus.Text = "Bereit";
        SourceHint.Text = "Im Player oder in Windows als Ausgabe wählen.";
        OutputCount.Text = "Kopfhörer · 2 von 5";
        EmptyHint.Visibility = Visibility.Collapsed;
        DriverStatus.Text = "Virtueller Audiotreiber: Installiert";
        DriverButton.Content = "Treiber entfernen";
        RenameButton.IsEnabled = false;
        rows.Add(new(new(new("preview-sony", "Sony WH-1000XM5", 65, true), "Verbunden", "", true)));
        rows.Add(new(new(new("preview-bose", "Bose QuietComfort 45", 80, true), "Wartet auf Kopfhörer", "Setzt nach dem Verbinden durch Windows automatisch fort.", false)));
    }
}
