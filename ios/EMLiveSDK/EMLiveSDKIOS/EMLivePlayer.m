//
//  EMLivePlayer.m
//  EMLiveSDKIOS
//
//  Created by 陈海东 on 16/9/7.
//  Copyright © 2016年 eastmoney. All rights reserved.
//

#import <Foundation/Foundation.h>
#import "EMLivePlayer.h"
#import "EMLivePlayListener.h"
#import "EMLivePlayConfig.h"
#import "EMLiveSDKTypeDef.h"
#import "EMParameter.h"
#import <IJKMediaFramework/IJKMediaFramework.h>

@interface EMLivePlayer()<IJKMediaNativeInvokeDelegate>
{
    unsigned int _renderIndex;
    char * _multiSourceUrls;
}

@property (nonatomic, retain) EMFFMoviePlayerController *player;
@property (nonatomic, strong) UIView *containView;
@property (nonatomic, assign) CGRect renderFrame;
@property (nonatomic, retain) IJKFFOptions *options;
@end

@implementation EMLivePlayer

@synthesize config;

- (instancetype) init
{
    self = [super init];
    if (self) {
        self.options = [IJKFFOptions optionsByDefault];
        _multiSourceUrls = NULL;
        [self registerNotification];
    }
    return self;
}

- (void)checkAndApplyOptions:(EMLivePlayConfig *)config_
{
    if (self.enableHWAcceleration) {
#if TARGET_IPHONE_SIMULATOR
        //NSLog(@"EMPlayer hardware accelerator not support ios simulator");
#elif TARGET_OS_IPHONE
        [self.options setPlayerOptionIntValue:1 forKey:@"videotoolbox"];
        //NSLog(@"enable hardware decode video.\n");
#endif
    }else {
        [self.options setPlayerOptionIntValue:0 forKey:@"videotoolbox"];
        //NSLog(@"enable software decode video.\n");
    }
    [self.options setPlayerOptionIntValue:[config_ connectRetryCount] forKey:@"reconnect_count"];
    [self.options setPlayerOptionIntValue:[config_ connectRetryInterval] forKey:@"reconnect_interval"];
    [self.options setPlayerOptionIntValue:[config_ loopCount] forKey:@"loop"];
}

-(void) setupVideoWidget:(CGRect)frame containView:(UIView*)view insertIndex:(unsigned int)idx
{
    self.renderFrame = frame;
    self.containView = view;
    _renderIndex = idx;
    if (frame.origin.x + frame.origin.y + frame.size.width + frame.size.width < 0.1f) {
        //NSLog(@"use full container frame");
        self.renderFrame = self.containView.bounds;
    }
    
    if (self.containView) {
        if (self.player && !self.player.view && self.player.isPlaying) {
            [self.player setVideoDisplay:YES];
            self.player.view.autoresizingMask = UIViewAutoresizingFlexibleWidth|UIViewAutoresizingFlexibleHeight;
        }
        if ([self.player.view superview] == nil) {
            self.containView.autoresizesSubviews = YES;
            [self.containView insertSubview:self.player.view atIndex:_renderIndex];
            self.player.scalingMode = IJKMPMovieScalingModeAspectFill;
        }
    } else {
        if (self.player && self.player.view) {
            [self.player.view removeFromSuperview];
        }
    }
    
    if (self.player.view) {
        
    } else if (self.containView && self.player.isPlaying) {
        
    }
}

-(void) resetVideoWidgetFrame:(CGRect)frame
{
    self.player.view.frame = frame;
    self.renderFrame = frame;
}

-(void) removeVideoWidget
{
    if (self.player.view) {
        [self.player.view removeFromSuperview];
    }
    self.containView = nil;
}

-(int) startPlayMultiUrl:(NSArray *)urls type:(EM_Enum_PlayType)playType
{
    NSString *videoUrls = @"";
    for (NSString *url in urls) {
        videoUrls = [NSString stringWithFormat:@"%@%@\n", videoUrls, url];
    }
    if (_multiSourceUrls) {
        free(_multiSourceUrls);
        _multiSourceUrls = NULL;
    }
    char *urlBuf = (char *)malloc([videoUrls length] + 1);
    if (!urlBuf) {
        return -1;
    }
    memset(urlBuf, 0, [videoUrls length] + 1);
    strcpy(urlBuf, [videoUrls UTF8String]);
    NSString *actPlayUrl = [NSString stringWithFormat:@"emmul://%lu", (unsigned long)urlBuf];
    _multiSourceUrls = urlBuf;
    return [self startPlay:actPlayUrl type:playType];
}

-(int) startPlay:(NSString*)url type:(EM_Enum_PlayType)playType
{
    //[EMFFMoviePlayerController checkIfFFmpegVersionMatch:YES];
    [self checkAndApplyOptions:config];
    if (self.player) {
        if (self.player.view != nil && [self.player.view superview] != nil) {
            [self.player.view removeFromSuperview];
        }
        [self.player shutdown];
        self.player = nil;
    }
    self.player = [[EMFFMoviePlayerController alloc] initWithContentURL:[NSURL URLWithString:url] withOptions:self.options displayVideo:(self.containView != nil)];
    self.player.shouldAutoplay = YES;
    self.player.nativeInvokeDelegate = self;
    if (self.containView && [self.player.view superview] == nil) {
        self.player.view.autoresizingMask = UIViewAutoresizingFlexibleWidth|UIViewAutoresizingFlexibleHeight;
        self.player.view.frame = self.renderFrame;
        self.player.scalingMode = IJKMPMovieScalingModeAspectFill;
        self.containView.autoresizesSubviews = YES;
        [self.containView insertSubview:self.player.view atIndex:_renderIndex];
    }
    [self.player prepareToPlay:playType];
    //add mediacontrol
    return 0;
}

-(int) stopPlay
{
    [self.player stop];
    [self.player shutdown];
    if (_multiSourceUrls) {
        free(_multiSourceUrls);
        _multiSourceUrls = NULL;
    }
    return 0;
}

-(bool) isPlaying
{
    if (self.player) {
        return [self.player isPlaying];
    }else {
        return NO;
    }
}

- (void) setPlaybackRate:(float) rate {
    if (self.player) {
        return [self.player setPlaybackRate:rate];
    }
}


-(void) pause
{
    if (self.player)
        [self.player pause];
}

-(void) resume
{
    if (self.player)
        [self.player play];
}

- (void) mute:(BOOL)enable
{
    if (self.player) {
        [self.player mute:enable];
    }
}

- (void) setPlayerLoop:(int)loopCount
{
    if (self.player && loopCount >= 0) {
        [self.player setLoop:loopCount];
    }
}


-(int) seek:(float)time
{
    if (self.player) {
        [self.player setCurrentPlaybackTime:time];
        return 0;
    }else {
        return -1;
    }
}


-(void) setRenderRotation:(EM_Enum_Type_HomeOrientation)oritation
{
    if (self.player)
        [self.player setRotateMode:[EMParameter getRotateDegreeFromOrientation:oritation]];
}


-(void) setRenderMode:(int)renderMode
{
    if (self.player)
        [self.player setScalingMode:[EMParameter getIJKScalingModeFromRenderMode:(EM_Enum_Type_RenderMode)renderMode]];
}


- (void)standby
{
    if (self.player) {
        [self.player standby];
    }
}

- (int)changeVideoSource:(NSString *)addr withType:(EM_Enum_PlayType)playType
{
    if (self.player) {
       return [self.player changeVideoSource:addr withType:playType];
    }
    return -1;
}

- (int)changeMultiVideoSource:(NSArray *)urls withType:(EM_Enum_PlayType)playType
{
    if (self.player) {
        NSString *videoUrls = @"";
        for (NSString *url in urls) {
            videoUrls = [NSString stringWithFormat:@"%@%@\n", videoUrls, url];
        }
        if (_multiSourceUrls) {
            free(_multiSourceUrls);
            _multiSourceUrls = NULL;
        }
        char *urlBuf = (char *)malloc([videoUrls length] + 1);
        if (!urlBuf) {
            return -1;
        }
        memset(urlBuf, 0, [videoUrls length] + 1);
        strcpy(urlBuf, [videoUrls UTF8String]);
        NSString *actPlayUrl = [NSString stringWithFormat:@"emmul://%lu", (unsigned long)urlBuf];
        _multiSourceUrls = urlBuf;
        return [self.player changeVideoSource:actPlayUrl withType:playType];
    }
    return -1;
}


- (int)prepareNewVideoSource:(NSString *)addr withType:(EM_Enum_PlayType)playType
{
    if (self.player) {
        return [self.player prepareNewVideoSource:addr withType:playType];
    }
    return -1;
}


- (int)changeVideoSourceWithPreparedIndex:(int)index
{
    if (self.player) {
        return [self.player changeVideoSourceWithPreparedIndex:index];
    }
    return -1;
}

- (int)deletePreparedVideoSource:(int)index
{
    if (self.player) {
        return [self.player deletePreparedVideoSource:index];
    }
    return -1;
}

- (IJKLogLevel) ijkLogLevelFromEMLogLevel:(EM_Enum_Type_LogLevel)level
{
    IJKLogLevel ret = k_IJK_LOG_DEFAULT;
    switch (level) {
        case EM_LOGLEVEL_INFO:
            ret = k_IJK_LOG_INFO;
            break;
        case EM_LOGLEVEL_WARN:
            ret = k_IJK_LOG_WARN;
            break;
        case EM_LOGLEVEL_ERROR:
            ret = k_IJK_LOG_ERROR;
            break;
        case EM_LOGLEVEL_DEBUG:
            ret = k_IJK_LOG_DEBUG;
            break;
        case EM_LOGLEVEL_NULL:
            ret = k_IJK_LOG_SILENT;
            break;
        default:
            return k_IJK_LOG_DEFAULT;
    }
    return ret;
}


-(void) setLogLevel:(EM_Enum_Type_LogLevel) level
{
    [EMFFMoviePlayerController setLogLevel:[self ijkLogLevelFromEMLogLevel:level]];
}

- (UIImage *)captureFrame
{
    if (self.player) {
        return [self.player captureFrame];
    }
    return nil;
}

+(NSArray*) getSDKVersion
{
    return [EMParameter getSDKVersion];
}

-(void)registerNotification
{
    [[NSNotificationCenter defaultCenter] addObserver:self selector:@selector(natrualSizeAvalible:) name:EMMPMovieNaturalSizeAvailableNotification object:self.player];
    [[NSNotificationCenter defaultCenter] addObserver:self selector:@selector(seekComplete:) name:EMMPMoviePlayerDidSeekCompleteNotification object:self.player];
    [[NSNotificationCenter defaultCenter] addObserver:self selector:@selector(didVideoDecoderOpen:) name:EMMPMoviePlayerVideoDecoderOpenNotification object:self.player];
    [[NSNotificationCenter defaultCenter] addObserver:self selector:@selector(moviePlayBackDidFinish:) name:EMMPMoviePlayerPlaybackDidFinishNotification object:self.player];
    [[NSNotificationCenter defaultCenter] addObserver:self selector:@selector(loadStateDidChange:) name:EMMPMoviePlayerLoadStateDidChangeNotification object:self.player];
    //[[NSNotificationCenter defaultCenter] addObserver:self selector:@selector(notificationDispath:) name:IJKMPMoviePlayerScalingModeDidChangeNotification object:self.player];
    [[NSNotificationCenter defaultCenter] addObserver:self selector:@selector(moviePlayBackStateDidChange:) name:EMMPMoviePlayerPlaybackStateDidChangeNotification object:self.player];
    [[NSNotificationCenter defaultCenter] addObserver:self selector:@selector(firstAudioFrameComes:) name:EMMPMoviePlayerFirstAudioFrameRenderedNotification object:self.player];
    [[NSNotificationCenter defaultCenter] addObserver:self selector:@selector(firstVideoFrameComes:) name:EMMPMoviePlayerFirstVideoFrameRenderedNotification object:self.player];
    [[NSNotificationCenter defaultCenter] addObserver:self selector:@selector(mediaIsPreparedToPlayDidChange:) name:EMMPMediaPlaybackIsPreparedToPlayDidChangeNotification object:self.player];
    [[NSNotificationCenter defaultCenter] addObserver:self selector:@selector(openHWVideoDecoderFailed:) name:IJKMPMoviePlayerOpenHWVideoDecoderFailedNotification object:self.player];
    [[NSNotificationCenter defaultCenter] addObserver:self selector:@selector(videoPlayProgress:) name:
        IJKMPMoviePlayerPlayProgressNotification object:self.player];
    [[NSNotificationCenter defaultCenter] addObserver:self selector:@selector(videoPlayStreamUnixTime:) name:
        IJKMPMoviePlayerPlayStreamUnixTimeNotification object:self.player];
    [[NSNotificationCenter defaultCenter] addObserver:self selector:@selector(didTcpConnectServer:) name:
        IJKMPMoviePlayerConnectSeverSuccessNotification object:self.player];

    [[NSNotificationCenter defaultCenter] addObserver:self selector:@selector(netDisconnected:) name:IJKMPMoviePlayerNetDisconnectNotification object:self.player];
}

- (void)removeNotification
{
    [[NSNotificationCenter defaultCenter] removeObserver:self];
}

#pragma marked 回调

- (int)invoke:(IJKMediaEvent)event attributes:(NSDictionary *)attributes
{
    ////NSLog(@"envent:%ld\n", (long)event);
    return 0;
}

- (void)NetInfoStatus:(NSDictionary *)netInfo
{
    if (self.delegate && [self.delegate respondsToSelector:@selector(onNetStatus:)])
    {
        [self.delegate onNetStatus:netInfo];
    }
    /*int64_t curtimeus = time(NULL)*1000*1000;
    NSTimeInterval duration = [self.player duration];
    NSTimeInterval playProgress = [self.player currentPlaybackTime];
    NSDictionary *dic = [NSDictionary dictionaryWithObjectsAndKeys:@(curtimeus),EVT_TIME,@((float)duration), EVT_PLAY_DURATION,
                         @((float)playProgress), EVT_PLAY_PROGRESS, @"", EVT_MSG, nil];
    if (self.delegate && [self.delegate respondsToSelector:@selector(onPlayEvent:withParam:)]) {
        [self.delegate onPlayEvent:EM_PLAY_EVT_PLAY_PROGRESS withParam:dic];
        ////NSLog(@"play duration:%@", dic);
    }*/
}

- (void)natrualSizeAvalible:(NSNotification *)notification
{
    //NSLog(@"natrual size avalible notification comes\n");
}

- (void)seekComplete:(NSNotification *)notification
{
    //NSLog(@"seek complete notification comes\n");
}

- (void)didVideoDecoderOpen:(NSNotification *)notification
{
    //NSLog(@"video decoder open notification comes\n");
}

- (void)firstAudioFrameComes:(NSNotification *)notification
{
    //NSLog(@"first audio frame comes notification comes\n");
}

- (void)firstVideoFrameComes:(NSNotification *)notification
{
    //NSLog(@"first video frame comes notification comes\n");
    if (self.delegate && [self.delegate respondsToSelector:@selector(onPlayEvent:withParam:)]){
        int64_t curtimeus = time(NULL)*1000*1000;
        NSDictionary *param = [NSDictionary dictionaryWithObjectsAndKeys:
                               [NSNumber numberWithLongLong:curtimeus], EVT_TIME, @"EM_PLAY_EVT_RCV_FIRST_I_FRAME", EVT_MSG, nil];
        [self.delegate onPlayEvent:EM_PLAY_EVT_RCV_FIRST_I_FRAME withParam:param];
    }
}
     
- (void)netDisconnected:(NSNotification *)note
{
    if (self.delegate && [self.delegate respondsToSelector:@selector(onPlayEvent:withParam:)]){
        int64_t curtimeus = time(NULL)*1000*1000;
        NSDictionary *param = [NSDictionary dictionaryWithObjectsAndKeys:
                               [NSNumber numberWithLongLong:curtimeus], EVT_TIME, @"EM_PLAY_ERR_NET_DISCONNECT", EVT_MSG, nil];
        //NSLog(@"net disconnect notification comes\n");
        [self.delegate onPlayEvent:EM_PLAY_ERR_NET_DISCONNECT withParam:param];
    }
}

- (void)videoPlayProgress:(NSNotification *)notification
{
    NSDictionary *dict = notification.object;
    /*
     int64_t curtimeus = time(NULL)*1000*1000;
     NSTimeInterval duration = [self.player duration];
     NSTimeInterval playProgress = [self.player currentPlaybackTime];
     NSDictionary *dic = [NSDictionary dictionaryWithObjectsAndKeys:@(curtimeus),EVT_TIME,@((float)duration), EVT_PLAY_DURATION,
                         @((float)playProgress), EVT_PLAY_PROGRESS, @"EVT_PLAY_PROGRESS", EVT_MSG, nil];*/
    if (self.delegate && [self.delegate respondsToSelector:@selector(onPlayEvent:withParam:)]) {
        [self.delegate onPlayEvent:EM_PLAY_EVT_PLAY_PROGRESS withParam:dict];
    }

}

- (void)videoPlayStreamUnixTime:(NSNotification *)notification
{
    NSDictionary *dict = notification.object;
    if (self.delegate && [self.delegate respondsToSelector:@selector(onPlayEvent:withParam:)]) {
        [self.delegate onPlayEvent:EM_PLAY_EVT_STREAM_UNIX_TIME withParam:dict];
    }
}


- (void)didTcpConnectServer:(NSNotification *)notification
{
    int64_t curtimeus = time(NULL)*1000*1000;
    NSDictionary *param = [NSDictionary dictionaryWithObjectsAndKeys:
                           [NSNumber numberWithLongLong:curtimeus], EVT_TIME, @"EM_PLAY_EVT_CONNECT_SUCC", EVT_MSG, nil];
    if (self.delegate && [self.delegate respondsToSelector:@selector(onPlayEvent:withParam:)]) {
        [self.delegate onPlayEvent:EM_PLAY_EVT_CONNECT_SUCC withParam:param];
    }
}

- (void)openHWVideoDecoderFailed:(NSNotification *)note
{
    int64_t curtimeus = time(NULL)*1000*1000;
    NSDictionary *param = [NSDictionary dictionaryWithObjectsAndKeys:
                           [NSNumber numberWithLongLong:curtimeus], EVT_TIME, @"EM_PLAY_WARNING_HW_ACCELERATION_FAIL", EVT_MSG, nil];
    if (self.delegate && [self.delegate respondsToSelector:@selector(onPlayEvent:withParam:)]) {
        [self.delegate onPlayEvent:EM_PLAY_WARNING_HW_ACCELERATION_FAIL withParam:param];
    }

}

- (void)loadStateDidChange:(NSNotification*)notification
{
    //    MPMovieLoadStateUnknown        = 0,
    //    MPMovieLoadStatePlayable       = 1 << 0,
    //    MPMovieLoadStatePlaythroughOK  = 1 << 1, // Playback will be automatically started in this state when shouldAutoplay is YES
    //    MPMovieLoadStateStalled        = 1 << 2, // Playback will be automatically paused in this state, if started
    if (self.delegate && [self.delegate respondsToSelector:@selector(onPlayEvent:withParam:)])
    {
        IJKMPMovieLoadState loadState = _player.loadState;
        int64_t curtimeus = time(NULL)*1000*1000;
        NSMutableDictionary *param = [NSMutableDictionary dictionaryWithObjectsAndKeys:
                              [NSNumber numberWithLongLong:curtimeus], EVT_TIME, @"", EVT_MSG, nil];
        if ((loadState & IJKMPMovieLoadStatePlaythroughOK) != 0) {
            //NSLog(@"loadStateDidChange: IJKMPMovieLoadStatePlaythroughOK: %d\n", (int)loadState);
            [param setValue:@"EM_PLAY_EVT_PLAY_BEGIN" forKey:EVT_MSG];
            [self.delegate onPlayEvent:EM_PLAY_EVT_PLAY_BEGIN withParam:param];
        } else if ((loadState & IJKMPMovieLoadStateStalled) != 0) {
            //NSLog(@"loadStateDidChange: IJKMPMovieLoadStateStalled: %d\n", (int)loadState);
            [param setValue:@"EM_PLAY_EVT_PLAY_LOADING" forKey:EVT_MSG];
            [self.delegate onPlayEvent:EM_PLAY_EVT_PLAY_LOADING withParam:param];
            [param setValue:@"EM_PLAY_WARNING_VIDEO_PLAY_LAG" forKey:EVT_MSG];
            [self.delegate onPlayEvent:EM_PLAY_WARNING_VIDEO_PLAY_LAG withParam:param];
        } else {
            //NSLog(@"loadStateDidChange: ???: %d\n", (int)loadState);
        }
    }
}

- (void)moviePlayBackDidFinish:(NSNotification*)notification
{
    //    MPMovieFinishReasonPlaybackEnded,
    //    MPMovieFinishReasonPlaybackError,
    //    MPMovieFinishReasonUserExited
    int reason = [[[notification userInfo] valueForKey:EMMPMoviePlayerPlaybackDidFinishReasonUserInfoKey] intValue];
    int64_t curtimeus = time(NULL)*1000*1000;
    NSMutableDictionary *param = [NSMutableDictionary dictionaryWithObjectsAndKeys:
                           [NSNumber numberWithLongLong:curtimeus], EVT_TIME, @"", EVT_MSG, nil];
    switch (reason)
    {
        case IJKMPMovieFinishReasonPlaybackEnded:
            //NSLog(@"playbackStateDidChange: IJKMPMovieFinishReasonPlaybackEnded: %d\n", reason);
            if (self.delegate && [self.delegate respondsToSelector:@selector(onPlayEvent:withParam:)]) {
                [param setValue:@"EM_PLAY_EVT_PLAY_END" forKey:EVT_MSG];
                [self.delegate onPlayEvent:EM_PLAY_EVT_PLAY_END withParam:param];
            }
            break;
            
        case IJKMPMovieFinishReasonUserExited:
            //NSLog(@"playbackStateDidChange: IJKMPMovieFinishReasonUserExited: %d\n", reason);
            if (self.delegate && [self.delegate respondsToSelector:@selector(onPlayEvent:withParam:)]) {
                [param setValue:@"EM_PLAY_EVT_PLAY_END" forKey:EVT_MSG];
                [self.delegate onPlayEvent:EM_PLAY_EVT_PLAY_END withParam:param];
            }
            break;
            
        case IJKMPMovieFinishReasonPlaybackError:
            //NSLog(@"playbackStateDidChange: IJKMPMovieFinishReasonPlaybackError: %d\n", reason);
            break;
            
        default:
            //NSLog(@"playbackPlayBackDidFinish: ???: %d\n", reason);
            break;
    }
}

- (void)mediaIsPreparedToPlayDidChange:(NSNotification*)notification
{
    //NSLog(@"mediaIsPreparedToPlayDidChange\n");
}

- (void)moviePlayBackStateDidChange:(NSNotification*)notification
{
    //    MPMoviePlaybackStateStopped,
    //    MPMoviePlaybackStatePlaying,
    //    MPMoviePlaybackStatePaused,
    //    MPMoviePlaybackStateInterrupted,
    //    MPMoviePlaybackStateSeekingForward,
    //    MPMoviePlaybackStateSeekingBackward
    
    int64_t curtimeus = time(NULL)*1000*1000;
    NSDictionary *param = [NSDictionary dictionaryWithObjectsAndKeys:
                           [NSNumber numberWithLongLong:curtimeus], EVT_TIME, @"", EVT_MSG, nil];

    switch (_player.playbackState)
    {
        case IJKMPMoviePlaybackStateStopped: {
            //NSLog(@"IJKMPMoviePlayBackStateDidChange %d: stoped", (int)_player.playbackState);
            break;
        }
        case IJKMPMoviePlaybackStatePlaying: {
            //NSLog(@"IJKMPMoviePlayBackStateDidChange %d: playing", (int)_player.playbackState);
            /*if (self.delegate && [self.delegate respondsToSelector:@selector(onPlayEvent:withParam:)]) {
                [self.delegate onPlayEvent:PLAY_EVT_PLAY_PROGRESS withParam:param];
            }*/
            break;
        }
        case IJKMPMoviePlaybackStatePaused: {
            //NSLog(@"IJKMPMoviePlayBackStateDidChange %d: paused", (int)_player.playbackState);
            break;
        }
        case IJKMPMoviePlaybackStateInterrupted: {
            //NSLog(@"IJKMPMoviePlayBackStateDidChange %d: interrupted", (int)_player.playbackState);
            break;
        }
        case IJKMPMoviePlaybackStateSeekingForward:
        case IJKMPMoviePlaybackStateSeekingBackward: {
            //NSLog(@"IJKMPMoviePlayBackStateDidChange %d: seeking", (int)_player.playbackState);
            break;
        }
        default: {
            //NSLog(@"IJKMPMoviePlayBackStateDidChange %d: unknown", (int)_player.playbackState);
            break;
        }
    }
}


- (void)dealloc
{
    [self removeNotification];
    NSLog(@"delloc");
}

@end
