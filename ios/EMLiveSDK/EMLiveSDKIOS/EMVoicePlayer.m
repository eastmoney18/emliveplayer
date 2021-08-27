//
//  ELVoicePlayer.m
//  EMLiveSDKIOS
//
//  Created by maguobing on 2018/10/10.
//  Copyright © 2018年 eastmoney. All rights reserved.
//

#import "EMVoicePlayer.h"
#import <IJKMediaPlayer.h>
#import <AVFoundation/AVFoundation.h>

@interface EMVoicePlayer ()
{
    NSString *_urlPlayer;
}
@property (nonatomic, retain) IJKAVMoviePlayerController *player;

@end

@implementation EMVoicePlayer

- (void)dealloc {
//    NSLog(@"EMVoicePlayer 正常释放");
    [self removeObserver];
    
    if (self.player) {
        [self.player shutdown];
        self.player = nil;
    }
}

- (instancetype)init {
    self = [super init];
    if (self) {
        [self registerObserver];
    }
    return self;
}

- (int)startPlay:(NSString*)url {
    if (self.player) {
        [self.player shutdown];
        self.player = nil;
    }
    _urlPlayer = url;
    self.player = [[IJKAVMoviePlayerController alloc] initWithContentURLString:url];
    self.player.shouldAutoplay = YES;//加载成功后会自动播放
    [self.player prepareToPlay];
    [self.player play];//无用
    
    return 0;
}

- (int)stopPlay
{
    [self.player shutdown];
    return 0;
}

- (bool)isPlaying
{
    if (self.player) {
        return [self.player isPlaying];
    }else {
        return NO;
    }
}

- (void)pause
{
    if (self.player)
        [self.player pause];
}

- (void)resume
{
    if (self.player)
        [self.player play];
}

- (void)mute:(BOOL)enable{
    if (self.player) {
        [self.player mute:enable];
    }
}

- (int)seek:(float)time
{
    if (self.player) {
        [self.player setCurrentPlaybackTime:time];
        return 0;
    }else {
        return -1;
    }
}

- (void)setPlaybackRate:(float) rate {
    if (self.player) {
        return [self.player setPlaybackRate:rate];
    }
}

- (NSTimeInterval)getCurrentTime {
    return self.player.currentPlaybackTime;
}

- (NSTimeInterval)duration {
    if (self.player.duration < 0) {
        return 0;
    }
    return self.player.duration;
}

- (void)registerObserver
{
    //准备好：PreparedToPlay
    [[NSNotificationCenter defaultCenter] addObserver:self selector:@selector(voiceIsPreparedToPlayDidChange:) name:IJKAVMoviePlayerIsPreparedToPlayDidChangeNotification object:self.player];
    
    //可播放缓存进度
    [[NSNotificationCenter defaultCenter] addObserver:self selector:@selector(voicePlayLoadedTime:) name:IJKAVMoviePlayerLoadedTimeNotification  object:self.player];
    
    //播放进度
    [[NSNotificationCenter defaultCenter] addObserver:self selector:@selector(videoPlayProgress:) name:
     IJKAVMoviePlayerControllerPlayVoiceProgress object:self.player];
    
    //播放完毕
    [[NSNotificationCenter defaultCenter] addObserver:self selector:@selector(voicePlayDidFinish:) name:IJKAVMoviePlayerPlaybackDidFinishNotification object:self.player];
    
    //播放失败
    [[NSNotificationCenter defaultCenter] addObserver:self selector:@selector(voicePlayFailed:) name:IJKAVMoviePlayerControllerPlayVoiceFailed object:self.player];
    
}

- (void)removeObserver {
    [[NSNotificationCenter defaultCenter] removeObserver:self name:IJKAVMoviePlayerIsPreparedToPlayDidChangeNotification object:nil];
    [[NSNotificationCenter defaultCenter] removeObserver:self name:IJKAVMoviePlayerLoadedTimeNotification object:nil];
    [[NSNotificationCenter defaultCenter] removeObserver:self name:IJKAVMoviePlayerControllerPlayVoiceProgress object:nil];
    [[NSNotificationCenter defaultCenter] removeObserver:self name:IJKAVMoviePlayerPlaybackDidFinishNotification object:nil];
    [[NSNotificationCenter defaultCenter] removeObserver:self name:IJKAVMoviePlayerControllerPlayVoiceFailed object:nil];
}

- (void)videoPlayProgress:(NSNotification *)notification
{
    NSDictionary *dict = notification.userInfo;
    if (self.delegate && [self.delegate respondsToSelector:@selector(onPlayEvent:withParam:)]) {
        [self.delegate onPlayEvent:EM_PLAY_EVT_PLAY_PROGRESS withParam:dict];
    }
}

- (void)voicePlayLoadedTime:(NSNotification *)notification{
    NSDictionary *dict = notification.userInfo;
    if (self.delegate && [self.delegate respondsToSelector:@selector(onPlayEvent:withParam:)]) {
        [self.delegate onPlayEvent:EM_PLAY_EVT_PLAY_LOADING withParam:dict];
    }
}

- (void)voiceIsPreparedToPlayDidChange:(NSNotification*)notification
{
    NSDictionary *dict = notification.object;
    if (self.delegate && [self.delegate respondsToSelector:@selector(onPlayEvent:withParam:)]) {
        [self.delegate onPlayEvent:EM_PLAY_EVT_PLAY_BEGIN withParam:dict];
    }
}

- (void)voicePlayDidFinish:(NSNotification *)notification {
    NSDictionary *dict = notification.object;
    if (self.delegate && [self.delegate respondsToSelector:@selector(onPlayEvent:withParam:)]) {
        [self.delegate onPlayEvent:EM_PLAY_EVT_PLAY_END withParam:dict];
    }
}

- (void)voicePlayFailed:(NSNotification *)notification {
    NSDictionary *dict = notification.object;
    if (self.delegate && [self.delegate respondsToSelector:@selector(onPlayEvent:withParam:)]) {
        [self.delegate onPlayEvent:EM_PUSH_ERR_AUDIO_ENCODE_FAIL withParam:dict];
    }
}


@end
