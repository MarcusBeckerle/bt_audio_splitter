using System.ComponentModel;
using System.Runtime.CompilerServices;
using System.Windows.Media;
using BtAudioSplitter.Core;

namespace BtAudioSplitter;

internal sealed class OutputViewModel : INotifyPropertyChanged
{
    public string Id { get; }
    public string Name { get; private set; } = "";
    public string Status { get; private set; } = "";
    public string Detail { get; private set; } = "";
    public bool Enabled { get; private set; }
    public string ActionLabel => Enabled ? "Pausieren" : "Verbinden";
    public string DeviceInfo => Name + "\nWindows-Geräte-ID: " + Id;
    public Brush StatusColor { get; private set; } = Brushes.Gray;
    private int volume;
    public int Volume
    {
        get => volume;
        set { volume = Math.Clamp(value, 0, 100); Notify(); Notify(nameof(VolumeLabel)); }
    }
    public string VolumeLabel => $"{Volume} %";
    public event PropertyChangedEventHandler? PropertyChanged;

    public OutputViewModel(OutputStatus status) { Id = status.Preference.Id; Update(status, null); }
    public void Update(OutputStatus status, int? pendingVolume)
    {
        Name = status.Preference.Name;
        Status = status.Status;
        Detail = status.Detail;
        Enabled = status.Preference.Enabled;
        StatusColor = status.Playing ? new SolidColorBrush(Color.FromRgb(23, 134, 68)) :
            Enabled ? new SolidColorBrush(Color.FromRgb(151, 100, 10)) : new SolidColorBrush(Color.FromRgb(88, 101, 119));
        Volume = pendingVolume ?? status.Preference.Volume;
        Notify("");
    }
    private void Notify([CallerMemberName] string? name = null) => PropertyChanged?.Invoke(this, new(name));
}
