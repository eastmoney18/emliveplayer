package com.eastmoney.emlivesdkandroid;

import android.content.Context;
import android.graphics.Bitmap;
import android.media.AudioManager;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.os.Message;
import android.util.Log;
import android.view.ViewGroup;

import com.eastmoney.emlivesdkandroid.ui.EMLiveVideoView2;
import com.eastmoney.emlivesdkandroid.util.ProcessQueue;
import com.medialivelib.image.MLImageBufferSource;
import com.medialivelib.image.MLImageContext;

import java.io.BufferedReader;
import java.io.IOException;
import java.io.InputStreamReader;
import java.util.Timer;
import java.util.TimerTask;
import java.util.concurrent.locks.Lock;
import java.util.concurrent.locks.ReentrantLock;

import tv.danmaku.ijk.media.player.AndroidMediaPlayer;
import tv.danmaku.ijk.media.player.ExoMediaPlayer;
import tv.danmaku.ijk.media.player.IMediaPlayer;
import tv.danmaku.ijk.media.player.IMediaPlayerPresentBufferListener;
import tv.danmaku.ijk.media.player.IjkMediaPlayer;

/**
 * Created by eastmoneyd_pcfs on 2018/2/27.
 */

public class EMLivePlayer2 {
    public static final String TAG = "EMLivePlayer2";
    private static final int MSG_UPDATE_INFO = 1;
    private static final int MSG_RECV_FIRST_I_FRAME = 2;
    private static final int MSG_REFRESH_NET_STATUS = 3;
    private static final int MSG_RELEASE_MEDIA_PLAYER = 4;
    private static final int MSG_AUDIO_DEVICE_FOCUS = 5;
    private static final int MSG_GET_PLAYURLS_TIMEOUT = 6;
    private static final int MSG_PLAYURLS_EMPTY = 7;
    private static final int MSG_PLAY_SEEK_COMPLETE = 8;
    private static final int MSG_PLAY_LIVE_END = 9;
    private static final int MSG_TYPE_HW_DECODE_VIDEO_FAILED = 10;
    private static final int MSG_TYPE_HW_DECODE_2_SOFT_DECODE = 11;
    public static final String ON_RECEIVE_VIDEO_FRAME = "com.eastmonet.emlivesdkandroid.EMLivePlayer.OnReceiveVideoFrame";
    public static final int PLAY_TYPE_LIVE_RTMP = 0;
    public static final int PLAY_TYPE_LIVE_FLV = 1;
    public static final int PLAY_TYPE_VOD_FLV = 2;
    public static final int PLAY_TYPE_VOD_HLS = 3;
    public static final int PLAY_TYPE_VOD_MP4 = 4;
    public static final int PLAY_TYPE_LOCAL_VIDEO = 5;


    /*
     *  ?????????????????? add by ccl
     *  ?????????2018-10-08
     */
    public static final int PLAY_TYPE_NET_AUDIO = 6;
    public static final int PLAY_TYPE_NET_EXAUDIO = 7;

    public static final int PLAY_STATE_NOT_INIT = 0;
    public static final int PLAY_STATE_STARTING = 1;
    public static final int PLAY_STATE_STRTED = 2;
    public static final int PLAY_STATE_STOPPING = 3;
    public static final int PLAY_STATE_STOPPED = 4;
    public static final int PLAY_STATE_SEEKING = 5;
    public static final int PLAY_STATE_EXITED = 7;//read data thread exit , so can't resume play

    private IEMLivePlayListener mLiveListener = null;
    private volatile int mState = PLAY_STATE_NOT_INIT;
    private EMLivePlayConfig mConfig = null;
    private Context mContext = null;
    private EMLiveVideoView2 mRenderView = null;
    private ViewGroup mParentView = null; //??????????????????view??????
    private IMediaPlayer mediaPlayer = null;
    private int mVideoWidth = 0;
    private int mVideoHeight = 0;
    private Bundle mNetBundle = new Bundle();
    private volatile boolean mSeeking = false;

    private boolean mEnablehardAcc = false;
    private int mServerIp;
    private Timer mNetInfoTimer = null;
    private ProcessQueue mProcessQueue = null;
    private volatile boolean mPaused = false;
    private int mRotationDegree = 0;
    private int mRenderMode = EMLiveConstants.RENDER_MODE_FILL_PRESERVE_AR_FILL;
    private boolean mMute = false;
    private MLImageContext mImageContext = null;
    private final Lock mNetBundleLock = new ReentrantLock();
    private MLImageBufferSource mBufferSource = null;
    private boolean mFirstFrameRendered = false;
    private boolean mPlayStatictis = false;

    private AudioManager mAudioManager;
    private int mPlayStartPosition;
    final private Object mReleaseObj = new Object();

    private boolean mRecordUserResume = false;  //????????????????????????resume

    private DataCallback mDataCallback;//????????????????????????

    public EMLivePlayer2(Context context) {
        if (!EMLiveBase.mNativeLoaded) {
            System.loadLibrary("emlivenative");
            System.loadLibrary("medialib");
            EMLiveBase.mNativeLoaded = true;
        }
        this.mContext = context;
        mNetBundle.putString(EMLiveConstants.EVT_DESCRIPTION, "netinfo status");
        mProcessQueue = new ProcessQueue("EMLivePlayer Msg queue");
        mProcessQueue.Start();
        mPaused = false;
        mImageContext = new MLImageContext();
        mImageContext.init();

        mAudioManager = (AudioManager) context.getSystemService(Context.AUDIO_SERVICE);
    }

    /**
     * ??????????????????
     * @param config:??????????????????????????????????????????????????????????????????????????????????????????????????????
     */
    public void setConfig(EMLivePlayConfig config) {
        this.mConfig = config;
    }

    /**
     *@return :?????????????????????????????????????????????
     */
    public static int[] getSDKVersion() {
        return EMLiveConstants.sdkVersion;
    }

    /**
     * ?????????????????????????????????
     * @param view ????????????View??????
     */
    public void setPlayerView(EMLiveVideoView2 view) {
        if (mParentView != null) {
            setPlayerView((ViewGroup)null);
        }
        if (mRenderView != view) {
            if (view != null) {
                view.setDisplayType(EMLiveVideoView2.RENDER_MODE_FASTIMAGE_TEXTURE_VIEW);
            }
            if (mRenderView != null) {
                mRenderView.bindToMLImageContext(null);
            }
        }
        if (view != null) {
            view.bindToMLImageContext(mImageContext);
            view.setRenderRotation(mRotationDegree);
            view.setRenderMode(mRenderMode);
        }
        mRenderView = view;
    }

    /**
     * ????????????viewGroup
     * @param viewGroup ????????????????????????view,???????????????layout
     */
    public void setPlayerViewGroup(ViewGroup viewGroup, int viewType) {
        if (mParentView != viewGroup) {
            if (mParentView != null && mRenderView != null) {
                mParentView.removeView(mRenderView);
            }
        }
        if (viewGroup == null) {
            if (mRenderView != null) {
                mRenderView.bindToMLImageContext(null);
                mRenderView = null;
            }
        } else {
            if (mRenderView == null) {
                mRenderView = new EMLiveVideoView2(mContext);
                mRenderView.setDisplayType(viewType);
                mRenderView.bindToMLImageContext(mImageContext);
                mRenderView.setRenderRotation(mRotationDegree);
                mRenderView.setRenderMode(mRenderMode);
            }
            else{
                //fix
                ViewGroup parent = (ViewGroup) mRenderView.getParent();
                if (parent != null){
                    parent.removeView(mRenderView);
                }
            }

            ViewGroup.LayoutParams layoutParams = new ViewGroup.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT);
            viewGroup.addView(mRenderView, layoutParams);
        }
        mParentView = viewGroup;
    }

    /**
     * ????????????viewGroup
     * @param viewGroup ????????????????????????view,???????????????layout
     */
    public void setPlayerView(ViewGroup viewGroup) {
        setPlayerViewGroup(viewGroup, EMLiveVideoView2.RENDER_MODE_FASTIMAGE_TEXTURE_VIEW);
    }

    /**
     * ?????????????????????????????????
     * @param view ????????????View??????
     * @param viewType ?????????View???????????? EMLiveVideoView.RENDER_MODE_FASTIMAGE_SURFACE_VIEW or EMLiveVideoView.RENDER_MODE_FASTIMAGE_TEXTURE_VIEW
     */
    public void setPlayerView(EMLiveVideoView2 view, int viewType) {
        if (mParentView != null) {
            setPlayerView((ViewGroup)null);
        }
        if (mRenderView != view) {
            if (view != null) {
                view.setDisplayType(viewType);
            }
            if (mRenderView != null) {
                mRenderView.bindToMLImageContext(null);
            }
        }
        if (view != null) {
            view.bindToMLImageContext(mImageContext);
            view.setRenderRotation(mRotationDegree);
            view.setRenderMode(mRenderMode);
        }
        mRenderView = view;
    }

    /**
     * ?????????????????????????????????????????????
     */
    //TODO ?????????????????????????????????

    private void applyPlayConfig() {
        if (mConfig != null && mediaPlayer != null) {
            IjkMediaPlayer player = (IjkMediaPlayer)mediaPlayer;
            player.setOption(IjkMediaPlayer.OPT_CATEGORY_PLAYER, "reconnect_count", mConfig.mConnectRetryCount);
            player.setOption(IjkMediaPlayer.OPT_CATEGORY_PLAYER, "reconnect_interval", mConfig.mConnectRetryInterval);
            player.setOption(IjkMediaPlayer.OPT_CATEGORY_PLAYER, "play_channel_mode", mConfig.mPlayAudioChannelMode);
            player.setOption(IjkMediaPlayer.OPT_CATEGORY_PLAYER, "dns_timeout", mConfig.mDnsCacheValidTime);
            player.setOption(IjkMediaPlayer.OPT_CATEGORY_PLAYER, "dns_cache_count", mConfig.mDnsCacheCount);
            player.setOption(IjkMediaPlayer.OPT_CATEGORY_PLAYER, "start-on-prepared", mConfig.mAutoPlay ? 1 : 0);
            player.setOption(IjkMediaPlayer.OPT_CATEGORY_PLAYER, "view-video-first-frame", mConfig.mViewFirstFrameOnPrepare ? 1: 0);
            if (Build.VERSION.SDK_INT < 23){
                player.setOption(IjkMediaPlayer.OPT_CATEGORY_PLAYER, "enable_sonic_handle", 1);
            }
            player.setOption(IjkMediaPlayer.OPT_CATEGORY_PLAYER, "audio_stream_type", mConfig.mAudioStreamType);
            switch (mConfig.mLoadingStrategy) {
                case EMLiveConstants.EM_PLAY_LOADING_STARTEGY_DEFAULT:
                    break;
                case EMLiveConstants.EM_PLAY_LOADING_STARTEGY_FAST:
                    player.setOption(IjkMediaPlayer.OPT_CATEGORY_PLAYER, "probesize", 100); // ????????????100KB
                    break;
                case EMLiveConstants.EM_PLAY_LOADING_STARTEGY_NORMAL:
                    player.setOption(IjkMediaPlayer.OPT_CATEGORY_PLAYER, "probesize", 200); // ????????????100KB
                    break;
                case EMLiveConstants.EM_PLAY_LOADING_STARTEGY_CUSTOM:
                    player.setOption(IjkMediaPlayer.OPT_CATEGORY_PLAYER, "probesize", mConfig.mCustomLoadingValue >= 10 ? mConfig.mCustomLoadingValue : 300);
                    break;
                default:
                    break;
            }
            Log.d(TAG, "loading strategy is " + mConfig.mLoadingStrategy);
        }
    }

    private void refreshNetInfo() {
        IjkMediaPlayer mp = (IjkMediaPlayer)mediaPlayer;
        if (mp != null) {
            mNetBundleLock.lock();
            try {
                if (mNetBundle == null) return;
                mNetBundle.putInt(EMLiveConstants.NET_STATUS_NET_SPEED, (int) mp.getTcpSpeed() * 8 / 1000);
                mNetBundle.putInt(EMLiveConstants.NET_STATUS_CACHE_SIZE, (int) (mp.getAudioCachedBytes() + mp.getVideoCachedBytes()));
                mNetBundle.putLong(EMLiveConstants.EVT_TIME, System.currentTimeMillis());
                mNetBundle.putInt(EMLiveConstants.NET_STATUS_VIDEO_BITRATE, (int) mp.getVideoBitrate() * 8 / 1000);
                mNetBundle.putInt(EMLiveConstants.NET_STATUS_AUDIO_BITRATE, (int) mp.getAudioBitrate() * 8 / 1000);
                mNetBundle.putInt(EMLiveConstants.NET_STATUS_VIDEO_FPS, (int) mp.getVideoOutputFramesPerSecond());
                mNetBundle.putInt(EMLiveConstants.NET_STATUS_VIDEO_WIDTH, mRotationDegree % 2 > 0 ?  mVideoHeight : mVideoWidth);
                mNetBundle.putInt(EMLiveConstants.NET_STATUS_VIDEO_HEIGHT, mRotationDegree % 2 > 0 ?  mVideoWidth : mVideoHeight);
                mNetBundle.putString(EMLiveConstants.NET_STATUS_CPU_USAGE, /*executeTop()*/ "User 10%, System 5%");
                mNetBundle.putString(EMLiveConstants.NET_STATUS_SERVER_IP, ipN2A(mServerIp));
            } finally {
                mNetBundleLock.unlock();
            }
            mHandler.sendEmptyMessage(MSG_REFRESH_NET_STATUS);
            synchronized (this) {
                if (mNetInfoTimer != null) {
                    mNetInfoTimer.schedule(new TimerTask() {
                        @Override
                        public void run() {
                            refreshNetInfo();
                        }
                    }, 500);
                }
            }
        }
    }

    private AudioManager.OnAudioFocusChangeListener mAudioFocusListener = new AudioManager.OnAudioFocusChangeListener() {
        @Override
        public void onAudioFocusChange(int focusChange) {
            if (focusChange == AudioManager.AUDIOFOCUS_LOSS
                    | focusChange == AudioManager.AUDIOFOCUS_LOSS_TRANSIENT
                    | focusChange == AudioManager.AUDIOFOCUS_LOSS_TRANSIENT_CAN_DUCK){
                Message msg = new Message();
                msg.what = MSG_AUDIO_DEVICE_FOCUS;
                msg.arg1 = AudioManager.AUDIOFOCUS_LOSS;
                mHandler.sendMessageAtTime(msg, 0);
                Log.i(TAG, "onAudioFocusChange: AUDIOFOCUS_LOSS");
            }
            else if(focusChange == AudioManager.AUDIOFOCUS_GAIN){
                Message msg = new Message();
                msg.what = MSG_AUDIO_DEVICE_FOCUS;
                msg.arg1 = AudioManager.AUDIOFOCUS_GAIN;
                mHandler.sendMessageAtTime(msg, 0);
                Log.i(TAG, "onAudioFocusChange: AUDIOFOCUS_GAIN");
            }
        }
    };

    private int startPlayInternal(final String url, int playType, boolean bMulti) {
        mPlayStartPosition = 0;
        if (mState != PLAY_STATE_NOT_INIT && mState != PLAY_STATE_STOPPED) {
            Log.e(TAG, "started play now.");
            return -1;
        }
        //fix : start play again when play complete , 2021/01/18  add by lxs start
        if(mediaPlayer != null){
            stopPlay(true);
        }
        //fix : start play again when play complete , end
        mState = PLAY_STATE_STARTING;
        mPlayStatictis = false;
        mPaused = false;
        mRecordUserResume = false;
        mFirstFrameRendered = false;

        this.mediaPlayer = new IjkMediaPlayer();
        try {
            this.mediaPlayer.setOnPreparedListener(new IMediaPlayer.OnPreparedListener() {
                @Override
                public void onPrepared(IMediaPlayer mp) {
                    if (!mPlayStatictis && mConfig != null && mConfig.mUpdateAVtatistic){
                        // ??????????????????
                        mPlayStatictis = true;
                    }

                    mVideoWidth = mp.getVideoWidth();
                    mVideoHeight = mp.getVideoHeight();
                    //mHandler.sendEmptyMessageDelayed(MSG_UPDATE_INFO, 500);
                    if (mNetInfoTimer == null) {
                        mNetInfoTimer = new Timer("player net refresh timer");
                    }
                    mNetInfoTimer.schedule(new TimerTask() {
                        @Override
                        public void run() {
                            refreshNetInfo();
                        }
                    }, 500);
                    mState = PLAY_STATE_STRTED;

                    if(mPaused){
                        mp.pause();
                    }
                    else if ((mConfig != null && !mConfig.mAutoPlay)){
                        if(!mConfig.mViewFirstFrameOnPrepare){
                            mp.pause();
                        }
                    }
                    else{
                        mp.start();
                    }

                    if (mLiveListener != null) {
                        Bundle prepareBundle = new Bundle();
                        prepareBundle.putInt(EMLiveConstants.NET_STATUS_VIDEO_WIDTH, mVideoWidth);
                        prepareBundle.putInt(EMLiveConstants.NET_STATUS_VIDEO_HEIGHT, mVideoHeight);
                        prepareBundle.putInt(EMLiveConstants.EVT_PLAY_DURATION, (int) mp.getDuration());
                        mLiveListener.onPlayEvent(EMLiveConstants.PLAY_EVT_PLAY_PREPARED, prepareBundle);

                        Bundle bundle = new Bundle();
                        bundle.putLong(EMLiveConstants.EVT_TIME, System.currentTimeMillis());
                        bundle.putString(EMLiveConstants.EVT_DESCRIPTION, "play begin");
                        mLiveListener.onPlayEvent(EMLiveConstants.PLAY_EVT_PLAY_BEGIN, bundle);
                    }

                    if (mConfig.mAudioFocusDetect){
                        mAudioManager.requestAudioFocus(mAudioFocusListener, AudioManager.STREAM_MUSIC, AudioManager.AUDIOFOCUS_GAIN);
                        mAudioManager.requestAudioFocus(mAudioFocusListener, AudioManager.STREAM_RING, AudioManager.AUDIOFOCUS_GAIN);
                        mAudioManager.requestAudioFocus(mAudioFocusListener, AudioManager.STREAM_VOICE_CALL, AudioManager.AUDIOFOCUS_GAIN);
                    }
                }
            });
            ((IjkMediaPlayer) mediaPlayer).setPlayType(playType);
            //Log.e(TAG, "test");
            if (mEnablehardAcc) {
                ((IjkMediaPlayer) mediaPlayer).enableMediaDecoder(true);
            }else {
                ((IjkMediaPlayer) mediaPlayer).enableMediaDecoder(false);
            }
            applyPlayConfig();
            boolean ret = mImageContext._startBufferSource(mEnablehardAcc ? MLImageBufferSource.ML_IMAGE_BUFFER_INPUT_ANDROID_SurfaceTexture : MLImageBufferSource.ML_IMAGE_BUFFER_INPUT_TYPE_RGBA32, 0, 0);
            if (!ret) {
                return -1;
            }
            mBufferSource = (MLImageBufferSource) mImageContext.getNativeCameraObject();
            if (mBufferSource != null) {
                mBufferSource.setListener(mRenderListener);
            }
            if (mEnablehardAcc) {
                this.mediaPlayer.setSurface(mBufferSource.createInputSurface());
            }
            if (mRenderView != null) {
                mRenderView.bindToMLImageContext(mImageContext);
            }
            this.mediaPlayer.setOnBufferingUpdateListener(mOnBufferingUpdateListener);
            this.mediaPlayer.setOnVideoSizeChangedListener(mOnVideoSizeChangedListener);
            this.mediaPlayer.setOnCompletionListener(mOncompletionListener);
            this.mediaPlayer.setOnErrorListener(mOnErrorListener);
            this.mediaPlayer.setOnInfoListener(mOnInfoListener);
            this.mediaPlayer.setOnSeekCompleteListener(mOnSeekCompleteListener);
            this.mediaPlayer.setOnPlayProgressListener(mOnVideoPlayProgressListener);
            this.mediaPlayer.setOnPlayStreamUnixTimeListener(mOnVideoPlayStreamUnixTimeListener);
            ((IjkMediaPlayer)mediaPlayer).setMediaPlayerPresentBufferListener(mediaPlayerPresentBufferListener);
            if (bMulti) {
                ((IjkMediaPlayer)this.mediaPlayer).setMultiDataSource(url);
            } else {
                this.mediaPlayer.setDataSource(url);
            }
            this.mediaPlayer.setAudioStreamType(AudioManager.STREAM_MUSIC);
            this.mediaPlayer.setScreenOnWhilePlaying(true);
            mediaPlayer.prepareAsync();
            ((IjkMediaPlayer)mediaPlayer)._setMute(mMute ? 1 : 0);
        } catch (IOException e) {
            e.printStackTrace();
        }
        return 0;
    }


    private int startPlayNetAudio(int playType, final String url){
        if (mState != PLAY_STATE_NOT_INIT) {
            Log.e(TAG, "started play now.");
            return -1;
        }
        if (playType == PLAY_TYPE_NET_AUDIO){
            this.mediaPlayer = new AndroidMediaPlayer();
            Log.i(TAG, "use mediaplayer play audio.");
        }
        else if(playType == PLAY_TYPE_NET_EXAUDIO){
            this.mediaPlayer = new ExoMediaPlayer();
            Log.i(TAG, "use exoplayer play audio.");
        }else{
            Log.e(TAG, " play type error.");
            return -1;
        }

        mState  = PLAY_STATE_STARTING;
        mPaused = false;
        mFirstFrameRendered = false;
        mPlayStatictis = false;

        try {
            this.mediaPlayer.setOnPreparedListener(new IMediaPlayer.OnPreparedListener() {
                @Override
                public void onPrepared(IMediaPlayer mp) {
                    Bundle bundle = new Bundle();
                    bundle.putLong(EMLiveConstants.EVT_TIME, System.currentTimeMillis());
                    bundle.putString(EMLiveConstants.EVT_DESCRIPTION, "play begin");

                    if (!mPlayStatictis && mConfig != null && mConfig.mUpdateAVtatistic){
                        // ??????????????????
                        mPlayStatictis = true;
                    }

                    mState = PLAY_STATE_STRTED;

                    if (mPaused) {
                        mp.pause();
                    } else {
                        mp.start();
                    }
                    if (mLiveListener != null) {
                        mLiveListener.onPlayEvent(EMLiveConstants.PLAY_EVT_PLAY_BEGIN, bundle);
                    }

                    if (mConfig.mAudioFocusDetect){
                        mAudioManager.requestAudioFocus(mAudioFocusListener, AudioManager.STREAM_MUSIC, AudioManager.AUDIOFOCUS_GAIN);
                    }
                }
            });

            this.mediaPlayer.setOnBufferingUpdateListener(mOnBufferingUpdateListener);
            this.mediaPlayer.setOnVideoSizeChangedListener(mOnVideoSizeChangedListener);
            this.mediaPlayer.setOnCompletionListener(mOncompletionListener);
            this.mediaPlayer.setOnErrorListener(mOnErrorListener);
            this.mediaPlayer.setOnInfoListener(mOnInfoListener);
            this.mediaPlayer.setOnSeekCompleteListener(mOnSeekCompleteListener);
            this.mediaPlayer.setOnPlayProgressListener(mOnVideoPlayProgressListener);
            this.mediaPlayer.setOnPlayStreamUnixTimeListener(mOnVideoPlayStreamUnixTimeListener);
            this.mediaPlayer.setDataSource(mContext, Uri.parse(url));
           // this.mediaPlayer.setDataSource(url);
            this.mediaPlayer.setAudioStreamType(AudioManager.STREAM_MUSIC);
            this.mediaPlayer.setScreenOnWhilePlaying(true);
            this.mediaPlayer.prepareAsync();

        } catch (IOException e) {
            e.printStackTrace();
        }

        return 0;
    }

    /**
     * ??????????????????
     * @param addr ??????????????????????????????????????????????????????
     * @param playType ??????????????????????????? @EMLiveConstants?????????
     * @return
     */
    public int startPlay(String addr, int playType) {
        if (playType == PLAY_TYPE_NET_AUDIO || playType == PLAY_TYPE_NET_EXAUDIO){
            return startPlayNetAudio(playType, addr);
        }
        else{
            return startPlayInternal(addr, playType, false);
        }
    }

    /**
     * ??????????????????
     * @param clearLastFrame ???????????????????????????????????????
     * @return
     */
    public int stopPlay(boolean clearLastFrame)
    {
        if (mState != PLAY_STATE_STOPPING && mState != PLAY_STATE_NOT_INIT) {
            mState = PLAY_STATE_STOPPING;
            synchronized (this) {
                if (mNetInfoTimer != null) {
                    mNetInfoTimer.cancel();
                    mNetInfoTimer = null;
                }
            }

            synchronized (mReleaseObj){
                if (mImageContext != null) {
                    mBufferSource = null;
                    mImageContext._stopProcess(clearLastFrame);
                }
            }

            if (mAudioFocusListener != null && mConfig.mAudioFocusDetect){
                mAudioManager.abandonAudioFocus(mAudioFocusListener);
                mAudioFocusListener = null;
            }

            if (this.mediaPlayer != null) {
                final IMediaPlayer player = this.mediaPlayer;
                player.setOnBufferingUpdateListener(null);
                player.setOnPlayStreamUnixTimeListener(null);
                player.setOnCompletionListener(null);
                player.setOnVideoSizeChangedListener(null);
                player.setOnErrorListener(null);
                player.setOnInfoListener(null);
                player.setOnPlayProgressListener(null);
                player.setOnPreparedListener(null);
                player.setOnSeekCompleteListener(null);

                if (player instanceof IjkMediaPlayer){
                    ((IjkMediaPlayer)mediaPlayer).setMediaPlayerPresentBufferListener(null);
                }

                /*this.mProcessQueue.runAsync(new ProcessQueue.excuteBlock() {
                    @Override
                    public void excute() {
                        if (player != null) {
                          //  player.stop();
                            player.release();
                        }
                    }
                });*/


                player.stop();
                player.release();

                this.mediaPlayer = null;
                mState = PLAY_STATE_NOT_INIT;
            }
            if (this.mRenderView != null) {
                if (clearLastFrame) {

                }
            }
        }
        return 0;
    }

    /**
     * clear view last frame
     */
    public void clearLastFrame(){
        if (mImageContext != null) {
            mImageContext._clearLastFrame();
        }
    }

    /**
     * capture player picture
     * @param width output image size width, <= 0 for orignal size
     * @param height output image size height, <= 0 for orignal size
     * @return bitmap, null for failed.
     */
    public Bitmap captureFrame(int width, int height) {
        if (mImageContext != null) {
            return mImageContext.captureViewPicture();
        }
        return null;
    }

    public void captureFrame(final IEMLiveTakePictureListener listener) {
        if (mImageContext != null && mProcessQueue != null && listener != null) {
            mProcessQueue.runAsync(new ProcessQueue.excuteBlock() {
                @Override
                public void excute() {
                    if (mImageContext != null) {
                        Bitmap bitmap = mImageContext.captureViewPicture();
                        if (bitmap != null) {
                            listener.onPictureTaked(bitmap, bitmap.getWidth(), bitmap.getHeight());
                            return;
                        }
                    }
                    listener.onPictureTaked(null, 0, 0);
                }
            });
        }
    }

    /**
     * just get picture from android api , not self jni/ndk api{@link #captureFrame(IEMLiveTakePictureListener)}
     * @param listener
     */
    public void captureFrameWithAndroidApi(final IEMLiveTakePictureListener listener) {
        if (mImageContext != null && mProcessQueue != null && listener != null) {
            mProcessQueue.runAsync(new ProcessQueue.excuteBlock() {
                @Override
                public void excute() {
                    if (mImageContext != null) {
                        Bitmap bitmap = mImageContext.captureViewPictureWithSysApi();
                        if(bitmap == null){
                            bitmap = mImageContext.captureViewPicture();
                        }
                        if (bitmap != null) {
                            listener.onPictureTaked(bitmap, bitmap.getWidth(), bitmap.getHeight());
                            return;
                        }
                    }
                    listener.onPictureTaked(null, 0, 0);
                }
            });
        }
    }

    /**
     * just directly get picture from android api  , not self jni/ndk api{@link #captureFrame(int, int)}
     * @return
     */
    public Bitmap captureFrameWithAndroidApi() {
        if (mImageContext != null) {
            Bitmap bitmap = mImageContext.captureViewPictureWithSysApi();
            if(bitmap == null){
                bitmap = mImageContext.captureViewPicture();
            }
            return bitmap;
        }
        return null;
    }

    /**
     * ??????????????????????????????
     * @return ?????????bool???????????????????????????
     */
    public boolean isPlaying() {
        if (this.mediaPlayer != null){
            return this.mediaPlayer.isPlaying();
        }
        return false;
    }

    public void pause() {
        if (mState == PLAY_STATE_STRTED || mState == PLAY_STATE_STOPPED) {
            if (this.mediaPlayer != null) {
                this.mediaPlayer.pause();
            }
            mPaused = true;
        } else if (mState == PLAY_STATE_STARTING) {
            mPaused = true;
        }
        mRecordUserResume = false;
    }

    /***
     * ??????????????????????????????????????????
     * @param url ???????????????????????????
     * @param seekToPosition ??????????????????0?????????????????????0?????????
     * @return 0???????????? ??????????????????
     */
    public int changeVideoSource(String url, int playType , int seekToPosition) {
        mPlayStartPosition = seekToPosition;
        if (mState != PLAY_STATE_NOT_INIT) {
            if (this.mediaPlayer != null) {
                mPaused = false;
                mRecordUserResume = false;
                mRotationDegree = 0;
                mFirstFrameRendered = false;
                mSeeking = false;
                return ((IjkMediaPlayer)this.mediaPlayer)._changeVideoSource(url, playType);
            }
        }
        return -1;
    }

    public int changeVideoSource(String url, int playType ) {
        return changeVideoSource(url,playType,0);
    }

    public void setLooping(boolean onOff) {
        if (mState != PLAY_STATE_NOT_INIT) {
            if (this.mediaPlayer != null) {
                this.mediaPlayer.setLooping(onOff);
            }
        }
    }

    /**
     * ????????????
     */
    public void resume() {
        if (mState == PLAY_STATE_STRTED || mState == PLAY_STATE_STOPPED) {
            if (this.mediaPlayer != null) {
                this.mediaPlayer.start();
            }
            mPaused = false;
        } else if (mState == PLAY_STATE_STARTING) {
            mPaused = false;
        }
        mRecordUserResume = true;
    }

    public void standby() {
        if (mState == PLAY_STATE_STRTED) {
            if (this.mediaPlayer != null) {
                ((IjkMediaPlayer)this.mediaPlayer).standby();
            }
            mPaused = true;
        }
        mRecordUserResume = false;
    }

    /**
     * ???????????????????????????????????????????????????
     * @param offsetms ????????????????????????????????????????????????
     */
    public void seek(int offsetms) {
        if (mState == PLAY_STATE_STRTED || mState == PLAY_STATE_STOPPED) {
            if (this.mediaPlayer != null) {
                this.mediaPlayer.seekTo(offsetms);
                //fix: flv???mp4???????????????????????????complate??????
                //??????: 2018-12-20
                //mSeeking = true;
            }
        }
    }

    /**
     *get????????????
     *
     * @return
     */
    public long getCurrentPosition(){
        if (mediaPlayer == null) {
            return 0;
        }
        return mediaPlayer.getCurrentPosition();
    }
    public void setPlaybackRate(float rate){
        if (this.mediaPlayer != null){

            if (this.mediaPlayer instanceof IjkMediaPlayer){
                IjkMediaPlayer mp = (IjkMediaPlayer)this.mediaPlayer;
                mp._setPlaybackRate(rate);
            }
            else if (this.mediaPlayer instanceof AndroidMediaPlayer){
                AndroidMediaPlayer amp = (AndroidMediaPlayer)this.mediaPlayer;
                amp.setPlaybackRate(rate);
            }
            else if (this.mediaPlayer instanceof ExoMediaPlayer){
                ExoMediaPlayer emp = (ExoMediaPlayer)this.mediaPlayer;
                emp.setPlaybackRate(rate);
            }
        }
    }

    /**
     * ??????????????????????????????????????????????????????
     */
    public void destroy() {
        if (mediaPlayer != null) {
            stopPlay(true);
        }
        if (mImageContext != null) {
            mImageContext.destroy();
            mImageContext = null;
        }
        if (mProcessQueue != null) {
            mProcessQueue.Stop(false);
            mProcessQueue = null;
        }
        mRenderView = null;
        mLiveListener = null;
        mDataCallback = null;
        mContext = null;
        mConfig = null;
/*        mNetBundleLock.lock();
        try {
            mNetBundle = null;
        } finally {
            mNetBundleLock.unlock();
        }
*/
    }

    /**
     * ??????????????????????????????????????????
     * @param listener
     */
    public void setPlayListener(IEMLivePlayListener listener) {
        this.mLiveListener = listener;
    }

    /**
     * ???????????????????????????????????????????????????
     * @param renderMode
     */
    public void setRenderMode(int renderMode) {
        if (mRenderView != null) {
            mRenderView.setRenderMode(renderMode);
        }
        mRenderMode = renderMode;
    }

    public void setPlayAudioChannelMode(int mode) {
        if (mediaPlayer != null) {
            ((IjkMediaPlayer)mediaPlayer)._setPlayAudioChannelMode(mode);
        }
    }

    /**
     * ????????????????????????????????????????????????????????????????????????
     * @param rotation
     */
    public void setRenderRotation(int rotation) {

    }

    /**
     * ???????????????????????????
     * @param onoff ????????????
     * @return ???????????????????????????????????????true,??????????????????????????????????????????
     */
    public boolean enableHardwareDecode(boolean onoff) {
        mEnablehardAcc = onoff;
        return true;
    }

    /**
     * ?????????????????????????????????????????????????????????????????????
     * @param onff ??????
     */
    public void setMute(boolean onff) {
        if (mediaPlayer != null) {
            ((IjkMediaPlayer)mediaPlayer)._setMute(onff ? 1 : 0);
        }
        mMute = onff;
    }

    /**
     * ??????????????????log level?????????sdk??????log??????????????????????????????
     * @param logLevel
     */
    public void setLogLevel(int logLevel) {

    }

    /**
     * ?????????PCM???????????????????????????????????????????????????sdk??????
     * @param data ??????
     * @param length ??????
     * @param sr ?????????
     * @param channels ??????
     * @param ptsms ?????????
     */
    protected void onPcmData(byte[] data, int length, int sr, int channels, long ptsms) {
        if(mDataCallback != null){
            mDataCallback.onPcmData(data,  length,  sr,  channels,  ptsms);
        }
    }

    /**
     * ?????????YUV???????????????????????????????????????????????????????????????sdk??????
     * @param data
     * @param frameType
     * @param gopIndex
     * @param frameIndex
     * @param frameAngle
     * @param timeStamp
     */
    public void onVideoData(byte[] data, int frameType, int gopIndex, int frameIndex, int frameAngle, long timeStamp) {

    }

    /**
     * ??????????????????log?????????????????????????????????????????????????????????????????????
     * @param logstr
     */
    public void onLogRecord(String logstr) {

    }

    /**
     * ???????????????????????????????????????
     * @return yes or no
     */
    private boolean isAVCDecBlacklistDevices() {
        return Build.MANUFACTURER.equalsIgnoreCase("HUAWEI") && Build.MODEL.equalsIgnoreCase("Che2-TL00");
    }

    /**
     * ???????????????????????????????????????
     */
    private IMediaPlayer.OnCompletionListener mOncompletionListener = new IMediaPlayer.OnCompletionListener() {
        @Override
        public void onCompletion(IMediaPlayer mp) {
            mState = PLAY_STATE_STOPPED;
            Bundle bundle = new Bundle();
            bundle.putLong(EMLiveConstants.EVT_TIME, System.currentTimeMillis());
            bundle.putString(EMLiveConstants.EVT_DESCRIPTION, "play complete");
            if (mLiveListener != null) {
                mLiveListener.onPlayEvent(EMLiveConstants.PLAY_EVT_PLAY_END, bundle);
            }
        }
    };

    /**
     * ??????????????????????????????????????????
     */
    private IMediaPlayer.OnBufferingUpdateListener mOnBufferingUpdateListener = new IMediaPlayer.OnBufferingUpdateListener() {
        @Override
        public void onBufferingUpdate(IMediaPlayer mp, int percent) {
           // Log.e(TAG, "on buffering update: "+ percent);
        }
    };

    /**
     * ??????????????????????????????????????????
     */
    private IMediaPlayer.OnSeekCompleteListener mOnSeekCompleteListener = new IMediaPlayer.OnSeekCompleteListener() {
        @Override
        public void onSeekComplete(IMediaPlayer mp) {
            Log.e(TAG, "on seek complete");
            mSeeking = false;
            mState = PLAY_STATE_STRTED;

            mHandler.sendEmptyMessage(MSG_PLAY_SEEK_COMPLETE);
        }
    };

    /**
     * ??????????????????????????????????????????
     */
    private IMediaPlayer.OnVideoSizeChangedListener mOnVideoSizeChangedListener = new IMediaPlayer.OnVideoSizeChangedListener() {
        @Override
        public void onVideoSizeChanged(IMediaPlayer mp, int width, int height, int sar_num, int sar_den) {
            mVideoHeight = height;
            mVideoWidth = width;
            if (mBufferSource != null) {
                mBufferSource.bufferSourceSizeChanged(width, height);
            }
            mNetBundleLock.lock();
            try {
                //mNetBundle.putInt(EMLiveConstants.NET_STATUS_VIDEO_WIDTH, mVideoWidth);
                //mNetBundle.putInt(EMLiveConstants.NET_STATUS_VIDEO_HEIGHT, mVideoHeight);
                mNetBundle.putString(EMLiveConstants.NET_STATUS_CPU_USAGE, "20");
            } finally {
                mNetBundleLock.unlock();
            }
        }
    };

    /**
     * ??????????????????????????????????????????
     */
    private IMediaPlayer.OnErrorListener mOnErrorListener = new IMediaPlayer.OnErrorListener() {
        @Override
        public boolean onError(IMediaPlayer mp, int what, int extra) {
            Log.e(TAG, "error mssage:"+what+"extra"+extra);
            final Bundle bundle = new Bundle();
            bundle.putLong(EMLiveConstants.EVT_TIME, System.currentTimeMillis());
            switch (what) {
                case IMediaPlayer.MEDIA_ERROR_IO:
                case IMediaPlayer.MEDIA_ERROR_MALFORMED:
                case IMediaPlayer.MEDIA_ERROR_NOT_VALID_FOR_PROGRESSIVE_PLAYBACK:
                case IMediaPlayer.MEDIA_ERROR_SERVER_DIED:
                case IMediaPlayer.MEDIA_ERROR_TIMED_OUT:
                case IMediaPlayer.MEDIA_ERROR_UNKNOWN:
                case IMediaPlayer.MEDIA_ERROR_UNSUPPORTED:
                    mState = PLAY_STATE_STOPPED;
                    break;
                case IMediaPlayer.MEDIA_INFO_EXIT_READ:
                    mState = PLAY_STATE_STOPPED;
                    mLiveListener.onPlayEvent(EMLiveConstants.PLAY_EVT_PLAY_EXIT, bundle);
                    break;
                case IMediaPlayer.MEDIA_ERROR_NETWORK_DISCONNECT:
                    if (mLiveListener != null) {
                        if (extra == -403) {
                            bundle.putString(EMLiveConstants.EVT_DESCRIPTION, "network forbidden");
                            mLiveListener.onPlayEvent(EMLiveConstants.PLAY_ERR_NET_FORBIDDEN, bundle);
                        }
                        else {
                            bundle.putString(EMLiveConstants.EVT_DESCRIPTION, "network disconnect");
                            mLiveListener.onPlayEvent(EMLiveConstants.PLAY_ERR_NET_DISCONNECT, bundle);
                        }
                    }
                    //mHandler.removeMessages(MSG_UPDATE_INFO);
                    synchronized (EMLivePlayer2.this) {
                        if (mNetInfoTimer != null) {
                            mNetInfoTimer.cancel();
                            mNetInfoTimer = null;
                        }
                    }
                    mState = PLAY_STATE_STOPPED;
                    break;
            }
            return false;
        }
    };

    /**
     * ??????????????????????????????????????????
     */
    private IMediaPlayer.OnInfoListener mOnInfoListener = new IMediaPlayer.OnInfoListener() {
        @Override
        public boolean onInfo(IMediaPlayer mp, final int what, int extra) {
            Bundle bundle = new Bundle();
            switch (what) {
                case IMediaPlayer.MEDIA_INFO_UNKNOWN:
                    break;
                case IMediaPlayer.MEDIA_INFO_STARTED_AS_NEXT:
                    break;
                case IMediaPlayer.MEDIA_INFO_VIDEO_RENDERING_START:
                    if(mConfig != null && !mConfig.mAutoPlay && mConfig.mViewFirstFrameOnPrepare){
                        setMute(mMute);
                        if(mRecordUserResume){
                            resume();
                        }
                        Log.i(TAG, "restore user handle real mute:" + mMute +  " real resume:" + mRecordUserResume);
                    }
                    break;
                case IMediaPlayer.MEDIA_INFO_VIDEO_FIRST_I_FRAME_DECODED:
                    break;
                case IMediaPlayer.MEDIA_INFO_NETWORK_RECONNECT:
                    mLiveListener.onPlayEvent(EMLiveConstants.PLAY_WARNING_RECONNECT, bundle);
                    break;
                case IMediaPlayer.MEDIA_INFO_VIDEO_SOURCE_CHANGED:
                    mState = PLAY_STATE_STRTED;
                    if (mp != null && !mPaused) {
                        mp.start();
                    }
                    break;
                case IMediaPlayer.MEDIA_INFO_VIDEO_ROTATION_CHANGED:
                    mRotationDegree = (extra % 360) / 90;
                    if (mRenderView != null) {
                        mRenderView.setRenderRotation(mRotationDegree);
                    }
                    break;
                case IMediaPlayer.MEDIA_INFO_BUFFERING_START:
                    bundle.putString(EMLiveConstants.EVT_DESCRIPTION, "loading");
                    if (mLiveListener != null) {
                        mLiveListener.onPlayEvent(EMLiveConstants.PLAY_EVT_PLAY_LOADING, bundle);
                    }
                    break;
                case IMediaPlayer.MEDIA_INFO_BUFFERING_END:
                    bundle.putString(EMLiveConstants.EVT_DESCRIPTION, "play begin");
                    if (mLiveListener != null) {
                        mLiveListener.onPlayEvent(EMLiveConstants.PLAY_EVT_PLAY_BEGIN, bundle);
                    }

                    break;
                case IMediaPlayer.MEDIA_INFO_NETWORK_BANDWIDTH:
                    break;
                case IMediaPlayer.MEDIA_INFO_BAD_INTERLEAVING:
                    break;
                case IMediaPlayer.MEDIA_INFO_NOT_SEEKABLE:
                    break;
                case IMediaPlayer.MEDIA_INFO_METADATA_UPDATE:
                    break;
                case IMediaPlayer.MEDIA_INFO_TIMED_TEXT_ERROR:
                    break;
                case IMediaPlayer.MEDIA_INFO_UNSUPPORTED_SUBTITLE:
                    break;
                case IMediaPlayer.MEDIA_INFO_SUBTITLE_TIMED_OUT:
                    break;
                case IMediaPlayer.MEDIA_INFO_AUDIO_RENDERING_START:
                    break;
                case IMediaPlayer.MEDIA_INFO_SERVER_IP_CHANGED:
                    mServerIp = extra;
                    break;
                default:
                    break;
            }
            return false;
        }
    };

    private IMediaPlayer.OnVideoPlayProgressListener mOnVideoPlayProgressListener = new IMediaPlayer.OnVideoPlayProgressListener() {
        @Override
        public void onVideoPlayProgress(IMediaPlayer iMediaPlayer, int i, int i1) {
            //Log.e(TAG, mNetBundle.toString());
            if (mLiveListener != null && !mSeeking) {
                Bundle bundle = new Bundle();
                bundle.putLong(EMLiveConstants.EVT_TIME, System.currentTimeMillis());
                bundle.putString(EMLiveConstants.EVT_DESCRIPTION, "play grogress");
                bundle.putInt(EMLiveConstants.EVT_PLAY_DURATION, ((int) i1));
                bundle.putInt(EMLiveConstants.EVT_PLAY_PROGRESS, ((int) i));
                mLiveListener.onPlayEvent(EMLiveConstants.PLAY_EVT_PLAY_PROGRESS, bundle);
            }
        }
    };

    private IMediaPlayer.OnVideoPlayStreamUnixTimeListener mOnVideoPlayStreamUnixTimeListener = new IMediaPlayer.OnVideoPlayStreamUnixTimeListener() {
        @Override
        public void onVideoPlayStreamUnixTime(IMediaPlayer iMediaPlayer, long l) {
            if (mLiveListener != null) {
                Bundle bundle = new Bundle();
                bundle.putLong(EMLiveConstants.EVT_TIME, System.currentTimeMillis());
                bundle.putString(EMLiveConstants.EVT_DESCRIPTION, "play stream unix time");
                bundle.putLong(EMLiveConstants.EVT_PLAY_STREAM_UNIX_TIME, l);
                mLiveListener.onPlayEvent(EMLiveConstants.PLAY_EVT_PLAY_STREAM_UNIX_TIME, bundle);
            }
        }
    };

    private IMediaPlayerPresentBufferListener mediaPlayerPresentBufferListener = new IMediaPlayerPresentBufferListener() {
        @Override
        public void onDrawFrameBuffer(int i, byte[] bytes, int i1, int i2) {
            if(mEnablehardAcc){
                synchronized (mReleaseObj) {
                    Log.w(TAG, "start hardAcc failed, switch to soft decode!!");
                    mEnablehardAcc = false;
                    mBufferSource = null;
                    if (mImageContext != null) {
                        mImageContext._stopProcess(false);
                        boolean ret = mImageContext._startBufferSource(MLImageBufferSource.ML_IMAGE_BUFFER_INPUT_TYPE_RGBA32, 0, 0);
                        if (!ret) {
                            mHandler.sendEmptyMessage(MSG_TYPE_HW_DECODE_VIDEO_FAILED);
                            return;
                        }
                        mBufferSource = (MLImageBufferSource) mImageContext.getNativeCameraObject();
                        if (mBufferSource != null) {
                            mBufferSource.setListener(mRenderListener);
                            mHandler.sendEmptyMessage(MSG_TYPE_HW_DECODE_2_SOFT_DECODE);
                        }
                        else{
                            mHandler.sendEmptyMessage(MSG_TYPE_HW_DECODE_VIDEO_FAILED);
                        }
                    }
                }
            }

            synchronized (mReleaseObj){
                if (mBufferSource != null) {
                    mBufferSource.feedInputBuffer(bytes, 0, bytes.length, i1, i2, 0);
                }
            }
        }

        @Override
        public void onPostAudioFrameBuffer(byte[] bytes, int length, int ptsms, int sr, int channels) {
            onPcmData(bytes, length, sr, channels, ptsms);
        }
    };

    private MLImageBufferSource.IMLImageBufferSourceListener mRenderListener = new MLImageBufferSource.IMLImageBufferSourceListener() {
        @Override
        public void oneFrameRendered() {
            if (mHandler != null && mRenderView != null && mRenderView.viewCreated() && !mFirstFrameRendered) {
                mHandler.sendEmptyMessage(MSG_RECV_FIRST_I_FRAME);
                mFirstFrameRendered = true;
            }
        }
    };

    private String executeTop() {
        java.lang.Process p = null;
        BufferedReader in = null;
        String returnString = null;
        try {
            p = Runtime.getRuntime().exec("top -n 1");
            in = new BufferedReader(new InputStreamReader(p.getInputStream()));
            while (returnString == null || returnString.contentEquals("")) {
                returnString = in.readLine();
            }
        } catch (IOException e) {
            Log.e("executeTop", "error in getting first line of top");
            e.printStackTrace();
        } finally {
            try {
                in.close();
                p.destroy();
            } catch (IOException e) {
                Log.e("executeTop",
                        "error in closing and destroying top process");
                e.printStackTrace();
            }
        }
        return returnString;
    }

    private static String ipN2A(int ip) {
        String ret = "";
        ret += String.format("%d.", (ip >>> 24) & 0xFF);
        ret += String.format("%d.", (ip >>> 16) & 0xFF);
        ret += String.format("%d.", (ip >>> 8) & 0xFF);
        ret += String.format("%d", ip & 0xFF);
        return ret;
    }

    /**
     * ??????????????????????????????????????????
     */
    private Handler mHandler = new Handler(Looper.getMainLooper()) {
        @Override
        public void handleMessage(Message msg) {
            final Bundle bundle = new Bundle();
            switch (msg.what) {
                case MSG_UPDATE_INFO:
                    if (mediaPlayer != null && !mSeeking) {
                        bundle.putLong(EMLiveConstants.EVT_TIME, System.currentTimeMillis());
                        bundle.putString(EMLiveConstants.EVT_DESCRIPTION, "play grogress");
                        bundle.putInt(EMLiveConstants.EVT_PLAY_DURATION, ((int) mediaPlayer.getDuration() / 1000));
                        bundle.putInt(EMLiveConstants.EVT_PLAY_PROGRESS, ((int) mediaPlayer.getCurrentPosition() / 1000));
                        //Log.e(TAG, mNetBundle.toString());
                        if (mLiveListener != null) {
                            mLiveListener.onPlayEvent(EMLiveConstants.PLAY_EVT_PLAY_PROGRESS, bundle);
                        }
                    }
                    break;
                case MSG_RECV_FIRST_I_FRAME:
                    bundle.putString(EMLiveConstants.EVT_DESCRIPTION, "video first i frame");
//                    Log.d("lxs2020" , "mPlayStartPosition = "+mPlayStartPosition);
                    if (mPlayStartPosition > 0) {
                        seek(mPlayStartPosition);
                        mPlayStartPosition = 0;
                    }
                    if (mLiveListener != null) {
                        mLiveListener.onPlayEvent(EMLiveConstants.PLAY_EVT_RCV_FIRST_I_FRAME, bundle);
                    }
                    break;
                case MSG_REFRESH_NET_STATUS:
                    mNetBundleLock.lock();
                    try {
                        if (mLiveListener != null) {
                            mLiveListener.onNetStatus((Bundle) mNetBundle.clone());
                        }
                    } finally {
                        mNetBundleLock.unlock();
                    }
                    break;
                case MSG_AUDIO_DEVICE_FOCUS:
                    if (mLiveListener != null){
                        if (msg.arg1 == AudioManager.AUDIOFOCUS_GAIN)
                            mLiveListener.onPlayEvent(EMLiveConstants.PLAY_EVT_PLAY_AUDIODEVICE_GAIN_FOCUS, bundle);
                        else if (msg.arg1 == AudioManager.AUDIOFOCUS_LOSS)
                            mLiveListener.onPlayEvent(EMLiveConstants.PLAY_EVT_PLAY_AUDIODEVICE_LOSS_FOCUS, bundle);
                    }
                    break;
                case MSG_GET_PLAYURLS_TIMEOUT:
                    if (mLiveListener != null){
                        bundle.putString(EMLiveConstants.EVT_DESCRIPTION, "get play urls time out!");
                        mLiveListener.onPlayEvent(EMLiveConstants.PLAY_ERR_GETPLAYURL_TIMEOUT, bundle);
                    }
                    break;
                case MSG_PLAYURLS_EMPTY:
                    if (mLiveListener != null){
                        bundle.putString(EMLiveConstants.EVT_DESCRIPTION, "play urls empty!");
                        mLiveListener.onPlayEvent(EMLiveConstants.PLAY_ERR_PLAYURLS_EMPTY, bundle);
                    }
                    break;
                case MSG_PLAY_SEEK_COMPLETE:
                    if (mLiveListener != null){
                        bundle.putString(EMLiveConstants.EVT_DESCRIPTION, "seek complete!");
                        mLiveListener.onPlayEvent(EMLiveConstants.PLAY_EVT_PLAY_SEEK_COMPLETE, bundle);
                    }
                    break;
                case MSG_PLAY_LIVE_END:
                    {
                        if (mLiveListener != null){
                            bundle.putString(EMLiveConstants.EVT_DESCRIPTION, "live play complete!");
                            mLiveListener.onPlayEvent(EMLiveConstants.PLAY_EVT_PLAY_END, bundle);
                        }
                    }
                    break;
                case MSG_TYPE_HW_DECODE_VIDEO_FAILED:
                    if (mLiveListener != null){
                        mLiveListener.onPlayEvent(EMLiveConstants.PLAY_WARNING_VIDEO_DECODE_FAIL, bundle);
                    }
                    break;
                case MSG_TYPE_HW_DECODE_2_SOFT_DECODE:
                    if (mLiveListener != null){
                        mLiveListener.onPlayEvent(EMLiveConstants.PLAY_WARNING_SWITCH_SOFT_DECODE, bundle);
                    }
                    break;
            }
            //mHandler.removeMessages(MSG_UPDATE_INFO);
            //mHandler.sendEmptyMessageDelayed(MSG_UPDATE_INFO, 300);
        }
    };

    public void setDataCallback(DataCallback dataCallback) {
        this.mDataCallback = dataCallback;
    }

    public interface DataCallback{
        /**
         * ?????????PCM??????
         * @param data ??????
         * @param length ??????
         * @param sr ?????????
         * @param channels ??????
         * @param ptsms ?????????
         */
        void onPcmData(byte[] data, int length, int sr, int channels, long ptsms);
        /**
         * ?????????????????????
         * @param data ????????????
         * @param offset ??????offset???
         * @param length ???offset?????????????????????
         * @param width ??????
         * @param height ??????
         * @param timestamp ?????????
         */
        void onFrameData(byte[] data, int offset, int length, int width, int height, long timestamp);
    }
}
