using Avalonia.Threading;
using MiniAudioEx.Core.StandardAPI;

namespace Podlord.App;

public interface IAlertSoundPlayer : IDisposable
{
    bool Play(string path, out string error);
}

public sealed class MiniAudioAlertSoundPlayer : IAlertSoundPlayer
{
    private const uint SampleRate = 44_100;
    private const uint Channels = 2;
    private static readonly TimeSpan KeepAlive = TimeSpan.FromSeconds(20);
    private readonly object gate = new();
    private readonly List<PlayingSound> playing = [];
    private readonly DispatcherTimer updateTimer = new() { Interval = TimeSpan.FromMilliseconds(33) };
    private bool initialized;
    private bool disposed;

    public MiniAudioAlertSoundPlayer()
    {
        updateTimer.Tick += (_, _) => Update();
    }

    public bool Play(string path, out string error)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(path);

        lock (gate)
        {
            if (disposed)
            {
                error = "audio player is disposed";
                return false;
            }

            if (!File.Exists(path))
            {
                error = "asset file was not found";
                return false;
            }

            try
            {
                EnsureInitialized();
                var clip = new AudioClip(path);
                var source = new AudioSource();
                source.Play(clip);
                playing.Add(new PlayingSound(source, clip, DateTimeOffset.Now));
                updateTimer.Start();
                error = string.Empty;
                return true;
            }
            catch (Exception ex) when (ex is InvalidOperationException or IOException or ArgumentException)
            {
                error = ex.Message;
                return false;
            }
        }
    }

    public void Dispose()
    {
        lock (gate)
        {
            if (disposed)
            {
                return;
            }

            disposed = true;
            updateTimer.Stop();
            foreach (var item in playing)
            {
                item.Dispose();
            }
            playing.Clear();
            if (initialized)
            {
                AudioContext.Deinitialize();
                initialized = false;
            }
        }
    }

    private void EnsureInitialized()
    {
        if (initialized)
        {
            return;
        }

        AudioContext.Initialize(SampleRate, Channels);
        initialized = true;
    }

    private void Update()
    {
        lock (gate)
        {
            if (disposed || !initialized)
            {
                updateTimer.Stop();
                return;
            }

            AudioContext.Update();
            var cutoff = DateTimeOffset.Now - KeepAlive;
            foreach (var item in playing.Where(item => item.StartedAt < cutoff).ToArray())
            {
                item.Dispose();
                playing.Remove(item);
            }

            if (playing.Count == 0)
            {
                updateTimer.Stop();
            }
        }
    }

    private sealed record PlayingSound(AudioSource Source, AudioClip Clip, DateTimeOffset StartedAt) : IDisposable
    {
        public void Dispose()
        {
            Source.Dispose();
            Clip.Dispose();
        }
    }
}
