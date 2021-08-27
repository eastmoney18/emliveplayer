package tv.danmaku.ijk.media.player;

import android.content.Context;
import android.net.Uri;
import android.os.Handler;
import android.os.Looper;
import android.os.Message;
import android.util.Log;
import android.view.Surface;
import android.view.SurfaceHolder;


import com.google.android.exoplayer2.ExoPlaybackException;
import com.google.android.exoplayer2.ExoPlayer;
import com.google.android.exoplayer2.ExoPlayerFactory;
import com.google.android.exoplayer2.Format;
import com.google.android.exoplayer2.PlaybackParameters;
import com.google.android.exoplayer2.SimpleExoPlayer;
import com.google.android.exoplayer2.Timeline;
import com.google.android.exoplayer2.extractor.DefaultExtractorsFactory;
import com.google.android.exoplayer2.source.ExtractorMediaSource;
import com.google.android.exoplayer2.source.LoopingMediaSource;
import com.google.android.exoplayer2.source.MediaSource;
import com.google.android.exoplayer2.source.TrackGroupArray;
import com.google.android.exoplayer2.trackselection.DefaultTrackSelector;
import com.google.android.exoplayer2.trackselection.TrackSelectionArray;
import com.google.android.exoplayer2.upstream.DataSource;
import com.google.android.exoplayer2.upstream.DefaultDataSourceFactory;
import com.google.android.exoplayer2.util.Util;

import java.io.FileDescriptor;
import java.io.IOException;
import java.lang.ref.WeakReference;
import java.util.Map;

import tv.danmaku.ijk.media.player.misc.ITrackInfo;

public class ExoMediaPlayer extends AbstractMediaPlayer {
    private final static String TAG = "ExoMediaPlayer";
    private final int EM_EXPLAY_PROGRESS = 1002;

    private SimpleExoPlayer mSimpleExoPlayer;
    private Handler         mMainHandler;
    private boolean         mPause = false;
    private boolean         mBeginPlay;


    @Override
    public void setDisplay(SurfaceHolder sh) {

    }

    @Override
    public void setDataSource(Context context, Uri uri) throws IOException
            , IllegalArgumentException
            , SecurityException
            , IllegalStateException {

        if (mMainHandler == null){
            mMainHandler = new Handler(context.getMainLooper());
        }

        if (mSimpleExoPlayer == null){
            DefaultTrackSelector trackSelector = new DefaultTrackSelector();
            mSimpleExoPlayer = ExoPlayerFactory.newSimpleInstance(context, trackSelector);
            mSimpleExoPlayer.addListener(new MediaPlayerEventListener(this));
        }

        DataSource.Factory dataSourceFactory = new DefaultDataSourceFactory(context,
                Util.getUserAgent(context, "ExoMediaPlayer"));

        MediaSource source = new ExtractorMediaSource(uri
                , dataSourceFactory
                , new DefaultExtractorsFactory()
                , mMainHandler, new MediaSourceEventListener(this));

        mBeginPlay = false;
        mSimpleExoPlayer.prepare(source);
        mSimpleExoPlayer.setPlayWhenReady(true);
    }

    private Handler mHandler = new Handler(Looper.getMainLooper()){
        @Override
        public void handleMessage(Message msg) {
            if (msg.what == EM_EXPLAY_PROGRESS){
                onPlayProgress((int)getCurrentPosition(), (int)getDuration());
                mHandler.sendEmptyMessageDelayed(EM_EXPLAY_PROGRESS, 200);
            }
        }
    };

    @Override
    public void setDataSource(Context context, Uri uri, Map<String, String> headers) throws IOException
            , IllegalArgumentException
            , SecurityException
            , IllegalStateException {

    }

    @Override
    public void setDataSource(FileDescriptor fd) throws IOException, IllegalArgumentException, IllegalStateException {

    }

    @Override
    public void setDataSource(String path) throws IOException, IllegalArgumentException, SecurityException, IllegalStateException {

    }

    @Override
    public String getDataSource() {
        return null;
    }

    @Override
    public void prepareAsync() throws IllegalStateException {
    }

    @Override
    public void start() throws IllegalStateException {
        mPause = false;
        mSimpleExoPlayer.setPlayWhenReady(true);
        mHandler.sendEmptyMessageDelayed(EM_EXPLAY_PROGRESS, 200);
    }

    @Override
    public void stop() throws IllegalStateException {
        mSimpleExoPlayer.stop();
        mHandler.removeMessages(EM_EXPLAY_PROGRESS);
    }

    @Override
    public void pause() throws IllegalStateException {
        mPause = true;
        mSimpleExoPlayer.setPlayWhenReady(false);
        mHandler.removeMessages(EM_EXPLAY_PROGRESS);
    }

    @Override
    public void setScreenOnWhilePlaying(boolean screenOn) {

    }

    public void setPlaybackRate(float rate) throws IllegalStateException{
        PlaybackParameters parameters1 = new PlaybackParameters(rate, 1.0f);
        mSimpleExoPlayer.setPlaybackParameters(parameters1);
    }

    @Override
    public int getVideoWidth() {
        Format format = mSimpleExoPlayer.getVideoFormat();
        if (format == null){
            return 0;
        }

        return format.width;
    }

    @Override
    public int getVideoHeight() {
        Format format = mSimpleExoPlayer.getVideoFormat();
        if (format == null){
            return 0;
        }

        return format.height;
    }

    @Override
    public boolean isPlaying() {
        return mSimpleExoPlayer.getPlayWhenReady();
    }

    @Override
    public void seekTo(long msec) throws IllegalStateException {
        mSimpleExoPlayer.seekTo(msec);
    }

    @Override
    public long getCurrentPosition() {
        return mSimpleExoPlayer.getCurrentPosition();
    }

    @Override
    public long getDuration() {
        return mSimpleExoPlayer.getDuration();
    }

    @Override
    public void release() {
        mSimpleExoPlayer.release();
        mHandler.removeMessages(EM_EXPLAY_PROGRESS);
    }

    @Override
    public void reset() {

    }

    @Override
    public void setVolume(float leftVolume, float rightVolume) {
        mSimpleExoPlayer.setVolume(leftVolume);
    }

    @Override
    public int getAudioSessionId() {
        return 0;
    }

    @Override
    public MediaInfo getMediaInfo() {
        return null;
    }

    @Override
    public void setLogEnabled(boolean enable) {

    }

    @Override
    public boolean isPlayable() {
        return true;
    }

    @Override
    public void setAudioStreamType(int streamtype) {
        mSimpleExoPlayer.setAudioStreamType(streamtype);
    }

    @Override
    public void setKeepInBackground(boolean keepInBackground) {

    }

    @Override
    public int getVideoSarNum() {
        return 0;
    }

    @Override
    public int getVideoSarDen() {
        return 0;
    }

    @Override
    public void setWakeMode(Context context, int mode) {

    }

    @Override
    public void setLooping(boolean looping) {

    }

    @Override
    public boolean isLooping() {
        return false;
    }

    @Override
    public ITrackInfo[] getTrackInfo() {
        return new ITrackInfo[0];
    }

    @Override
    public void setSurface(Surface surface) {

    }

    @Override
    public void setRecordStatus(boolean recordOn) {

    }

    private class MediaPlayerEventListener implements ExoPlayer.EventListener{

        public final WeakReference<ExoMediaPlayer> mWeakMediaPlayer;

        MediaPlayerEventListener(ExoMediaPlayer player) {
            mWeakMediaPlayer = new WeakReference<ExoMediaPlayer>(player);
        }

        @Override
        public void onTimelineChanged(Timeline timeline, Object manifest) {
            Log.d(TAG, "onTimelineChanged....");
        }

        @Override
        public void onTracksChanged(TrackGroupArray trackGroups, TrackSelectionArray trackSelections) {

        }

        @Override
        public void onLoadingChanged(boolean isLoading) {
            ExoMediaPlayer self = mWeakMediaPlayer.get();

            if (self != null){
                notifyOnBufferingUpdate(self.mSimpleExoPlayer.getBufferedPercentage());
            }

            if (!isLoading && mSimpleExoPlayer.getPlayWhenReady() && !mBeginPlay){
                notifyOnPrepared();
                mHandler.sendEmptyMessageDelayed(EM_EXPLAY_PROGRESS, 200);
                mBeginPlay = true;
            }
            else if (isLoading){
                mBeginPlay = false;
            }
        }

        @Override
        public void onPlayerStateChanged(boolean playWhenReady, int playbackState) {
            ExoMediaPlayer self = mWeakMediaPlayer.get();
            if (self == null){
                return;
            }

            if (playbackState == ExoPlayer.STATE_READY && !mPause){
                if (!mBeginPlay){
                    notifyOnPrepared();
                    mBeginPlay = true;
                }
                else{
                    notifyOnSeekComplete();
                }

            }else if (playbackState == ExoPlayer.STATE_ENDED){
                notifyOnCompletion();
                mHandler.removeMessages(EM_EXPLAY_PROGRESS);
            }
        }

        @Override
        public void onPlayerError(ExoPlaybackException error) {
            ExoMediaPlayer self = mWeakMediaPlayer.get();

            if (self == null){
                return;
            }

            Log.e(TAG, "onPlayerError: " + error.getMessage() );
            notifyOnError(IMediaPlayer.MEDIA_ERROR_NETWORK_DISCONNECT, 0);
        }

        @Override
        public void onPositionDiscontinuity() {
        }

        @Override
        public void onPlaybackParametersChanged(PlaybackParameters playbackParameters) {

        }
    }

    private class MediaSourceEventListener implements ExtractorMediaSource.EventListener{

        public final WeakReference<ExoMediaPlayer> mWeakMediaPlayer;

        MediaSourceEventListener(ExoMediaPlayer player) {
            mWeakMediaPlayer = new WeakReference<ExoMediaPlayer>(player);
        }

        @Override
        public void onLoadError(IOException error) {
            ExoMediaPlayer self = mWeakMediaPlayer.get();
            if (self == null){
                return;
            }

            Log.e(TAG, "onLoadError: " + error.getMessage());
            //notifyOnError(-1, -1);
        }
    }
}
