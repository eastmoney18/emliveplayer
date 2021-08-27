//
//  EMLivePlayer2.h
//  EMLiveSDKIOS
//
//  Created by 陈海东 on 18/3/1.
//  Copyright © 2018年 eastmoney. All rights reserved.
//

#import <Foundation/Foundation.h>
#import <UIKit/UIKit.h>
#import "EMLivePlayListener.h"
#import "EMLivePlayConfig.h"
#import "EMLiveSDKTypeDef.h"

typedef NS_ENUM(NSInteger, EM_Enum_PlayType) {
    EM_PLAY_TYPE_LIVE_RTMP = 0,          //RTMP直播
    EM_PLAY_TYPE_LIVE_FLV,               //FLV直播
    EM_PLAY_TYPE_VOD_FLV,                //FLV点播
    EM_PLAY_TYPE_VOD_HLS,                //HLS点播/直播
    EM_PLAY_TYPE_VOD_MP4,                //MP4点播
    EM_PLAY_TYPE_LOCAL_VIDEO,            //本地播放
};



@protocol EMLiveAudioPCMListener<NSObject>

/**
 * 回调媒体中的音频数据
 * 注意：在回调中不能修改数据，不能做耗时动作，异步处理时，需要copy数据
 * 说明：音频是16bit数据
 * size 数据长度
 * sr 音频采样率
 * channels 声道数
 * timems时间戳
 * 音频1s数据长度算法 1s = SampleRate x channels x 2  (BYTES)
 */
-(void)onPCMData:(const uint8_t *)data Length:(int)size SampleRate:(int)sr Channels:(int)channels TimeStamp:(int64_t)timems;

@end



@interface EMLivePlayer : NSObject
@property (nonatomic, weak) id<EMLivePlayListener> delegate;
@property (nonatomic, weak) id<EMLiveAudioPCMListener> audioPCMDelegate;
@property (nonatomic, assign) BOOL enableHWAcceleration;
@property (nonatomic, copy) EMLivePlayConfig *config;



/* setupVideoWidget 创建Video渲染Widget,该控件承载着视频内容的展示。
 * 参数:
 *      frame : Widget在父view中的rc
 *      view : 父view
 * 变更历史：1.5.2版本将参数frame废弃，设置此参数无效，控件大小与参数view的大小保持一致
 */
-(void) setupVideoWidget:(CGRect)frame containView:(UIView*)view insertIndex:(unsigned int)idx;


/* setupVideoWidget 创建Video渲染Widget,该控件承载着视频内容的展示。
 * 参数:
 *      view : 父view
 *      idx: 插入view index
 * 类抖音小视频设置view，控件大小与参数view的大小保持一致
 */
-(void) setupVideoWidget:(UIView*)view insertIndex:(unsigned int)idx;


/* removeVideoWidget 移除Video渲染Widget
 */
-(void) removeVideoWidget;

/**
 * 使用fast view render 方式渲染，在设置view render mode前设置，否则使用默认方式渲染
 */
-(void) setUseFastViewRender:(BOOL) on;


/* startPlay 启动从指定URL播放RTMP音视频流
 * 参数:
 *      url : 完整的URL
 *      playType: 播放类型
 * 返回: 0 = OK
 */
-(int) startPlay:(NSString*)url type:(EM_Enum_PlayType)playType;

/* stopPlay 停止播放音视频流
 * 返回: 0 = OK
 */
-(int) stopPlay;

/**
 * 清除停止播放后，残留的最后一帧画面
 * stopPlay后调用
 */
-(void) clearLastFrame;

/* isPlaying 是否正在播放
 * 返回： YES 拉流中，NO 没有拉流
 */
-(bool) isPlaying;

/* pause 暂停播放，适用于点播
 *
 */
-(void) pause;

/* resume 继续播放，适用于点播
 *
 */
-(void) resume;

/*
 * mute 播放器静音
 */
- (void) mute:(BOOL)enable;

/*
 * setPlayerLoop 播放器点播播放次数
 * loopCount： 重复次数， 1默认值，0表示无限重复。
 */
- (void) setPlayerLoop:(int)loopCount;

/*
 seek 播放跳转到音视频流某个时间
 * time: 流时间，单位为秒
 * 返回: 0 = OK
 */
-(int) seek:(float)time;

/*
 * standby: 暂停一切， 后台视频也不再加载， 配合切换视频源或者预加载使用，也可以用在流量模式下
 */
- (void)standby;

/*
 *changeVideoSource: 在播放，暂停或者standby状态下，直接切换视频源
 * addr: 要切换的视频源地址
 * playType: 要切换视频源的地址格式
 * return 成功返回0，失败返回-1
 */
- (int)changeVideoSource:(NSString *)addr withType:(EM_Enum_PlayType)playType;

/*
 设置点播情况下的播放速度
 rate:播放倍数 0.5-2
 */
- (void) setPlaybackRate:(float) rate;
/*
 * setRenderRotation 设置画面的方向
 * 参数：
 *       rotation : EM_Enum_Type_HomeOrientation
 */
-(void) setRenderRotation:(EM_Enum_Type_HomeOrientation)rotation;

/* setRenderMode 设置画面的裁剪模式
 * 参数
 *       renderMode : 详见 EM_Enum_Type_RenderMode 的定义。
 */
-(void) setRenderMode:(EM_Enum_Type_RenderMode)renderMode;

/* setPlayChannelMode 设置音频播放的立体声模式
 * 参数
 *       mode : 详见 EM_Enum_Type_Play_Channel_Mode 的定义。
 */
-(void) setPlayChannelMode:(EM_Enum_Type_Play_Channel_Mode)mode;

/* setLogLevel 设置log输出级别
 *  level：参见 LOGLEVEL
 *
 */
-(void) setLogLevel:(EM_Enum_Type_LogLevel) level;

- (UIImage *)captureFrame;

/* getSDKVersion 获取SDK版本信息
 */
+(NSArray*) getSDKVersion;

@end


