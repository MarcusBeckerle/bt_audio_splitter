using System.Runtime.InteropServices;
using BtAudioSplitter.Audio;

namespace BtAudioSplitter.Setup;

/// <summary>Uses the documented IMMDevice property store in an explicitly elevated helper process.</summary>
internal static class EndpointNaming
{
    public static void RenameCable(string endpointId)
    {
        using var backend = new WasapiBackend();
        if (!backend.EnumerateOutputs().Any(d => d.Id == endpointId && d.IsCable))
            throw new InvalidOperationException("Das ausgewählte Gerät ist kein VB-CABLE-Ausgabegerät.");

        var enumerator = (IDeviceEnumerator)new DeviceEnumerator();
        IDevice? device = null;
        IProperties? properties = null;
        var value = new StringVariant { Type = 31, Pointer = Marshal.StringToCoTaskMemUni("Audio Splitter") };
        try
        {
            Marshal.ThrowExceptionForHR(enumerator.GetDevice(endpointId, out device));
            Marshal.ThrowExceptionForHR(device.OpenPropertyStore(2, out properties)); // STGM_READWRITE
            var key = new PropertyKey { Format = new Guid("a45c254e-df1c-4efd-8020-67d146a850e0"), Id = 14 };
            Marshal.ThrowExceptionForHR(properties.SetValue(ref key, ref value));
            Marshal.ThrowExceptionForHR(properties.Commit());
        }
        finally
        {
            Marshal.FreeCoTaskMem(value.Pointer);
            if (properties is not null) Marshal.ReleaseComObject(properties);
            if (device is not null) Marshal.ReleaseComObject(device);
            Marshal.ReleaseComObject(enumerator);
        }
    }

    [StructLayout(LayoutKind.Sequential)]
    private struct PropertyKey { public Guid Format; public uint Id; }
    // PROPVARIANT is 24 bytes on x64. Only VT_LPWSTR is needed; release its allocation explicitly.
    [StructLayout(LayoutKind.Explicit, Size = 24)]
    private struct StringVariant
    {
        [FieldOffset(0)] public ushort Type;
        [FieldOffset(8)] public IntPtr Pointer;
    }
    [ComImport, Guid("BCDE0395-E52F-467C-8E3D-C4579291692E")]
    private class DeviceEnumerator { }
    [ComImport, Guid("A95664D2-9614-4F35-A746-DE8DB63617E6"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
    private interface IDeviceEnumerator
    {
        [PreserveSig] int EnumAudioEndpoints(int flow, int mask, out IntPtr collection);
        [PreserveSig] int GetDefaultAudioEndpoint(int flow, int role, out IntPtr endpoint);
        [PreserveSig] int GetDevice([MarshalAs(UnmanagedType.LPWStr)] string id, out IDevice device);
    }
    [ComImport, Guid("D666063F-1587-4E43-81F1-B948E807363F"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
    private interface IDevice
    {
        [PreserveSig] int Activate(ref Guid iid, int context, IntPtr parameters, out IntPtr instance);
        [PreserveSig] int OpenPropertyStore(int mode, out IProperties properties);
    }
    [ComImport, Guid("886d8eeb-8cf2-4446-8d02-cdba1dbdcf99"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
    private interface IProperties
    {
        [PreserveSig] int GetCount(out uint count);
        [PreserveSig] int GetAt(uint index, out PropertyKey key);
        [PreserveSig] int GetValue(ref PropertyKey key, out StringVariant value);
        [PreserveSig] int SetValue(ref PropertyKey key, ref StringVariant value);
        [PreserveSig] int Commit();
    }
}
