//
//  EMLiveSDKTypeDef.h
//  EMLiveSDKIOS
//
//  Created by 陈海东 on 16/9/7.
//  Copyright © 2016年 eastmoney. All rights reserved.
//

#import "EMLiveSDKEventDef.h"

#ifndef EMLiveSDKTypeDef_h
#define EMLiveSDKTypeDef_h

typedef NS_ENUM(NSInteger, EM_Enum_Type_LogLevel) {
    EM_LOGLEVEL_NULL = 0,      // 不输出任何sdk log
    EM_LOGLEVEL_ERROR = 1,     // 只输出ERROR的log
    EM_LOGLEVEL_WARN = 2,      // 只输出ERROR 和 WARNNING的log
    EM_LOGLEVEL_INFO = 3,      // 输出 INFO，WARNNING 和 ERROR 级别的log
    EM_LOGLEVEL_DEBUG = 4     // 输出 DEBUG，INFO，WARNING和ERROR 级别的log
};

typedef NS_ENUM(NSInteger, EM_Enum_Type_HomeOrientation) {
    EM_Enum_HOME_ORIENTATION_RIGHT  = 0,        // home在右边
    EM_Enum_HOME_ORIENTATION_DOWN,              // home在下面
    EM_Enum_HOME_ORIENTATION_LEFT,              // home在左边
    EM_Enum_HOME_ORIENTATION_UP,                // home在上面
};

typedef NS_ENUM(NSInteger, EM_Enum_Type_RenderMode) {
    EM_RENDER_MODE_FILL_SCREEN  = 0,    // 图像铺满屏幕
    EM_RENDER_MODE_FILL_EDGE,            // 图像长边填满屏幕
    EM_RENDER_MODE_BLUR_FILL_SCREEN     // 图像长边填满屏幕，背景模糊填充
};

typedef NS_ENUM(NSInteger, EM_Enum_Type_Play_Channel_Mode) {
    EM_PLAY_CHANNEL_MODE_STEREO = 0,
    EM_PLAY_CHANNEL_MODE_LEFT = 1,
    EM_PLAY_CHANNEL_MODE_RIGHT = 2,
};

typedef NS_ENUM(NSInteger, EM_Enum_Type_Play_Loading_Strategy) {
    EM_PLAY_LOADING_STARTEGY_DEFAULT = 0, // 默认加载速度，慢
    EM_PLAY_LOADING_STARTEGY_FAST = 1,    // 快速加载，一般小视频推荐
    EM_PLAY_LOADING_STARTEGY_NORMAL = 2,  // 正常加载，一般720P高清视频推荐
    EM_PLAY_LOADING_STARTEGY_CUSTOM = 3,  // 用户自定义加载，一般使用在1080P及画以上的超清视频
};

typedef NS_OPTIONS(NSUInteger, SourceTypeOptions) {
    SourceTypeLive = 1,        // 直播
    SourceTypeRecord = 2,      // 点播
    SourceTypeRecordVoice = 3  // 点播（纯音频）
};

//typedef void(^ResultSource)(id _Nullable dataObject);
typedef void(^ResultSource)(NSArray * _Nullable sourceURLs);

// 状态键名定义
#define NET_STATUS_CPU_USAGE         @"CPU_USAGE"        // cpu使用率
#define NET_STATUS_VIDEO_BITRATE     @"VIDEO_BITRATE"    // 当前视频编码器输出的比特率，也就是编码器每秒生产了多少视频数据，单位 kbps
#define NET_STATUS_AUDIO_BITRATE     @"AUDIO_BITRATE"    // 当前音频编码器输出的比特率，也就是编码器每秒生产了多少音频数据，单位 kbps
#define NET_STATUS_VIDEO_FPS         @"VIDEO_FPS"        // 当前视频帧率，也就是视频编码器每条生产了多少帧画面
#define NET_STATUS_NET_SPEED         @"NET_SPEED"        // 当前的发送速度
#define NET_STATUS_NET_JITTER        @"NET_JITTER"       // 网络抖动情况，抖动越大，网络越不稳定
#define NET_STATUS_CACHE_SIZE        @"CACHE_SIZE"       // 缓冲区大小，缓冲区越大，说明当前上行带宽不足以消费掉已经生产的视频数据
#define NET_STATUS_VIDEO_CACHE_COUNT @"VIDEO_CACHE_COUNT" //发送缓冲区中视频帧数
#define NET_STATUS_AUDIO_CACHE_COUNT @"AUDIO_CACHE_COUNT" //发送缓冲区中音频帧数
#define NET_STATUS_DROP_SIZE         @"DROP_SIZE"
#define NET_STATUS_VIDEO_WIDTH       @"VIDEO_WIDTH"
#define NET_STATUS_VIDEO_HEIGHT      @"VIDEO_HEIGHT"
#define NET_STATUS_SERVER_IP         @"SERVER_IP"
#define NET_STATUS_CODEC_CACHE       @"CODEC_CACHE"      //编解码缓冲大小
#define NET_STATUS_CODEC_DROP_CNT    @"CODEC_DROP_CNT"   //编解码队列DROPCNT

#define EVT_MSG                      @"EVT_MSG"
#define EVT_TIME                     @"EVT_TIME"
#define EVT_PLAY_PROGRESS            @"EVT_PLAY_PROGRESS"
#define EVT_PLAY_DURATION            @"EVT_PLAY_DURATION"
#define EVT_PLAY_STATE               @"EVT_PLAY_STATE"
#define EVT_VIDEO_WIDTH              @"EVT_VIDEO_WIDTH"
#define EVT_VIDEO_HEIGHT             @"EVT_VIDEO_HEIGHT"
#define EVT_RECORD_PROGRESS          @"EVT_RECORD_PROGRESS"

#define EVT_PLAY_STREAM_UNIX_TIME    @"EVT_PLAY_STREAM_UNIX_TIME"

#endif /* EMLiveSDKTypeDef_h */



