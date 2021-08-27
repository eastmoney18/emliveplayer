//
//  ELVoicePlayer.h
//  EMLiveSDKIOS
//
//  Created by maguobing on 2018/10/10.
//  Copyright © 2018年 eastmoney. All rights reserved.
//

#import <Foundation/Foundation.h>
#import "EMLivePlayListener.h"

@interface EMVoicePlayer : NSObject

@property (nonatomic, weak) id<EMLivePlayListener>  delegate;

/* startPlay 启动从指定URL播放MP3音频流
 * 参数:
 *      url : 完整的URL
 *
 * 返回: 0 = OK
 */
- (int)startPlay:(NSString*)url;

/* stopPlay 停止播放音频流
 * 返回: 0 = OK
 */
- (int)stopPlay;

/* isPlaying 是否正在播放
 * 返回： YES 拉流中，NO 没有拉流
 */
- (bool)isPlaying;

/* pause 暂停播放，适用于点播
 *
 */
- (void)pause;

/* resume 继续播放，适用于点播
 *
 */
- (void)resume;

/*
 * mute 播放器静音
 */
- (void)mute:(BOOL)enable;

///*
// * setPlayerLoop 播放器点播播放次数
// * loopCount： 重复次数， 1默认值，0表示无限重复。
// */
//- (void)setPlayerLoop:(int)loopCount;

/*
 seek 播放跳转到音视频流某个时间
 * time: 流时间，单位为秒
 * 返回: 0 = OK
 */
- (int)seek:(float)time;

/*
 设置点播情况下的播放速度
 rate:播放倍数 0.5-2
 */
- (void)setPlaybackRate:(float) rate;

/*
 * 获取当前播放时间
 */
- (NSTimeInterval)getCurrentTime;
/*
 * 获取总时长
 */
- (NSTimeInterval)duration;

@end
