package com.eastmoney.emlivesdkandroid;

import android.media.AudioManager;

import static com.eastmoney.emlivesdkandroid.EMLiveConstants.EM_PLAY_LOADING_STARTEGY_DEFAULT;

/**
 * Created by eastmoney on 16/10/21.
 */
public class EMLivePlayConfig {
    int mCacheTime = 5;
    int mMaxAutoAdjustCacheTime = 5;
    int mMinAutoAdjustCacheTime = 1;
    int mConnectRetryCount = 4;
    int mConnectRetryInterval = 3;
    boolean mAutoAdjustCacheTime = true;
    boolean mAudioFocusDetect = true;
    boolean mAutoPlay = true;
    int mPlayAudioChannelMode = 0;
    int mAudioStreamType = AudioManager.STREAM_MUSIC;
    boolean mUpdateAVtatistic = true;

    //播放加载速度默认为EM_PLAY_LOADING_STARTEGY_DEFAULT
    int mLoadingStrategy = EM_PLAY_LOADING_STARTEGY_DEFAULT;
    // 用户自定义加载策略设置的值，单位KB
    int mCustomLoadingValue = 300;
    // dns缓存上限，如果设置0，则不启用dns cache功能 默认100
    int mDnsCacheCount = 100;
    // dnscache中dns 有效时间，超过这个时间会自动更新dns，单位:s 默认300(5min)
    int mDnsCacheValidTime = 300;
    boolean mViewFirstFrameOnPrepare = false;

    public EMLivePlayConfig() {
    }

    /**
     * 设置播放的缓存时间
     * @param cacheTime 缓存时间，单位毫秒
     */
    public void setCacheTime(int cacheTime) {
        this.mCacheTime = cacheTime;
    }

    /**
     * 设置是否自动处理播放缓存
     * @param onff 开关
     */
    public void setAutoAdjustCacheTime(boolean onff) {
        this.mAutoAdjustCacheTime = onff;
    }

    /**
     * 设置自动缓存策略最大缓存时间
     * @param timems 缓存时间，单位毫秒
     */
    public void setMaxAutoAdjustCacheTime(int timems) {
        this.mMaxAutoAdjustCacheTime = timems;
    }

    /**
     * 设置自动缓存策略最小缓存时间
     * @param timems 缓存时间，单位毫秒
     */
    public void setMinAutoAdjustCacheTime(int timems) {
        this.mMinAutoAdjustCacheTime = timems;
    }

    /**
     * 设置播放中断自动重连次数
     * @param count 次数
     */
    public void setConnectRetryCount(int count) {
        this.mConnectRetryCount = count;
    }

    /**
     * 设置播放中断后自动重连间隔时间
     * @param interval 间隔时间，单位秒
     */
    public void setConnectRetryInterval(int interval) {
        this.mConnectRetryInterval = interval;
    }

    /**
     * 设置播放器的声道
     * PLAY_CHANNEL_CONFIG_STEREO
     * PLAY_CHANNEL_CONFIG_LEFT
     * PLAY_CHANNEL_CONFIG_RIGHT
     * @param mode
     */
    public void setPlayAudioChannelMode(int mode) {
        this.mPlayAudioChannelMode = mode;
    }

    /**
     * 设置是否侦测音频设备焦点
     * true sdk侦测 false 停止侦测
     */
    public void setAudioFocusDetect(boolean value) {
        this.mAudioFocusDetect = value;
    }

    /**
     * 是否设置上传播放统计
     * @param value
     */
    public void setUpdateAVtatistic(boolean value) {this.mUpdateAVtatistic =  value;}

    /**
     * 设置audio stream 类型
     * @param audioStreamType
     */
    public void setAudioStreamType(int audioStreamType) {
        this.mAudioStreamType = audioStreamType;
    }

    /**
     * 设置视频加载策略
     * EM_PLAY_LOADING_STARTEGY_DEFAULT; 默认加载速度，慢
     * EM_PLAY_LOADING_STARTEGY_FAST;    快速加载，一般小视频推荐
     * EM_PLAY_LOADING_STARTEGY_NORMAL;  正常加载，一般720P高清视频推荐
     * EM_PLAY_LOADING_STARTEGY_CUSTOM;  用户自定义加载，一般使用在1080P及画以上的超清视频
     */
    public void setLoadingStrategy(int value){
        this.mLoadingStrategy = value;
    }

    /**
     * 设置用户自定义加载策略设置的值，单位KB，1080P及以上可设置300KB以上，确保视频能够正常播放，自定义值不可小于10KB
     */
    public void setCustomLoadingValue(int value){
        this.mCustomLoadingValue = value;
    }

    /**
     * dns缓存上限
     * @param value 如果设置0，则不启用dns cache功能 默认100
     */
    public void setDnsCacheCount(int value){
        this.mDnsCacheCount = value;
    }

    /**
     * dnscache中dns 有效时间，超过这个时间会自动更新dns，
     * @param value 单位:s 默认300(5min)
     */
    public void setDnsCacheValidTime(int value){
        this.mDnsCacheValidTime = value;
    }

    /**
     * 设置是否自动播放，默认自动播放
     * @param on
     */
    public void setAutoPlay(boolean on){
        this.mAutoPlay = on;
    }

    /**
     * 设置是否看视频第一帧(可作为封面)，@mAutoPlay=false生效
     * @param on
     */
    public void setViewFirstFrameOnPrepare(boolean on){
        this.mViewFirstFrameOnPrepare = on;
    }
}
