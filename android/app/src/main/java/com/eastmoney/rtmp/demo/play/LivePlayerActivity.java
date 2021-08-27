package com.eastmoney.rtmp.demo.play;

import android.app.Activity;
import android.content.Intent;
import android.content.pm.ActivityInfo;
import android.content.res.Configuration;
import android.graphics.Color;
import android.graphics.drawable.AnimationDrawable;
import android.os.Bundle;
import android.text.TextUtils;
import android.text.method.ScrollingMovementMethod;
import android.util.Log;
import android.view.View;
import android.view.View.OnClickListener;
import android.view.ViewGroup;
import android.widget.Button;
import android.widget.EditText;
import android.widget.ImageView;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.SeekBar;
import android.widget.TextView;
import android.widget.Toast;

import com.eastmoney.emlivesdkandroid.EMLiveConstants;
import com.eastmoney.emlivesdkandroid.EMLivePlayConfig;
import com.eastmoney.emlivesdkandroid.EMLivePlayer2;
import com.eastmoney.emlivesdkandroid.IEMLivePlayListener;
import com.eastmoney.emlivesdkandroid.ui.EMLiveVideoView2;
import com.eastmoney.rtmp.demo.R;
import com.eastmoney.rtmp.demo.common.activity.QRCodeScanActivity;

import java.text.SimpleDateFormat;

/**
 * 视频播放界面
 * <p>
 * <略>
 *
 * @author : company
 * @date : 2020/2/13
 */
public class LivePlayerActivity extends Activity implements IEMLivePlayListener, OnClickListener {
    private static final String TAG = LivePlayerActivity.class.getSimpleName();

    /**
     * 播放器
     */
    private EMLivePlayer2 mLivePlayer = null;
    private boolean mVideoPlay;
    /**
     * 渲染veiw
     */
    private EMLiveVideoView2 mPlayerView;
    private LinearLayout mLayout;
    private ImageView mLoadingView;
    private boolean mHWDecode = false;
    private LinearLayout mRootView;

    private Button mBtnLog;
    private Button mBtnPlay;
    private Button mBtnRenderRotation;
    private Button mBtnRenderMode;
    private Button mBtnHWDecode;
    private ScrollView mScrollView;
    private SeekBar mSeekBar;
    private TextView mTextDuration;
    private TextView mTextStart;

    public static final int ACTIVITY_TYPE_LIVE_PLAY = 2;
    public static final int ACTIVITY_TYPE_VOD_PLAY = 3;

    private Button mBtnStop;
    protected EditText mRtmpUrlView;
    public TextView mLogViewStatus;
    public TextView mLogViewEvent;
    protected StringBuffer mLogMsg = new StringBuffer("");
    private final int mLogMsgLenLimit = 3000;

    private int mCurrentRenderMode;
    private int mCurrentRenderRotation;

    private long mTrackingTouchTS = 0;
    private boolean mStartSeek = false;
    private boolean mVideoPause = false;
    private String mPlayUrl;
    private int mPlayType = EMLivePlayer2.PLAY_TYPE_LIVE_RTMP;
    private EMLivePlayConfig mPlayConfig;
    private long mStartPlayTS = 0;
    protected int mActivityType;

    float[] mPlaybackRate = new float[]{0.8f, 1.0f, 1.25f, 1.5f, 2.0f};

    private Activity act = null;

    @Override
    public void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        mCurrentRenderMode = EMLiveConstants.RENDER_MODE_FILL_PRESERVE_AR;
        mCurrentRenderRotation = EMLiveConstants.RENDER_ROTATION_PORTRAIT;

        mActivityType = ACTIVITY_TYPE_VOD_PLAY;

        mPlayConfig = new EMLivePlayConfig();

        setContentView();
        act = this;
        LinearLayout backLL = (LinearLayout) findViewById(R.id.back_ll);
        backLL.setOnClickListener(new OnClickListener() {
            @Override
            public void onClick(View v) {
                finish();
            }
        });
        TextView titleTV = (TextView) findViewById(R.id.title_tv);
        titleTV.setText(getIntent().getStringExtra("TITLE"));
    }

    /**
     * 初始化view
     */
    void initView() {
        mRtmpUrlView = (EditText) findViewById(R.id.rtmpUrl);
        mLogViewEvent = (TextView) findViewById(R.id.logViewEvent);
        mLogViewStatus = (TextView) findViewById(R.id.logViewStatus);

        Button scanBtn = (Button) findViewById(R.id.btnScan);
        scanBtn.setOnClickListener(new OnClickListener() {
            @Override
            public void onClick(View v) {
                Intent intent = new Intent(LivePlayerActivity.this, QRCodeScanActivity.class);
                startActivityForResult(intent, 100);
            }
        });
        scanBtn.setEnabled(true);
    }


    public static void scroll2Bottom(final ScrollView scroll, final View inner) {
        if (scroll == null || inner == null) {
            return;
        }
        int offset = inner.getMeasuredHeight() - scroll.getMeasuredHeight();
        if (offset < 0) {
            offset = 0;
        }
        scroll.scrollTo(0, offset);
    }

    public void setContentView() {
        super.setContentView(R.layout.activity_play);
        initView();

        mRootView = (LinearLayout) findViewById(R.id.root);
        if (mLivePlayer == null) {
            mLivePlayer = new EMLivePlayer2(this);
            mLivePlayer.setMute(false);
        }
        mLayout = (LinearLayout) findViewById(R.id.layout);
        mLayout.setOnClickListener(new OnClickListener() {
            private boolean mSwitch = false;

            @Override
            public void onClick(View v) {
                mSwitch = !mSwitch;
                mLayout.setLayerType(mSwitch ? View.LAYER_TYPE_HARDWARE : View.LAYER_TYPE_NONE, null);
            }
        });
        mPlayerView = (EMLiveVideoView2) findViewById(R.id.video_view);
        mPlayerView.enableRoundCorner(true, 10);
//        mPlayerView.disableLog(true);
        mLoadingView = (ImageView) findViewById(R.id.loadingImageView);

        mRtmpUrlView.setHint(" 请输入或扫二维码获取播放地址");
        mRtmpUrlView.setText("");

        mVideoPlay = false;
        mLogViewStatus.setVisibility(View.GONE);
        mLogViewStatus.setMovementMethod(new ScrollingMovementMethod());
        mLogViewEvent.setMovementMethod(new ScrollingMovementMethod());
        mScrollView = (ScrollView) findViewById(R.id.scrollview);
        mScrollView.setVisibility(View.GONE);

        mBtnPlay = (Button) findViewById(R.id.btnPlay);
        mBtnPlay.setOnClickListener(new OnClickListener() {
            @Override
            public void onClick(View v) {
                Log.d(TAG, "click playbtn isplay:" + mVideoPlay + " ispause:" + mVideoPause + " playtype:" + mPlayType);
                if (mVideoPlay) {
                    if (mPlayType == EMLivePlayer2.PLAY_TYPE_VOD_FLV
                            || mPlayType == EMLivePlayer2.PLAY_TYPE_VOD_HLS
                            || mPlayType == EMLivePlayer2.PLAY_TYPE_VOD_MP4
                            || mPlayType == EMLivePlayer2.PLAY_TYPE_NET_AUDIO
                            || mPlayType == EMLivePlayer2.PLAY_TYPE_NET_EXAUDIO) {
                        if (mVideoPause) {
                            mLivePlayer.resume();
                            mBtnPlay.setText("播放");
                            //mRootView.setBackgroundColor(0xff000000);
                        } else {
                            mLivePlayer.pause();
                            mBtnPlay.setText("暂停");
                        }
                        mVideoPause = !mVideoPause;

                    } else {
                        stopPlayRtmp();
                        mVideoPlay = !mVideoPlay;
                    }

                } else {
                    if (startPlayRtmp()) {
                        mVideoPlay = !mVideoPlay;
                    }
                }
            }
        });

        //停止按钮
        mBtnStop = (Button) findViewById(R.id.btnStop);
        mBtnStop.setOnClickListener(new OnClickListener() {
            @Override
            public void onClick(View v) {
                stopPlayRtmp();
                mVideoPlay = false;
                mVideoPause = false;
                if (mTextStart != null) {
                    mTextStart.setText("00:00");
                }
                if (mSeekBar != null) {
                    mSeekBar.setProgress(0);
                }
            }
        });

        mBtnLog = (Button) findViewById(R.id.btnLog);
        mBtnLog.setOnClickListener(new OnClickListener() {
            @Override
            public void onClick(View v) {
                if (mLogViewStatus.getVisibility() == View.GONE) {
                    mLogViewStatus.setVisibility(View.VISIBLE);
                    mScrollView.setVisibility(View.VISIBLE);
                    mLogViewEvent.setText(mLogMsg);
                    scroll2Bottom(mScrollView, mLogViewEvent);
                } else {
                    mLogViewStatus.setVisibility(View.GONE);
                    mScrollView.setVisibility(View.GONE);
                }
            }
        });

        //横屏|竖屏
        mBtnRenderRotation = (Button) findViewById(R.id.btnOrientation);
        mBtnRenderRotation.setOnClickListener(new OnClickListener() {
            @Override
            public void onClick(View v) {
                if (mPlayerView == null) {
                    return;
                }

                if (mCurrentRenderRotation == EMLiveConstants.RENDER_ROTATION_PORTRAIT) {
                    mBtnRenderRotation.setText("竖屏");
                    mCurrentRenderRotation = EMLiveConstants.RENDER_ROTATION_LANDSCAPE;
                } else if (mCurrentRenderRotation == EMLiveConstants.RENDER_ROTATION_LANDSCAPE) {
                    mBtnRenderRotation.setText("横屏");
                    mCurrentRenderRotation = EMLiveConstants.RENDER_ROTATION_PORTRAIT;
                }
                if (act != null)
                    act.setRequestedOrientation(mCurrentRenderRotation == 1 ? ActivityInfo.SCREEN_ORIENTATION_LANDSCAPE : ActivityInfo.SCREEN_ORIENTATION_PORTRAIT);
                //mPlayerView.setRenderRotation(mCurrentRenderRotation);
            }
        });

        //平铺模式
        mBtnRenderMode = (Button) findViewById(R.id.btnRenderMode);
        mBtnRenderMode.setOnClickListener(new OnClickListener() {
            @Override
            public void onClick(View v) {
                if (mLivePlayer == null) {
                    return;
                }

                if (mCurrentRenderMode == EMLiveConstants.RENDER_MODE_FILL_PRESERVE_AR_FILL) {
                    mLivePlayer.setRenderMode(EMLiveConstants.RENDER_MODE_FILL_PRESERVE_AR_BLUR_BG);
                    mCurrentRenderMode = EMLiveConstants.RENDER_MODE_FILL_PRESERVE_AR_BLUR_BG;
                } else if (mCurrentRenderMode == EMLiveConstants.RENDER_MODE_FILL_PRESERVE_AR_BLUR_BG) {
                    mLivePlayer.setRenderMode(EMLiveConstants.RENDER_MODE_FILL_PRESERVE_AR_FILL);
                    mCurrentRenderMode = EMLiveConstants.RENDER_MODE_FILL_PRESERVE_AR_FILL;
                }
            }
        });

        //硬件解码
        mBtnHWDecode = (Button) findViewById(R.id.btnHWDecode);
        mBtnHWDecode.getBackground().setAlpha(mHWDecode ? 255 : 100);
        mBtnHWDecode.setOnClickListener(new OnClickListener() {
            @Override
            public void onClick(View v) {
                mHWDecode = !mHWDecode;
                mBtnHWDecode.getBackground().setAlpha(mHWDecode ? 255 : 100);
                if (mHWDecode) {
                    Toast.makeText(getApplicationContext(), "已开启硬件解码加速，切换会重启播放流程!", Toast.LENGTH_SHORT).show();
                } else {
                    Toast.makeText(getApplicationContext(), "已关闭硬件解码加速，切换会重启播放流程!", Toast.LENGTH_SHORT).show();
                }

                if (mVideoPlay) {
                    stopPlayRtmp();
                    startPlayRtmp();
                    if (mVideoPause) {
                        mVideoPause = false;
                    }
                }
            }
        });

        mSeekBar = (SeekBar) findViewById(R.id.seekbar);
        mSeekBar.setOnSeekBarChangeListener(new SeekBar.OnSeekBarChangeListener() {
            @Override
            public void onProgressChanged(SeekBar seekBar, int progress, boolean bFromUser) {
                mTextStart.setText(String.format("%02d:%02d", progress / 60000, (progress / 1000) % 60));
            }

            @Override
            public void onStartTrackingTouch(SeekBar seekBar) {
                mStartSeek = true;
            }

            @Override
            public void onStopTrackingTouch(SeekBar seekBar) {
                if (mLivePlayer != null) {
                    mLivePlayer.seek(seekBar.getProgress());
                }
                mTrackingTouchTS = System.currentTimeMillis();
                mStartSeek = false;
            }
        });

        mTextDuration = (TextView) findViewById(R.id.duration);
        mTextStart = (TextView) findViewById(R.id.play_start);
        mTextDuration.setTextColor(Color.rgb(255, 255, 255));
        mTextStart.setTextColor(Color.rgb(255, 255, 255));

        findViewById(R.id.btnChangPlayRate).setTag(1);
        findViewById(R.id.btnChangPlayRate).setOnClickListener(new OnClickListener() {
            @Override
            public void onClick(View view) {
                if (mLivePlayer != null) {
                    Object tag = findViewById(R.id.btnChangPlayRate).getTag();
                    int index = (int) tag;
                    if (++index >= mPlaybackRate.length) {
                        index = 0;
                    }
                    mLivePlayer.setPlaybackRate(mPlaybackRate[index]);
                    ((Button) findViewById(R.id.btnChangPlayRate)).setText(String.valueOf(mPlaybackRate[index]));
                    findViewById(R.id.btnChangPlayRate).setTag(index);
                }
            }
        });

        View view = mPlayerView.getRootView();
        view.setOnClickListener(this);
    }

    @Override
    public void onDestroy() {
        super.onDestroy();
        if (mLivePlayer != null) {
            mLivePlayer.setPlayListener(null);
            mLivePlayer.setPlayerView(null);
            mLivePlayer.stopPlay(true);
            mLivePlayer.destroy();
            mLivePlayer = null;
        }
        if (mPlayerView != null) {
            mPlayerView = null;
        }
        mPlayConfig = null;
        Log.d(TAG, "vrender onDestroy");
    }

    @Override
    public void onPause() {
        super.onPause();
    }

    @Override
    public void onStop() {
        super.onStop();
        stopPlayRtmp();
    }

    @Override
    public void onResume() {
        super.onResume();
    }

    @Override
    public void onClick(View v) {
        switch (v.getId()) {
            default:
        }
    }

    private boolean checkPlayUrl(final String playUrl) {
        if (TextUtils.isEmpty(playUrl) || (!playUrl.startsWith("http://") && !playUrl.startsWith("https://") && !playUrl.startsWith("rtmp://") && !playUrl.startsWith("/"))) {
            Toast.makeText(getApplicationContext(), "播放地址不合法，目前仅支持rtmp,flv,hls,mp4播放方式和本地播放方式（绝对路径，如\"/sdcard/test.mp4\"）!", Toast.LENGTH_SHORT).show();
            return false;
        }

        mPlayUrl = playUrl;

        switch (mActivityType) {
            case ACTIVITY_TYPE_LIVE_PLAY: // TODO support HLS
            {
                if (playUrl.startsWith("rtmp://")) {
                    mPlayType = EMLivePlayer2.PLAY_TYPE_LIVE_RTMP;
                } else if ((playUrl.startsWith("http://") || playUrl.startsWith("https://")) && playUrl.contains(".flv")) {
                    mPlayType = EMLivePlayer2.PLAY_TYPE_LIVE_FLV;
                } else {
                    Toast.makeText(getApplicationContext(), "播放地址不合法，直播目前仅支持rtmp,flv播放方式!", Toast.LENGTH_SHORT).show();
                    return false;
                }
            }
            break;
            case ACTIVITY_TYPE_VOD_PLAY: // TODO support local vod
            {
                if (playUrl.startsWith("http://") || playUrl.startsWith("https://")) {
                    if (playUrl.contains(".flv")) {
                        mPlayType = EMLivePlayer2.PLAY_TYPE_VOD_FLV;
                    } else if (playUrl.contains(".m3u8")) {
                        mPlayType = EMLivePlayer2.PLAY_TYPE_VOD_HLS;
                    } else if (playUrl.toLowerCase().contains(".mp4")) {
                        mPlayType = EMLivePlayer2.PLAY_TYPE_VOD_MP4;
                    } else if (playUrl.toLowerCase().contains(".mp3") || playUrl.toLowerCase().contains(".wav")) {
                        mPlayType = EMLivePlayer2.PLAY_TYPE_NET_EXAUDIO;
                    } else {
                        Toast.makeText(getApplicationContext(), "播放地址不合法，点播目前仅支持flv,hls,mp4,mp3播放方式!", Toast.LENGTH_SHORT).show();
                    }
                } else if (playUrl.startsWith("/")) {
                    mPlayType = EMLivePlayer2.PLAY_TYPE_LOCAL_VIDEO;
                } else {
                    Toast.makeText(getApplicationContext(), "播放地址不合法，点播目前仅支持flv,hls,mp4播放方式!", Toast.LENGTH_SHORT).show();
                    return false;
                }
            }
            break;
            default:
                Toast.makeText(getApplicationContext(), "播放地址不合法，目前仅支持rtmp,flv,hls,mp4播放方式!", Toast.LENGTH_SHORT).show();
                return false;
        }
        return true;
    }

    protected void clearLog() {
        mLogMsg.setLength(0);
        mLogViewEvent.setText("");
        mLogViewStatus.setText("");
    }

    protected void appendEventLog(int event, String message) {
        String str = "receive event: " + event + ", " + message;
        Log.d(TAG, str);
        SimpleDateFormat sdf = new SimpleDateFormat("HH:mm:ss.SSS");
        String date = sdf.format(System.currentTimeMillis());
        while (mLogMsg.length() > mLogMsgLenLimit) {
            int idx = mLogMsg.indexOf("\n");
            if (idx == 0)
                idx = 1;
            mLogMsg = mLogMsg.delete(0, idx);
        }
        mLogMsg = mLogMsg.append("\n" + "[" + date + "]" + message);
    }

    protected void enableQRCodeBtn(boolean bEnable) {
        //disable qrcode
        Button btnScan = (Button) findViewById(R.id.btnScan);
        if (btnScan != null) {
            btnScan.setEnabled(bEnable);
        }
    }

    private boolean startPlayRtmp() {

        String playUrl = mRtmpUrlView.getText().toString();
        if (playUrl.length() < 1) {
            Toast.makeText(getApplicationContext(), "播放地址为空", Toast.LENGTH_SHORT).show();
            return false;
        }

        if (!checkPlayUrl(playUrl)) {
            return false;
        }

        clearLog();

        int[] ver = EMLivePlayer2.getSDKVersion();
        if (ver != null && ver.length >= 4) {
            mLogMsg.append(String.format("rtmp sdk version:%d.%d.%d.%d ", ver[0], ver[1], ver[2], ver[3]));
            mLogViewEvent.setText(mLogMsg);
        }
        mBtnPlay.setText("暂停");

        if (mLivePlayer != null) {
            mLivePlayer.setPlayListener(this);
            mLivePlayer.setPlayerView((ViewGroup) mPlayerView);
            // 硬件加速在1080p解码场景下效果显著，但细节之处并不如想象的那么美好：
            // (1) 只有 4.3 以上android系统才支持
            // (2) 兼容性我们目前还仅过了小米华为等常见机型，故这里的返回值您先不要太当真
            mLivePlayer.enableHardwareDecode(mHWDecode);
            mLivePlayer.setRenderRotation(mCurrentRenderRotation);
            mLivePlayer.setRenderMode(mCurrentRenderMode);
            mPlayConfig.setAudioFocusDetect(false);
            mPlayConfig.setConnectRetryCount(10);
            mPlayConfig.setConnectRetryInterval(3);
            mPlayConfig.setPlayAudioChannelMode(EMLiveConstants.PLAY_CHANNEL_CONFIG_LEFT);
            //  mPlayConfig.setDnsCacheCount(0);
            //  mPlayConfig.setDnsCacheValidTime(0);
            mLivePlayer.setConfig(mPlayConfig);
            if (mPlayType == EMLivePlayer2.PLAY_TYPE_LOCAL_VIDEO) {
                //mLivePlayer.setPlaybackRate(1.0f);
            }

            mStartPlayTS = System.nanoTime();
            mStartPlayTS = System.nanoTime();

            int result = mLivePlayer.startPlay(playUrl, mPlayType); // result返回值：0 success;  -1 empty url; -2 invalid url; -3 invalid playType;
            Log.i(TAG, "start play take time:" + (System.nanoTime() - mStartPlayTS) / 1000000L + "ms");
            if (result != 0) {
                mBtnPlay.setText("播放");
                //mRootView.setBackgroundResource(R.drawable.main_bkg);
                return false;
            }
        }

        appendEventLog(0, "点击播放按钮！播放类型：" + mPlayType);
        startLoadingAnimation();
        enableQRCodeBtn(false);
        return true;
    }

    private void stopPlayRtmp() {
        enableQRCodeBtn(true);
        mBtnPlay.setText("播放");
        stopLoadingAnimation();
        if (mLivePlayer != null) {
            //mLivePlayer.setPlayListener(null);
            mLivePlayer.stopPlay(true);
            //mLivePlayer.standby();
            mLivePlayer.setPlayerView(null);
            String playUrl = null;
        }
    }

    @Override
    public void onPlayEvent(int event, Bundle param) {
        if (event == EMLiveConstants.PLAY_EVT_PLAY_BEGIN) {
            Log.i(TAG, "recv play begin takes time:" + (System.nanoTime() - mStartPlayTS) / 1000000L + "ms");
            // mLivePlayer.setPlaybackRate(1.25f);
            stopLoadingAnimation();
            Log.d("AutoMonitor", "PlayFirstRender,cost=" + (System.currentTimeMillis() - mStartPlayTS));
        } else if (event == EMLiveConstants.PLAY_EVT_RCV_FIRST_I_FRAME) {
            Log.i(TAG, "recv first i frame takes time:" + (System.nanoTime() - mStartPlayTS) / 1000000L + "ms");
            Toast.makeText(this, "recv i first frame takes time:" + (System.nanoTime() - mStartPlayTS) / 1000000L + "ms", Toast.LENGTH_SHORT).show();
            ;
            mLivePlayer.setLooping(true);
        } else if (event == EMLiveConstants.PLAY_EVT_PLAY_PROGRESS) {
            if (mStartSeek) {
                return;
            }
            int progress = param.getInt(EMLiveConstants.EVT_PLAY_PROGRESS);
            int duration = param.getInt(EMLiveConstants.EVT_PLAY_DURATION);
            long curTS = System.currentTimeMillis();

            // 避免滑动进度条松开的瞬间可能出现滑动条瞬间跳到上一个位置
            if (Math.abs(curTS - mTrackingTouchTS) < 500) {
                //return;
            }
            mTrackingTouchTS = curTS;

            if (mSeekBar != null) {
                mSeekBar.setProgress(progress);
            }
            if (mTextStart != null) {
                mTextStart.setText(String.format("%02d:%02d", progress / 60000, (progress % 60000) / 1000));
            }
            if (mTextDuration != null) {
                mTextDuration.setText(String.format("%02d:%02d", duration / 60000, (duration % 60000) / 1000));
            }
            if (mSeekBar != null) {
                mSeekBar.setMax(duration);
            }
            return;
        } else if (event == EMLiveConstants.PLAY_ERR_NET_DISCONNECT
                || event == EMLiveConstants.PLAY_EVT_PLAY_END
                || event == EMLiveConstants.PLAY_ERR_GETPLAYURL_TIMEOUT
                || event == EMLiveConstants.PLAY_ERR_PLAYURLS_EMPTY) {
            stopPlayRtmp();
            mVideoPlay = false;
            mVideoPause = false;
            if (mTextStart != null) {
                mTextStart.setText("00:00");
            }
            if (mSeekBar != null) {
                mSeekBar.setProgress(0);
            }
        } else if (event == EMLiveConstants.PLAY_EVT_PLAY_LOADING) {
            startLoadingAnimation();
        } else if (event == EMLiveConstants.PLAY_EVT_PLAY_STREAM_UNIX_TIME) {
            Log.e(TAG, "unix time :" + param.getLong(EMLiveConstants.EVT_PLAY_STREAM_UNIX_TIME));
        } else if (event == EMLiveConstants.PLAY_EVT_PLAY_AUDIODEVICE_LOSS_FOCUS) {
            if (mLivePlayer != null) {
                mLivePlayer.pause();
            }

            Log.i(TAG, "PLAY_EVT_PLAY_AUDIODEVICE_LOSS_FOCUS");
        } else if (event == EMLiveConstants.PLAY_EVT_PLAY_AUDIODEVICE_GAIN_FOCUS) {
            if (mLivePlayer != null) {
                mLivePlayer.resume();
            }

            Log.i(TAG, "PLAY_EVT_PLAY_AUDIODEVICE_GAIN_FOCUS");
        }


        String msg = param.getString(EMLiveConstants.EVT_DESCRIPTION);
        appendEventLog(event, msg);
        if (mScrollView.getVisibility() == View.VISIBLE) {
            mLogViewEvent.setText(mLogMsg);
            scroll2Bottom(mScrollView, mLogViewEvent);
        }
        if (event < 0) {
            Toast.makeText(getApplicationContext(), param.getString(EMLiveConstants.EVT_DESCRIPTION), Toast.LENGTH_SHORT).show();
        } else if (event == EMLiveConstants.PLAY_EVT_PLAY_BEGIN) {
            stopLoadingAnimation();
        }
    }

    //公用打印辅助函数
    protected String getNetStatusString(Bundle status) {
        String str = String.format("%-14s %-14s %-12s\n%-14s %-14s %-12s\n%-14s %-14s %-12s\n%-14s %-12s",
                "CPU:" + status.getString(EMLiveConstants.NET_STATUS_CPU_USAGE),
                "RES:" + status.getInt(EMLiveConstants.NET_STATUS_VIDEO_WIDTH) + "*" + status.getInt(EMLiveConstants.NET_STATUS_VIDEO_HEIGHT),
                "SPD:" + status.getInt(EMLiveConstants.NET_STATUS_NET_SPEED) + "Kbps",
                "JIT:" + status.getInt(EMLiveConstants.NET_STATUS_NET_JITTER),
                "FPS:" + status.getInt(EMLiveConstants.NET_STATUS_VIDEO_FPS),
                "ARA:" + status.getInt(EMLiveConstants.NET_STATUS_AUDIO_BITRATE) + "Kbps",
                "QUE:" + status.getInt(EMLiveConstants.NET_STATUS_CODEC_CACHE) + "|" + status.getInt(EMLiveConstants.NET_STATUS_CACHE_SIZE),
                "DRP:" + status.getInt(EMLiveConstants.NET_STATUS_CODEC_DROP_CNT) + "|" + status.getInt(EMLiveConstants.NET_STATUS_DROP_SIZE),
                "VRA:" + status.getInt(EMLiveConstants.NET_STATUS_VIDEO_BITRATE) + "Kbps",
                "SVR:" + status.getString(EMLiveConstants.NET_STATUS_SERVER_IP),
                "AVRA:" + status.getInt(EMLiveConstants.NET_STATUS_VIDEO_BITRATE));
        return str;
    }

    @Override
    public void onNetStatus(Bundle status) {
        String str = getNetStatusString(status);
        mLogViewStatus.setText(str);
        Log.d(TAG, "Current status, CPU:" + status.getString(EMLiveConstants.NET_STATUS_CPU_USAGE) +
                ", RES:" + status.getInt(EMLiveConstants.NET_STATUS_VIDEO_WIDTH) + "*" + status.getInt(EMLiveConstants.NET_STATUS_VIDEO_HEIGHT) +
                ", SPD:" + status.getInt(EMLiveConstants.NET_STATUS_NET_SPEED) + "Kbps" +
                ", FPS:" + status.getInt(EMLiveConstants.NET_STATUS_VIDEO_FPS) +
                ", ARA:" + status.getInt(EMLiveConstants.NET_STATUS_AUDIO_BITRATE) + "Kbps" +
                ", VRA:" + status.getInt(EMLiveConstants.NET_STATUS_VIDEO_BITRATE) + "Kbps");
    }

    /**
     * 启动视频加载动画
     */
    private void startLoadingAnimation() {
        if (mLoadingView != null) {
            mLoadingView.setVisibility(View.VISIBLE);
            ((AnimationDrawable) mLoadingView.getDrawable()).start();
        }
    }

    /**
     * 停止视频加载动画
     */
    private void stopLoadingAnimation() {
        if (mLoadingView != null) {
            mLoadingView.setVisibility(View.GONE);
            ((AnimationDrawable) mLoadingView.getDrawable()).stop();
        }
    }

    @Override
    public void onActivityResult(int requestCode, int resultCode, Intent data) {
        if (requestCode != 100 || data == null || data.getExtras() == null || TextUtils.isEmpty(data.getExtras().getString("result"))) {
            return;
        }
        String result = data.getExtras().getString("result");
        if (mRtmpUrlView != null) {
            mRtmpUrlView.setText(result);
        }
    }

    @Override
    public void onConfigurationChanged(Configuration newConfig) {
        super.onConfigurationChanged(newConfig);
    }
}
