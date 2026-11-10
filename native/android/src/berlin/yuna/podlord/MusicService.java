package berlin.yuna.podlord;

import android.app.Notification;
import android.app.NotificationChannel;
import android.app.NotificationManager;
import android.app.PendingIntent;
import android.app.Service;
import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.content.IntentFilter;
import android.media.AudioAttributes;
import android.media.AudioFocusRequest;
import android.media.AudioManager;
import android.media.MediaMetadata;
import android.media.session.MediaSession;
import android.media.session.PlaybackState;
import android.os.IBinder;
import android.os.Build;
import android.os.Handler;
import android.os.Looper;

/** OS lifetime and media controls only. Qt owns decoding, tracks and playback. */
public final class MusicService extends Service {
    private static final String CHANNEL = "podlord-music";
    private static final String PAUSE = "berlin.yuna.podlord.music.PAUSE";
    private static final String NEXT = "berlin.yuna.podlord.music.NEXT";
    private static long owner;
    private static long generation;
    private long servedGeneration;
    private static AudioManager audio;
    private static AudioFocusRequest focus;
    private MediaSession session;
    private final BroadcastReceiver noisy = new BroadcastReceiver() {
        @Override public void onReceive(final Context context, final Intent intent) { dispatch(0); }
    };
    private static native void nativeCommand(long handle, int action);
    private static synchronized void dispatch(final int action) {
        if (owner != 0) nativeCommand(owner, action);
    }
    public static synchronized boolean start(final Context context, final long handle, final String title) {
        if (owner != 0 && owner != handle) return false;
        final boolean acquired = owner == 0;
        if (acquired) {
            audio = context.getSystemService(AudioManager.class);
            if (audio == null) return false;
            focus = new AudioFocusRequest.Builder(AudioManager.AUDIOFOCUS_GAIN)
                .setAudioAttributes(new AudioAttributes.Builder().setUsage(AudioAttributes.USAGE_MEDIA)
                    .setContentType(AudioAttributes.CONTENT_TYPE_MUSIC).build())
                .setOnAudioFocusChangeListener(change -> {
                    if (change != AudioManager.AUDIOFOCUS_GAIN) dispatch(0);
                }, new Handler(Looper.getMainLooper())).build();
            if (audio.requestAudioFocus(focus) != AudioManager.AUDIOFOCUS_REQUEST_GRANTED) {
                focus = null; audio = null;
                return false;
            }
            owner = handle;
            ++generation;
        }
        try {
            context.startForegroundService(new Intent(context, MusicService.class).putExtra("title", title).putExtra("generation", generation));
            return true;
        } catch (final RuntimeException failure) {
            stop(context, handle);
            return false;
        }
    }
    public static synchronized void stop(final Context context, final long handle) {
        if (owner != handle) return;
        owner = 0;
        ++generation;
        if (audio != null && focus != null) audio.abandonAudioFocusRequest(focus);
        focus = null; audio = null;
        context.stopService(new Intent(context, MusicService.class));
    }
    @Override public void onCreate() {
        super.onCreate();
        getSystemService(NotificationManager.class).createNotificationChannel(
            new NotificationChannel(CHANNEL, "Podlord", NotificationManager.IMPORTANCE_LOW));
        session = new MediaSession(this, "Podlord music");
        session.setCallback(new MediaSession.Callback() {
            @Override public void onPause() { dispatch(0); }
            @Override public void onStop() { dispatch(0); }
            @Override public void onSkipToNext() { dispatch(1); }
        });
        session.setActive(true);
        final IntentFilter filter = new IntentFilter(AudioManager.ACTION_AUDIO_BECOMING_NOISY);
        if (Build.VERSION.SDK_INT >= 33) registerReceiver(noisy, filter, Context.RECEIVER_NOT_EXPORTED);
        else registerReceiver(noisy, filter);
    }
    @Override public int onStartCommand(final Intent intent, final int flags, final int startId) {
        if (intent == null || owner == 0) { stopSelf(); return START_NOT_STICKY; }
        servedGeneration = intent.getLongExtra("generation", generation);
        if (servedGeneration != generation) return START_NOT_STICKY;
        if (PAUSE.equals(intent.getAction())) dispatch(0);
        else if (NEXT.equals(intent.getAction())) dispatch(1);
        final String title = intent.getStringExtra("title");
        if (title != null) session.setMetadata(new MediaMetadata.Builder()
            .putString(MediaMetadata.METADATA_KEY_TITLE, title)
            .putString(MediaMetadata.METADATA_KEY_ARTIST, "Podlord").build());
        session.setPlaybackState(new PlaybackState.Builder().setActions(
            PlaybackState.ACTION_PAUSE | PlaybackState.ACTION_STOP | PlaybackState.ACTION_SKIP_TO_NEXT)
            .setState(PlaybackState.STATE_PLAYING, PlaybackState.PLAYBACK_POSITION_UNKNOWN, 1).build());
        final Intent launch = getPackageManager().getLaunchIntentForPackage(getPackageName());
        final PendingIntent open = PendingIntent.getActivity(this, 0, launch,
            PendingIntent.FLAG_UPDATE_CURRENT | PendingIntent.FLAG_IMMUTABLE);
        final PendingIntent pause = PendingIntent.getService(this, 1, new Intent(this, MusicService.class).setAction(PAUSE), PendingIntent.FLAG_IMMUTABLE);
        final PendingIntent next = PendingIntent.getService(this, 2, new Intent(this, MusicService.class).setAction(NEXT), PendingIntent.FLAG_IMMUTABLE);
        startForeground(41, new Notification.Builder(this, CHANNEL).setSmallIcon(android.R.drawable.ic_media_play)
            .setContentTitle("Podlord").setContentText(session.getController().getMetadata().getString(MediaMetadata.METADATA_KEY_TITLE))
            .setContentIntent(open).setOngoing(true).setOnlyAlertOnce(true)
            .addAction(new Notification.Action.Builder(android.R.drawable.ic_media_pause, "Pause", pause).build())
            .addAction(new Notification.Action.Builder(android.R.drawable.ic_media_next, "Next", next).build())
            .setStyle(new Notification.MediaStyle().setMediaSession(session.getSessionToken()).setShowActionsInCompactView(0, 1)).build());
        return START_NOT_STICKY;
    }
    @Override public void onTaskRemoved(final Intent rootIntent) { dispatch(0); stopSelf(); }
    @Override public void onDestroy() {
        unregisterReceiver(noisy);
        session.release();
        if (servedGeneration == generation) dispatch(0);
        super.onDestroy();
    }
    @Override public IBinder onBind(final Intent intent) { return null; }
}
