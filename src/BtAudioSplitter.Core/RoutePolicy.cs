namespace BtAudioSplitter.Core;

public enum RouteAction { Paused, FeedbackBlocked, WaitingForDevice, WaitingForSource, Play }

public static class RoutePolicy
{
    public static RouteAction Decide(OutputPreference output, SourcePreference? source,
        bool outputAvailable, bool sourceReady)
    {
        // The user's intent has priority over all device arrival events.
        if (!output.Enabled) return RouteAction.Paused;
        if (source is { Loopback: true } && source.Id == output.Id) return RouteAction.FeedbackBlocked;
        if (!outputAvailable) return RouteAction.WaitingForDevice;
        if (!sourceReady) return RouteAction.WaitingForSource;
        return RouteAction.Play;
    }
}
