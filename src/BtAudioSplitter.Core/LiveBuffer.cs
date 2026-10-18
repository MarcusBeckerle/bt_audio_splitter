using NAudio.Wave;

namespace BtAudioSplitter.Core;

/// <summary>A bounded, frame-aligned live queue. Overrun drops oldest audio, never new audio.</summary>
public sealed class LiveBuffer : IWaveProvider
{
    private readonly object gate = new();
    private readonly byte[] data;
    private int head;
    private int size;
    private bool closed;
    public WaveFormat WaveFormat { get; }
    public int BufferedBytes { get { lock (gate) return size; } }
    public long DroppedBytes { get; private set; }

    public LiveBuffer(WaveFormat format, int milliseconds = 180)
    {
        WaveFormat = format;
        var frames = Math.Max(1, format.SampleRate * milliseconds / 1000);
        data = new byte[frames * format.BlockAlign];
    }

    public void Add(byte[] source, int offset, int count)
    {
        ArgumentOutOfRangeException.ThrowIfNegative(offset);
        ArgumentOutOfRangeException.ThrowIfNegative(count);
        if (offset > source.Length - count || count % WaveFormat.BlockAlign != 0)
            throw new ArgumentException("Audio must contain complete frames within the source array.");
        lock (gate)
        {
            if (closed) return;
            if (count > data.Length)
            {
                var skip = count - data.Length;
                offset += skip;
                count -= skip;
                DroppedBytes += skip;
            }
            var overflow = Math.Max(0, size + count - data.Length);
            head = (head + overflow) % data.Length;
            size -= overflow;
            DroppedBytes += overflow;
            var tail = (head + size) % data.Length;
            var first = Math.Min(count, data.Length - tail);
            Buffer.BlockCopy(source, offset, data, tail, first);
            Buffer.BlockCopy(source, offset + first, data, 0, count - first);
            size += count;
        }
    }

    public int Read(byte[] buffer, int offset, int count)
    {
        if (offset < 0 || count < 0 || offset > buffer.Length - count || count % WaveFormat.BlockAlign != 0)
            throw new ArgumentException("Read must request complete frames within the destination array.");
        lock (gate)
        {
            var read = Math.Min(count, size);
            var first = Math.Min(read, data.Length - head);
            Buffer.BlockCopy(data, head, buffer, offset, first);
            Buffer.BlockCopy(data, 0, buffer, offset + first, read - first);
            head = (head + read) % data.Length;
            size -= read;
            Array.Clear(buffer, offset + read, count - read);
            return count; // A live output must stay open during silence and underruns.
        }
    }

    public void Close()
    {
        lock (gate) { closed = true; size = 0; head = 0; }
    }
}
