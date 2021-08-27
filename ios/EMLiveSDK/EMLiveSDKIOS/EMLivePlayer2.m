//
//  EMLivePlayer2.m
//  EMLiveSDKIOS
//
//  Created by 陈海东 on 18/3/1.
//  Copyright © 2018年 eastmoney. All rights reserved.
//

#import <Foundation/Foundation.h>
#import "EMLivePlayer.h"
#import "EMLivePlayListener.h"
#import "EMLivePlayConfig.h"
#import "EMLiveSDKTypeDef.h"
#import "medialiveimage/MLImage.h"
#import "medialiveimage/MLImageContextInterface.h"
#import "medialiveimage/MLImageView.h"
#import "medialiveimage/MLImageBufferSource.h"
#import "EMParameter.h"
#import <IJKMediaFramework/IJKMediaFramework.h>

@interface EMLivePlayer()<IJKMediaNativeInvokeDelegate, IJKMediaVideoFrameDelegate, IJKMediaAudioFrameDelegate>
{
    unsigned int _renderIndex;
    NSString *_urlPlayer;
}

@property (nonatomic, retain) EMFFMoviePlayerController *player;
@property (nonatomic, retain) MLImageView *imageView;
@property (nonatomic, retain) MediaLiveImageContext *imageContext;
@property (nonatomic, weak)   MLImageBufferSource *imageBufferSource;
@property (nonatomic, strong) UIView *containView;
@property (nonatomic, assign) CGRect renderFrame;
@property (nonatomic, retain) IJKFFOptions *options;

/*
 auto play is false and view first video frame is true;
 播放器sdk层会截取视频第一帧，当作视频第一帧，此时播放器会解码，mute audio true
 渲染视频第一帧后，pause play，mute audio false
 用户在这个操作期间，需要记录下状态，在返回第一帧后，还原操作
 */
@property (nonatomic, assign) BOOL realMute; //记录用户是否真实mute
@property (nonatomic, assign) BOOL realPause;//记录用户是否真实pause
@property (nonatomic, assign) BOOL recvFirstVideoFrame; //是否收到视频第一帧

@end

@implementation EMLivePlayer

@synthesize config;

- (instancetype) init
{
    self = [super init];
    if (self) {
        
        [MediaLiveImageContext setLogoutBlock:^(int level, NSString *content) {
            emlivesdk_log_printf(@"%@", content);
        }];
        
        [EMFFMoviePlayerController setLogOutput:^(int level, NSString *content){
            //过滤ffmpeg输出过多的日志
            if([content containsString:@"tcp dns cost"] || [content containsString:@"cache dns ptr"]
               || [content containsString:@"dns from cache"]
               || [content containsString:@"find protocol"]
               || [content containsString:@"seek find timestamp"]){
                return;
            }
            emlivesdk_log_printf(@"%@", content);
        }];
#ifdef DEBUG
        [self setLogLevel:EM_LOGLEVEL_INFO];
#else
        [self setLogLevel:EM_LOGLEVEL_INFO];
#endif
        self.options = [IJKFFOptions optionsByDefault];
        //[self registerNotification];
        _imageView = [[MLImageView alloc] init];
        _imageView.autoresizingMask = UIViewAutoresizingFlexibleWidth|UIViewAutoresizingFlexibleHeight;
        _imageContext = [[MediaLiveImageContext alloc] init];
    }
    return self;
}

- (void)checkAndApplyOptions:(EMLivePlayConfig *)config_
{
    if (self.enableHWAcceleration) {
#if TARGET_IPHONE_SIMULATOR
        NSLog(@"EMPlayer hardware accelerator not support ios simulator");
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
    [self.options setPlayerOptionIntValue:[config_ playChannelMode] forKey:@"play_channel_mode"];
    [self.options setPlayerOptionIntValue:1 forKey:@"enable_sonic_handle"];
    [self.options setPlayerOptionIntValue:config_.dnsCacheValidTime forKey:@"dns_timeout"];
    [self.options setPlayerOptionIntValue:config_.dnsCacheCount forKey:@"dns_cache_count"];
    [self.options setPlayerOptionIntValue:config_.viewFirstVideoFrameOnPrepare forKey:@"view-video-first-frame"];
    
    switch (self.config.loadingStrategy) {
        case EM_PLAY_LOADING_STARTEGY_DEFAULT:
            break;
        case EM_PLAY_LOADING_STARTEGY_FAST:
            [self.options setPlayerOptionIntValue:100 forKey:@"probesize"]; // 探测大小100KB
            break;
        case EM_PLAY_LOADING_STARTEGY_NORMAL:
            [self.options setPlayerOptionIntValue:200 forKey:@"probesize"]; // 探测大小200KB
            break;
        case EM_PLAY_LOADING_STARTEGY_CUSTOM:
            [self.options setPlayerOptionIntValue:self.config.customLoadingValue>=10?self.config.customLoadingValue:100 forKey:@"probesize"];
            break;
        default:
            break;
    }
    
    emlivesdk_log_printf(@"use loading strategy is %d!!", self.config.loadingStrategy);
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
        _imageView.frame = self.renderFrame;
        self.containView.autoresizesSubviews = YES;
        [self.containView insertSubview:_imageView atIndex:_renderIndex];
        [_imageContext setImageView:_imageView];
    } else {
        [_imageView removeFromSuperview];
    }
}

-(void) setupVideoWidget:(UIView*)view insertIndex:(unsigned int)idx
{
    self.containView = view;
    _renderIndex = idx;
    self.renderFrame = self.containView.bounds;
    
    if (self.containView) {
        [_imageView removeFromSuperview];
        _imageView.frame = self.renderFrame;
        self.containView.autoresizesSubviews = YES;
        [self.containView insertSubview:_imageView atIndex:_renderIndex];
        _imageView.layer.opaque = NO;
        _imageView.opaque = NO;
        [_imageContext setImageView:_imageView];
    } else {
        [_imageView removeFromSuperview];
    }
}

-(void) setUseFastViewRender:(BOOL) on
{
    if(_imageContext){
        [_imageContext setUseFastViewRender:on];
    }
}

-(void) resetVideoWidgetFrame:(CGRect)frame
{
    _imageView.frame = frame;
    self.renderFrame = frame;
}

-(void) removeVideoWidget
{
    if (_imageView) {
        [_imageView removeFromSuperview];
    }
    self.containView = nil;
}

-(int) startPlay:(NSString*)url type:(EM_Enum_PlayType)playType
{
    //[EMFFMoviePlayerController checkIfFFmpegVersionMatch:YES];
    emlivesdk_log_printf(@"====start play:%@,type:%d====", url, (int)playType);
    [self checkAndApplyOptions:config];
    if (self.player) {
        if (self.player.view != nil && [self.player.view superview] != nil) {
            [self.player.view removeFromSuperview];
        }
        [self.player shutdown];
        self.player = nil;
    }
    _urlPlayer = url;
    self.player = [[EMFFMoviePlayerController alloc] initWithContentURL:[NSURL URLWithString:url] withOptions:self.options displayVideo:NO];
    if(self.config){
        self.player.shouldAutoplay = self.config.autoPlay;
    }
    else{
        self.player.shouldAutoplay = YES;
    }
    
    self.player.nativeInvokeDelegate = self;
    self.player.videoFrameDelegate = self;
    self.player.audioFrameDelegate = self;
    if(url.length > 0){
        [self.player prepareToPlay:(int)playType];
    }
    int bufferType = ML_IMAGE_BUFFER_TYPE_RGBA32;
    if (self.enableHWAcceleration) {
#if TARGET_IPHONE_SIMULATOR
        
#elif TARGET_OS_IPHONE
        bufferType = ML_IMAGE_BUFFER_TYPE_IOS_CVPixelImage;
#endif
    }
    if ([_imageContext startBufferSource:bufferType :0 :0]) {
        _imageBufferSource = [_imageContext getNativeBufferSource];
    }
    //add mediacontrol
    [self removeNotification];
    [self registerNotification];
    
    return 0;
}

-(int) stopPlay
{
    emlivesdk_log_printf(@"stop play...");
    [self.player stop];
    [self.player shutdown];
    [self removeNotification];
    
    return 0;
}

-(void)clearLastFrame
{
    if(_imageContext){
        [_imageContext clearLastFrame];
    }
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
    if (self.player){
        if(self.config && !self.config.autoPlay && self.config.viewFirstVideoFrameOnPrepare && !self.recvFirstVideoFrame){
            emlivesdk_log_printf(@"record user pause，waiting for frist video frame...");
            self.realPause = YES;
        }
        else{
            [self.player pause];
        }
    }
}

-(void) resume
{
    if (self.player){
        [self.player play];
        self.realPause = NO;
    }
}

- (void) mute:(BOOL)enable
{
    if (self.player) {
        [self.player mute:enable];
        self.realMute = enable;
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
    if (_imageContext) {
        MLImageRenderRotation rotation = ML_IMAGE_NO_ROTATION;
        int rotateDegree = [EMParameter getRotateDegreeFromOrientation:oritation];
        int degree = (rotateDegree + self.player.videoRotation % 360);
        switch (degree) {
            case 0:
                break;
            case 180:
                rotation = ML_IMAGE_ROTATION_CLOCKWISE_180_DEGREE;
                break;
            case 90:
                rotation = ML_IMAGE_ROTATION_CLOCKWISE_90_DEGREE;
                break;
            case 270:
                rotation = ML_IMAGE_ROTATION_CLOCKWISE_270_DEGREE;
                break;
            default:
                break;
        }
        [_imageContext setImageViewRotation:rotation :NO];
    }
}


-(void) setRenderMode:(EM_Enum_Type_RenderMode)renderMode
{
    if (_imageContext) {
        MLImageRenderMode mlRenderMode = ML_IMAGE_RENDER_MODE_STRETCH;
        if  (renderMode == EM_RENDER_MODE_FILL_SCREEN) {
            mlRenderMode = ML_IMAGE_RENDER_MODE_PRESERVE_AR_FILL;
        } else if (renderMode == EM_RENDER_MODE_FILL_EDGE) {
            mlRenderMode = ML_IMAGE_RENDER_MODE_PRESERVE_AR;
        }else if (renderMode == EM_RENDER_MODE_BLUR_FILL_SCREEN) {
            mlRenderMode = ML_IMAGE_RENDER_MODE_PRESERVE_AR_FILL_BLUR;
        }
        [_imageContext setImageViewRenderMode:mlRenderMode];
    }
}

-(void) setPlayChannelMode:(EM_Enum_Type_Play_Channel_Mode)mode
{
    if (self.player) {
        [self.player setPlayChannelMode:mode];
    }
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
        [self checkAndApplyOptions:config];
        return [self.player changeVideoSource:addr withType:playType];
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
    if (_imageContext) {
        return [_imageContext captureViewPicture];
    } else if (self.player) {
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
    [[NSNotificationCenter defaultCenter] addObserver:self selector:@selector(natrualSizeAvalible:)
                                                 name:EMMPMovieNaturalSizeAvailableNotification object:self.player];
    [[NSNotificationCenter defaultCenter] addObserver:self selector:@selector(seekComplete:)
                                                 name:EMMPMoviePlayerDidSeekCompleteNotification object:self.player];
    [[NSNotificationCenter defaultCenter] addObserver:self selector:@selector(didVideoDecoderOpen:)
                                                 name:EMMPMoviePlayerVideoDecoderOpenNotification object:self.player];
    [[NSNotificationCenter defaultCenter] addObserver:self selector:@selector(moviePlayBackDidFinish:) name:EMMPMoviePlayerPlaybackDidFinishNotification object:self.player];
    [[NSNotificationCenter defaultCenter] addObserver:self selector:@selector(loadStateDidChange:)
                                                 name:EMMPMoviePlayerLoadStateDidChangeNotification object:self.player];
    //[[NSNotificationCenter defaultCenter] addObserver:self selector:@selector(notificationDispath:) name:IJKMPMoviePlayerScalingModeDidChangeNotification object:self.player];
    [[NSNotificationCenter defaultCenter] addObserver:self selector:@selector(moviePlayBackStateDidChange:) name:EMMPMoviePlayerPlaybackStateDidChangeNotification object:self.player];
    [[NSNotificationCenter defaultCenter] addObserver:self selector:@selector(firstAudioFrameComes:) name:EMMPMoviePlayerFirstAudioFrameRenderedNotification object:self.player];
    [[NSNotificationCenter defaultCenter] addObserver:self selector:@selector(firstVideoFrameComes:) name:EMMPMoviePlayerFirstVideoFrameRenderedNotification object:self.player];
    [[NSNotificationCenter defaultCenter] addObserver:self selector:@selector(mediaIsPreparedToPlayDidChange:) name:EMMPMediaPlaybackIsPreparedToPlayDidChangeNotification object:self.player];
    [[NSNotificationCenter defaultCenter] addObserver:self selector:@selector(openHWVideoDecoderFailed:) name:IJKMPMoviePlayerOpenHWVideoDecoderFailedNotification object:self.player];
    [[NSNotificationCenter defaultCenter] addObserver:self selector:@selector(videoPlayProgress:)
                                                 name:IJKMPMoviePlayerPlayProgressNotification object:self.player];
    [[NSNotificationCenter defaultCenter] addObserver:self selector:@selector(videoPlayStreamUnixTime:)
                                                 name:IJKMPMoviePlayerPlayStreamUnixTimeNotification object:self.player];
    [[NSNotificationCenter defaultCenter] addObserver:self selector:@selector(didTcpConnectServer:)
                                                 name:IJKMPMoviePlayerConnectSeverSuccessNotification object:self.player];
    [[NSNotificationCenter defaultCenter] addObserver:self selector:@selector(netDisconnected:)
                                                 name:IJKMPMoviePlayerNetDisconnectNotification object:self.player];
    [[NSNotificationCenter defaultCenter] addObserver:self selector:@selector(netReconnect:)
                                                 name:IJKMPMoviePlayerNetReconnectNotification object:self.player];
    [[NSNotificationCenter defaultCenter] addObserver:self selector:@selector(netForbidden:)
                                                 name:IJKMPMoviePlayerNetForbiddenNotification object:self.player];
    [[NSNotificationCenter defaultCenter] addObserver:self selector:@selector(videoPlayPrepare:)
                                                 name:EMMPMediaPlaybackPreparedToPlayNotification object:self.player];
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
    if (self.delegate && [self.delegate respondsToSelector:@selector(onNetStatus:)]){
        [self.delegate onNetStatus:netInfo];
    }
}

- (void)natrualSizeAvalible:(NSNotification *)notification
{
    //NSLog(@"natrual size avalible notification comes\n");
}

- (void)seekComplete:(NSNotification *)notification
{
    emlivesdk_log_printf(@"seek complete notification comes.");
    if (self.delegate && [self.delegate respondsToSelector:@selector(onPlayEvent:withParam:)]){
        int64_t curtimeus = time(NULL)*1000*1000;
        NSDictionary *param = [NSDictionary dictionaryWithObjectsAndKeys: [NSNumber numberWithLongLong:curtimeus], EVT_TIME, @"", EVT_MSG, nil];
        [self.delegate onPlayEvent:EM_PLAY_EVT_PLAY_SEEK_COMPLETE withParam:param];
    }
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
        MLImageRenderRotation rotation = ML_IMAGE_NO_ROTATION;
        int degree = (self.player.videoRotation % 360);
        switch (degree) {
            case 0:
                break;
            case 180:
                rotation = ML_IMAGE_ROTATION_CLOCKWISE_180_DEGREE;
                break;
            case 90:
                rotation = ML_IMAGE_ROTATION_CLOCKWISE_90_DEGREE;
                break;
            case 270:
                rotation = ML_IMAGE_ROTATION_CLOCKWISE_270_DEGREE;
                break;
            default:
                break;
        }
        [_imageContext setImageViewRotation:rotation :NO];
        int64_t curtimeus = time(NULL)*1000*1000;
        NSDictionary *param = [NSDictionary dictionaryWithObjectsAndKeys:
                               [NSNumber numberWithLongLong:curtimeus], EVT_TIME, @"EM_PLAY_EVT_RCV_FIRST_I_FRAME", EVT_MSG, nil];
        [self.delegate onPlayEvent:EM_PLAY_EVT_RCV_FIRST_I_FRAME withParam:param];
    }
    
    self.recvFirstVideoFrame = YES;
    if(self.config && !self.config.autoPlay && self.config.viewFirstVideoFrameOnPrepare){
        [self.player mute:self.realMute];
        if(!self.realPause){
            [self resume];
        }
        else{
            [self pause];
        }
        emlivesdk_log_printf(@"Restore user action, real mute:%d real pause:%d",self.realMute, self.realPause);
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

- (void)netReconnect:(NSNotification *)note
{
    emlivesdk_log_printf(@"net reconnect...");
    if (self.delegate && [self.delegate respondsToSelector:@selector(onPlayEvent:withParam:)]){
        int64_t curtimeus = time(NULL)*1000*1000;
        NSDictionary *param = [NSDictionary dictionaryWithObjectsAndKeys:
                               [NSNumber numberWithLongLong:curtimeus], EVT_TIME, @"EM_PLAY_WARNING_RECONNECT", EVT_MSG, nil];
        [self.delegate onPlayEvent:EM_PLAY_WARNING_RECONNECT withParam:param];
    }
}

- (void)netForbidden:(NSNotification *)note
{
    emlivesdk_log_printf(@"net forbidden...");
    if (self.delegate && [self.delegate respondsToSelector:@selector(onPlayEvent:withParam:)]){
        int64_t curtimeus = time(NULL)*1000*1000;
        NSDictionary *param = [NSDictionary dictionaryWithObjectsAndKeys:
                               [NSNumber numberWithLongLong:curtimeus], EVT_TIME, @"EM_PLAY_ERR_NET_FORBIDDEN", EVT_MSG, nil];
        //NSLog(@"net disconnect notification comes\n");
        [self.delegate onPlayEvent:EM_PLAY_ERR_NET_FORBIDDEN withParam:param];
    }
}

- (void)videoPlayProgress:(NSNotification *)notification
{
    NSDictionary *dict = notification.userInfo;
    if(dict == nil){
        return;
    }
    if (self.delegate && [self.delegate respondsToSelector:@selector(onPlayEvent:withParam:)]) {
        [self.delegate onPlayEvent:EM_PLAY_EVT_PLAY_PROGRESS withParam:dict];
    }
}

- (void)videoPlayPrepare:(NSNotification *)notification{
    self.recvFirstVideoFrame = NO;
    self.realMute  = NO;
    
    if(self.config && !self.config.autoPlay && self.config.viewFirstVideoFrameOnPrepare){
        self.realPause = YES;
    }
    
    NSDictionary *dict = notification.userInfo;
    if(dict == nil){
        return;
    }
    
    if (self.delegate && [self.delegate respondsToSelector:@selector(onPlayEvent:withParam:)]) {
        [self.delegate onPlayEvent:EM_PLAY_EVT_PLAY_PREPARE withParam:dict];
    }
}

- (void)videoPlayStreamUnixTime:(NSNotification *)notification
{
    NSDictionary *dict = notification.userInfo;
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
    emlivesdk_log_printf(@"openHWVideoDecoderFailed...");
    int64_t curtimeus = time(NULL)*1000*1000;
    NSDictionary *param = [NSDictionary dictionaryWithObjectsAndKeys:
                           [NSNumber numberWithLongLong:curtimeus], EVT_TIME, @"EM_PLAY_WARNING_HW_ACCELERATION_FAIL", EVT_MSG, nil];
    self.enableHWAcceleration = false;
    if (_imageBufferSource) {
        [_imageContext stopProcess];
        [_imageContext startBufferSource:ML_IMAGE_BUFFER_TYPE_RGBA32 :0 :0];
        _imageBufferSource = [_imageContext getNativeBufferSource];
    }
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
    
    NSDictionary *dict = [notification userInfo];
    int64_t curtimeus = time(NULL)*1000*1000;
    NSMutableDictionary *param = [NSMutableDictionary dictionaryWithObjectsAndKeys:
                                  [NSNumber numberWithLongLong:curtimeus], EVT_TIME, @"", EVT_MSG, nil];
    
    int playState = EM_PLAY_STATE_STOP;
    switch (_player.playbackState)
    {
        case IJKMPMoviePlaybackStateStopped: {
            playState = EM_PLAY_STATE_STOP;
            // NSLog(@"IJKMPMoviePlayBackStateDidChange %d: stoped", (int)_player.playbackState);
            break;
        }
        case IJKMPMoviePlaybackStatePlaying: {
            //NSLog(@"IJKMPMoviePlayBackStateDidChange %d: playing", (int)_player.playbackState);
            /*if (self.delegate && [self.delegate respondsToSelector:@selector(onPlayEvent:withParam:)]) {
             [self.delegate onPlayEvent:PLAY_EVT_PLAY_PROGRESS withParam:param];
             }*/
            playState = EM_PLAY_STATE_PLAYING;
            //NSLog(@"IJKMPMoviePlaybackStatePlaying %d: playing", (int)_player.playbackState);
            break;
        }
        case IJKMPMoviePlaybackStatePaused: {
            playState = EM_PLAY_STATE_PAUSE;
            //NSLog(@"IJKMPMoviePlayBackStateDidChange %d: paused", (int)_player.playbackState);
            break;
        }
        case IJKMPMoviePlaybackStateInterrupted: {
            //NSLog(@"IJKMPMoviePlayBackStateDidChange %d: interrupted", (int)_player.playbackState);
            break;
        }
        case IJKMPMoviePlaybackStateSeekingForward:
        case IJKMPMoviePlaybackStateSeekingBackward: {
            if([[dict valueForKey:@"BUFFERING_STATE"]intValue]){
                playState = EM_PLAY_STATE_PLAYING;
            }
            else{
                playState = EM_PLAY_STATE_SEEK;
            }
            //NSLog(@"IJKMPMoviePlayBackStateDidChange %d:seeking, state:%d", (int)_player.playbackState, playState);
            break;
        }
        default: {
            NSLog(@"IJKMPMoviePlayBackStateDidChange %d: unknown", (int)_player.playbackState);
            break;
        }
    }
    
    if (self.delegate && [self.delegate respondsToSelector:@selector(onPlayEvent:withParam:)]) {
        [param setValue:@(playState) forKey:EVT_PLAY_STATE];
        [self.delegate onPlayEvent:EM_PLAY_EVT_PLAY_STATE_CHANGE withParam:param];
    }
}

- (void)onVideoFrameRendering:(uint8_t *)data :(int)size :(int)width :(int)height :(int64_t)timems
{
    if (_imageBufferSource && _containView) {
        [_imageBufferSource feedInputBuffer:data :size :width :height :timems];
    }
}

- (void)onAudioFrameRendering:(uint8_t *)data :(int)size :(int)sampleRate :(int)channels :(int64_t)timems
{
    if(self.audioPCMDelegate && [self.audioPCMDelegate respondsToSelector:@selector(onPCMData:Length:SampleRate:Channels:TimeStamp:)]){
        [self.audioPCMDelegate onPCMData:data Length:size SampleRate:sampleRate Channels:channels TimeStamp:timems];
    }
}

- (void)dealloc
{
    [self removeNotification];
    NSLog(@"delloc");
}

- (void)stopLivePlay
{
    [self.player stop];
    [self.player shutdown];
}

@end
