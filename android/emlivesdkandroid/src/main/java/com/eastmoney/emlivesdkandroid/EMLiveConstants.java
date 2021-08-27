package com.eastmoney.emlivesdkandroid;

/**
 * Created by eastmoney on 16/10/21.
 */
public class EMLiveConstants {
    public static final int[] sdkVersion = new int[]{1,5,48,39};
    public static final int EM_VIDEO_SOURCE_TYPE_CAMERA = 0;
    public static final int EM_VIDEO_SOURCE_TYPE_SCREEN = 1;
    public static final int EM_VIDEO_SOURCE_TYPE_VIDEOFILE = 2;
    public static final int HAND_DRAW_TYPE_NORMAL = 0;
    public static final int HAND_DRAW_TYPE_SIGNATURE = 1;
    public static final int HAND_DRAW_TYPE_ERASE = 2;
    public static final int PUSH_TYPE_RTMP = 0;
    public static final int PUSH_TYPE_FILE = 1;
    public static final int PUSH_TYPE_RTP = 2;
    public static final int VIDEO_ANGLE_HOME_RIGHT = 0;
    public static final int VIDEO_ANGLE_HOME_DOWN = 1;
    public static final int VIDEO_ANGLE_HOME_LEFT = 2;
    public static final int VIDEO_ANGLE_HOME_UP = 3;
    public static final int CUSTOM_MODE_AUDIO_CAPTURE = 1;
    public static final int CUSTOM_MODE_VIDEO_CAPTURE = 2;
    public static final int CUSTOM_MODE_AUDIO_PREPROCESS = 4;
    public static final int CUSTOM_MODE_VIDEO_PREPROCESS = 8;
    public static final int RENDER_MODE_FILL_STRETCH = 0;
    public static final int RENDER_MODE_FILL_PRESERVE_AR_FILL = 1;
    public static final int RENDER_MODE_FILL_PRESERVE_AR = 2;
    public static final int RENDER_MODE_FILL_PRESERVE_AR_BLUR_BG = 3;
    public static final int RENDER_ROTATION_PORTRAIT = 0;
    public static final int RENDER_ROTATION_LANDSCAPE = 1;
    public static final int HOME_ORITATION_DOWN = 0;
    public static final int HOME_ORITATION_UP = 1;
    public static final int HOME_ORITATION_LEFT = 2;
    public static final int HOME_ORITATION_RIGHT = 3;
    public static final int PLAY_EVT_CONNECT_SUCC = 2001;
    public static final int PLAY_EVT_RTMP_STREAM_BEGIN = 2002;
    public static final int PLAY_EVT_RCV_FIRST_I_FRAME = 2003;
    public static final int PLAY_EVT_PLAY_BEGIN = 2004;
    public static final int PLAY_EVT_PLAY_PROGRESS = 2005;
    public static final int PLAY_EVT_PLAY_END = 2006;
    public static final int PLAY_EVT_PLAY_LOADING = 2007;
    public static final int PLAY_EVT_PLAY_STREAM_UNIX_TIME = 2008;
    public static final int PLAY_EVT_PLAY_AUDIODEVICE_LOSS_FOCUS = 2009;
    public static final int PLAY_EVT_PLAY_AUDIODEVICE_GAIN_FOCUS = 2010;
    public static final int PLAY_EVT_PLAY_SEEK_COMPLETE = 2011;
    public static final int PLAY_EVT_PLAY_PREPARED = 2012;
    public static final int PLAY_EVT_PLAY_EXIT = 2100;//real exit , for example read_thread exit
    public static final int PLAY_ERR_NET_DISCONNECT = -2301;
    public static final int PLAY_ERR_NET_FORBIDDEN = -2302;
    public static final int PLAY_ERR_GETPLAYURL_TIMEOUT = -2303;
    public static final int PLAY_ERR_PLAYURLS_EMPTY = -2304;
    public static final int PLAY_WARNING_VIDEO_DECODE_FAIL = 2101;
    public static final int PLAY_WARNING_AUDIO_DECODE_FAIL = 2102;
    public static final int PLAY_WARNING_RECONNECT = 2103;
    public static final int PLAY_WARNING_RECV_DATA_LAG = 2104;
    public static final int PLAY_WARNING_VIDEO_PLAY_LAG = 2105;
    public static final int PLAY_WARNING_SWITCH_SOFT_DECODE = 2106;
    public static final int PLAY_WARNING_DNS_FAIL = 3001;
    public static final int PLAY_WARNING_SEVER_CONN_FAIL = 3002;
    public static final int PLAY_WARNING_SHAKE_FAIL = 3003;
    public static final int LOG_LEVEL_NULL = 0;
    public static final int LOG_LEVEL_ERROR = 1;
    public static final int LOG_LEVEL_WARN = 2;
    public static final int LOG_LEVEL_INFO = 3;
    public static final int LOG_LEVEL_DEBUG = 4;
    public static final String EVT_TIME = "EVT_TIME";
    public static final String EVT_DESCRIPTION = "EVT_DESCRIPTION";
    public static final String EVT_RECORD_PROGRESS = "EVT_RECORD_PROGRESS";
    public static final String NET_STATUS_VIDEO_BITRATE = "VIDEO_BITRATE";
    public static final String NET_STATUS_AUDIO_BITRATE = "AUDIO_BITRATE";
    public static final String NET_STATUS_VIDEO_FPS = "VIDEO_FPS";
    public static final String NET_STATUS_NET_SPEED = "NET_SPEED";
    public static final String NET_STATUS_NET_JITTER = "NET_JITTER";
    public static final String NET_STATUS_CACHE_SIZE = "CACHE_SIZE";
    public static final String NET_STATUS_DROP_SIZE = "DROP_SIZE";
    public static final String NET_STATUS_VIDEO_WIDTH = "VIDEO_WIDTH";
    public static final String NET_STATUS_VIDEO_HEIGHT = "VIDEO_HEIGHT";
    public static final String NET_STATUS_CPU_USAGE = "CPU_USAGE";
    public static final String NET_STATUS_SERVER_IP = "SERVER_IP";
    public static final String EVT_PLAY_PROGRESS = "EVT_PLAY_PROGRESS";
    public static final String EVT_PLAY_STREAM_UNIX_TIME = "EVT_PLAY_STREAM_UNIX_TIME";
    public static final String EVT_PLAY_DURATION = "EVT_PLAY_DURATION";
    public static final String NET_STATUS_CODEC_CACHE = "CODEC_CACHE";
    public static final String NET_STATUS_CODEC_DROP_CNT = "CODEC_DROP_CNT";
    public static final String NET_STATUS_VIDEO_CACHE_CNT = "VIDEO_CACHE_CNT";
    public static final String NET_STATUS_AUDIO_CACHE_CNT = "AUDIO_CACHE_CNT";

    public static final int PLAY_CHANNEL_CONFIG_STEREO= 0;
    public static final int PLAY_CHANNEL_CONFIG_LEFT = 1;
    public static final int PLAY_CHANNEL_CONFIG_RIGHT = 2;


    public static final int EM_PLAY_LOADING_STARTEGY_DEFAULT = 0; // 默认加载速度，慢
    public static final int EM_PLAY_LOADING_STARTEGY_FAST = 1;    // 快速加载，一般小视频推荐
    public static final int EM_PLAY_LOADING_STARTEGY_NORMAL = 2;  // 正常加载，一般720P高清视频推荐
    public static final int EM_PLAY_LOADING_STARTEGY_CUSTOM = 3;  // 用户自定义加载，一般使用在1080P及画以上的超清视频



    public EMLiveConstants() {
    }
}
