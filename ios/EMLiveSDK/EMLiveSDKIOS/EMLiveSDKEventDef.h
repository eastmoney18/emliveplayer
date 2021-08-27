//
//  EMLiveSDKEventDef.h
//  EMLiveSDKIOS
//
//  Created by 陈海东 on 16/9/7.
//  Copyright © 2016年 eastmoney. All rights reserved.
//
#ifndef EMLiveSDKEventDef_h
#define EMLiveSDKEventDef_h

typedef NS_ENUM(NSInteger, EM_Enum_EventID) {
    /**
     * 推流事件列表
     */
    /*step2*/    EM_PUSH_EVT_CONNECT_SUCC            =  1001,   // 已经连接推流服务器
    /*step3*/    EM_PUSH_EVT_PUSH_BEGIN              =  1002,   // 已经与服务器握手完毕,开始推流
    /*step1*/    EM_PUSH_EVT_OPEN_CAMERA_SUCC        =  1003,   // 打开摄像头成功
    EM_PUSH_EVT_VIDEO_RECORD_PROGRESS   =  1004,   // 小视频录制进度
    /*step1*/    EM_PUSH_ERR_OPEN_CAMERA_FAIL        = -1301,   // 打开摄像头失败
    /*step1*/    EM_PUSH_ERR_OPEN_MIC_FAIL           = -1302,   // 打开麦克风失败
    /*step3*/    EM_PUSH_ERR_VIDEO_ENCODE_FAIL       = -1303,   // 视频编码失败
    /*step3*/    EM_PUSH_ERR_AUDIO_ENCODE_FAIL       = -1304,   // 音频编码失败
    /*step1*/    EM_PUSH_ERR_UNSUPPORTED_RESOLUTION  = -1305,   // 不支持的视频分辨率
    /*step1*/    EM_PUSH_ERR_UNSUPPORTED_SAMPLERATE  = -1306,   // 不支持的音频采样率
    
    /*step2*/    EM_PUSH_ERR_NET_DISCONNECT          = -1307,   // 网络断连,且经多次重连抢救无效,可以放弃治疗,更多重试请自行重启推流
    
    EM_PUSH_ERR_CHECK_PARAMETER_FAIL    = -1308,
    
    EM_PUSH_ERR_AV_CAPTURE_FAIL         = -1309,
    /*step3*/    EM_PUSH_WARNING_NET_BUSY            =  1101,   // 网络状况不佳：上行带宽太小，上传数据受阻
    /*step2*/    EM_PUSH_WARNING_RECONNECT           =  1102,   // 网络断连, 已启动自动重连 (自动重连连续失败超过三次会放弃)
    /*step3*/    EM_PUSH_WARNING_HW_ACCELERATION_FAIL=  1103,   // 硬编码启动失败，采用软编码
    /*step2*/    EM_PUSH_WARNING_DNS_FAIL            =  3001,   // RTMP -DNS解析失败
    /*step2*/    EM_PUSH_WARNING_SEVER_CONN_FAIL     =  3002,   // RTMP服务器连接失败
    /*step2*/    EM_PUSH_WARNING_SHAKE_FAIL          =  3003,   // RTMP服务器握手失败
    
    /**
     * 播放事件列表
     */
    /*step1*/    EM_PLAY_EVT_CONNECT_SUCC            =  2001,   // 已经连接服务器
    /*step2*/    EM_PLAY_EVT_RTMP_STREAM_BEGIN       =  2002,   // 已经连接服务器，开始拉流
    /*step2*/    EM_PLAY_EVT_RCV_FIRST_I_FRAME       =  2003,   // 网络接收到首个视频数据包(IDR)
    /*step3*/    EM_PLAY_EVT_PLAY_BEGIN              =  2004,   // 视频播放开始
    /*step3*/    EM_PLAY_EVT_PLAY_PROGRESS           =  2005,   // 视频播放进度
    /*step3*/    EM_PLAY_EVT_PLAY_END                =  2006,   // 视频播放结束
    /*step3*/    EM_PLAY_EVT_PLAY_LOADING			 =  2007,   // 视频播放loading
                 EM_PLAY_EVT_STREAM_UNIX_TIME        =  2008,   // 视频流中夹杂着流的时钟信息
                 EM_PLAY_EVT_PLAY_PREPARE            =  2009,   // 视频准备好，可以开始播放
                 EM_PLAY_EVT_PLAY_SEEK_COMPLETE      =  2010,   // seek complete
                 EM_PLAY_EVT_PLAY_STATE_CHANGE       =  2200,   // play state change
    
    /*step1*/    EM_PLAY_ERR_NET_DISCONNECT          = -2301,   // 网络断连,且经多次重连抢救无效,可以放弃治疗,更多重试请自行重启播放
    /*step2*/    EM_PLAY_ERR_NET_FORBIDDEN           = -2302,   // 服务端拒绝访问，通知业务层重新获取播放地址
    /*step3*/    EM_PLAY_WARNING_VIDEO_DECODE_FAIL   =  2101,   // 当前视频帧解码失败
    /*step3*/    EM_PLAY_WARNING_AUDIO_DECODE_FAIL   =  2102,   // 当前音频帧解码失败
    /*step1*/    EM_PLAY_WARNING_RECONNECT           =  2103,   // 网络断连, 已启动自动重连 (自动重连连续失败超过三次会放弃)
    /*step3*/    EM_PLAY_WARNING_RECV_DATA_LAG       =  2104,   // 网络来包不稳：可能是下行带宽不足，或由于主播端出流不均匀
    /*step3*/    EM_PLAY_WARNING_VIDEO_PLAY_LAG      =  2105,   // 当前视频播放出现卡顿（用户直观感受）
    /*step3*/    EM_PLAY_WARNING_HW_ACCELERATION_FAIL=  2106,   // 硬解启动失败，采用软解
    /*step3*/    EM_PLAY_WARNING_VIDEO_DISCONTINUITY =  2107,   // 当前视频帧不连续，可能丢帧
    /*step1*/    EM_PLAY_WARNING_DNS_FAIL            =  3001,   // RTMP -DNS解析失败
    /*step1*/    EM_PLAY_WARNING_SEVER_CONN_FAIL     =  3002,   // RTMP服务器连接失败
    /*step1*/    EM_PLAY_WARNING_SHAKE_FAIL          =  3003,   // RTMP服务器握手失败
    
    /**
     * PCM音频 DB回调事件
     */
    EM_PUSH_AUDIO_PCM_DB_EVENT_CALLBACK             =  4001    //回调PCM DB数据
};

/**
 * EM_PLAY_EVT_PLAY_STATE_CHANGE
 */
typedef NS_ENUM(NSInteger, EM_Enum_PLAYSTATE) {
    EM_PLAY_STATE_PLAYING,
    EM_PLAY_STATE_STOP,
    EM_PLAY_STATE_PAUSE,
    EM_PLAY_STATE_SEEK,
};

#endif /* EMLiveSDKEventDef_h */


