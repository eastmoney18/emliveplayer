/*
 * EMFFMoviePlayerController.m
 *
 * Copyright (c) 2013 Zhang Rui <bbcallen@gmail.com>
 *
 * This file is part of ijkPlayer.
 *
 * ijkPlayer is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * ijkPlayer is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with ijkPlayer; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#import "IJKFFMoviePlayerController.h"
#include <mach/mach.h>
#import <UIKit/UIKit.h>
#import "IJKFFMoviePlayerDef.h"
#import "IJKMediaPlayback.h"
#import "IJKMediaModule.h"
#import "IJKAudioKit.h"
#import "IJKNotificationManager.h"
#import "NSString+IJKMedia.h"
#import "IJKSDLGLView.h"
#include "string.h"
#include "ijkplayer/version.h"
#include "NSString+IJKMedia.h"
// 状态键名定义
#define NET_STATUS_CPU_USAGE         @"CPU_USAGE"        // cpu使用率
#define NET_STATUS_VIDEO_BITRATE     @"VIDEO_BITRATE"    // 当前视频编码器输出的比特率，也就是编码器每秒生产了多少视频数据，单位 kbps
#define NET_STATUS_AUDIO_BITRATE     @"AUDIO_BITRATE"    // 当前音频编码器输出的比特率，也就是编码器每秒生产了多少音频数据，单位 kbps
#define NET_STATUS_VIDEO_FPS         @"VIDEO_FPS"        // 当前视频帧率，也就是视频编码器每条生产了多少帧画面
#define NET_STATUS_NET_SPEED         @"NET_SPEED"        // 当前的发送速度
#define NET_STATUS_NET_JITTER        @"NET_JITTER"       // 网络抖动情况，抖动越大，网络越不稳定
#define NET_STATUS_CACHE_SIZE        @"CACHE_SIZE"       // 缓冲区大小，缓冲区越大，说明当前上行带宽不足以消费掉已经生产的视频数据
#define NET_STATUS_DROP_SIZE         @"DROP_SIZE"
#define NET_STATUS_VIDEO_WIDTH       @"VIDEO_WIDTH"
#define NET_STATUS_VIDEO_HEIGHT      @"VIDEO_HEIGHT"
#define NET_STATUS_SERVER_IP         @"SERVER_IP"
#define NET_STATUS_CODEC_CACHE       @"CODEC_CACHE"      //编解码缓冲大小
#define NET_STATUS_CODEC_DROP_CNT    @"CODEC_DROP_CNT"   //编解码队列DROPCNT

static const char *kIJKFFRequiredFFmpegVersion = "ff3.1--ijk0.6.1--20160824--001";

float cpu_usage()
{
    kern_return_t kr;
    task_info_data_t tinfo;
    mach_msg_type_number_t task_info_count;
    
    task_info_count = TASK_INFO_MAX;
    kr = task_info(mach_task_self(), TASK_BASIC_INFO, (task_info_t)tinfo, &task_info_count);
    if (kr != KERN_SUCCESS) {
        return -1;
    }
    
    task_basic_info_t      basic_info;
    thread_array_t         thread_list;
    mach_msg_type_number_t thread_count;
    
    thread_info_data_t     thinfo;
    mach_msg_type_number_t thread_info_count;
    
    thread_basic_info_t basic_info_th;
    uint32_t stat_thread = 0; // Mach threads
    
    basic_info = (task_basic_info_t)tinfo;
    
    // get threads in the task
    kr = task_threads(mach_task_self(), &thread_list, &thread_count);
    if (kr != KERN_SUCCESS) {
        return -1;
    }
    if (thread_count > 0)
        stat_thread += thread_count;
    
    long tot_sec = 0;
    long tot_usec = 0;
    float tot_cpu = 0;
    int j;
    
    for (j = 0; j < (int)thread_count; j++)
    {
        thread_info_count = THREAD_INFO_MAX;
        kr = thread_info(thread_list[j], THREAD_BASIC_INFO,
                         (thread_info_t)thinfo, &thread_info_count);
        if (kr != KERN_SUCCESS) {
            return -1;
        }
        
        basic_info_th = (thread_basic_info_t)thinfo;
        
        if (!(basic_info_th->flags & TH_FLAGS_IDLE)) {
            tot_sec = tot_sec + basic_info_th->user_time.seconds + basic_info_th->system_time.seconds;
            tot_usec = tot_usec + basic_info_th->user_time.microseconds + basic_info_th->system_time.microseconds;
            tot_cpu = tot_cpu + basic_info_th->cpu_usage / (float)TH_USAGE_SCALE * 100.0;
        }
        
    } // for each thread
    
    kr = vm_deallocate(mach_task_self(), (vm_offset_t)thread_list, thread_count * sizeof(thread_t));
    //assert(kr == KERN_SUCCESS);
    
    return tot_cpu;
}

static LogBlock gLogBlock = nil;
static void emffplay_log_callback(int level, const char *buf, va_list vl)
{
    if(gLogBlock != nil){
        NSString *format = [[NSString alloc] initWithUTF8String:buf];
        NSString *content = [[NSString alloc]initWithFormat:format arguments:vl];
        gLogBlock(level, content);
    }
}

@interface EMFFMoviePlayerController()

@property (atomic, assign) BOOL isPause;
@property (atomic, assign) BOOL isStandby;

@end

@implementation EMFFMoviePlayerController {
    EMMediaPlayer *_mediaPlayer;
    IJKSDLGLView *_glView;
    IJKFFMoviePlayerMessagePool *_msgPool;
    NSString *_urlString;

    NSInteger _videoWidth;
    NSInteger _videoHeight;
    NSInteger _sampleAspectRatioNumerator;
    NSInteger _sampleAspectRatioDenominator;

    BOOL      _seeking;
    NSInteger _bufferingTime;
    NSInteger _bufferingPosition;

    BOOL _keepScreenOnWhilePlaying;
    BOOL _pauseInBackground;
    BOOL _isVideoToolboxOpen;
    BOOL _playingBeforeInterruption;

    IJKNotificationManager *_notificationManager;

    AVAppAsyncStatistic _asyncStat;
    BOOL _shouldShowHudView;
    NSTimer *_hudTimer;
    
    IJKFFOptions *_options;
    
    dispatch_queue_t _operation_queue;
}

@synthesize view = _view;
@synthesize currentPlaybackTime;
@synthesize duration;
@synthesize playableDuration;
@synthesize bufferingProgress = _bufferingProgress;
@synthesize numberOfBytesTransferred = _numberOfBytesTransferred;

@synthesize isPreparedToPlay = _isPreparedToPlay;
@synthesize playbackState = _playbackState;
@synthesize loadState = _loadState;

@synthesize naturalSize = _naturalSize;
@synthesize scalingMode = _scalingMode;
@synthesize shouldAutoplay = _shouldAutoplay;

@synthesize allowsMediaAirPlay = _allowsMediaAirPlay;
@synthesize airPlayMediaActive = _airPlayMediaActive;

@synthesize isDanmakuMediaAirPlay = _isDanmakuMediaAirPlay;

@synthesize monitor = _monitor;



#define FFP_IO_STAT_STEP (50 * 1024)

// as an example
void EMFFIOStatDebugCallback(const char *url, int type, int bytes)
{
    static int64_t s_ff_io_stat_check_points = 0;
    static int64_t s_ff_io_stat_bytes = 0;
    if (!url)
        return;

    if (type != IJKMP_IO_STAT_READ)
        return;

    if (!av_em_strstart(url, "http:", NULL))
        return;

    s_ff_io_stat_bytes += bytes;
    if (s_ff_io_stat_bytes < s_ff_io_stat_check_points ||
        s_ff_io_stat_bytes > s_ff_io_stat_check_points + FFP_IO_STAT_STEP) {
        s_ff_io_stat_check_points = s_ff_io_stat_bytes;
        ALOGI("io-stat: %s, +%d = %"PRId64"\n", url, bytes, s_ff_io_stat_bytes);
    }
}

void EMFFIOStatRegister(void (*cb)(const char *url, int type, int bytes))
{
    emmp_io_stat_register(cb);
}

void EMFFIOStatCompleteDebugCallback(const char *url,
                                      int64_t read_bytes, int64_t total_size,
                                      int64_t elpased_time, int64_t total_duration)
{
    if (!url)
        return;

    if (!av_em_strstart(url, "http:", NULL))
        return;

    ALOGI("io-stat-complete: %s, %"PRId64"/%"PRId64", %"PRId64"/%"PRId64"\n",
          url, read_bytes, total_size, elpased_time, total_duration);
}

void EMFFIOStatCompleteRegister(void (*cb)(const char *url,
                                            int64_t read_bytes, int64_t total_size,
                                            int64_t elpased_time, int64_t total_duration))
{
    emmp_io_stat_complete_register(cb);
}

- (id)initWithContentURL:(NSURL *)aUrl
             withOptions:(IJKFFOptions *)options
            displayVideo:(BOOL)display
{
    if (aUrl == nil)
        return nil;

    // Detect if URL is file path and return proper string for it
    NSString *aUrlString = [aUrl isFileURL] ? [aUrl path] : [aUrl absoluteString];

    return [self initWithContentURLString:aUrlString
                              withOptions:options displayVideo:display];
}

- (id)initWithContentURLString:(NSString *)aUrlString
                   withOptions:(IJKFFOptions *)options
                  displayVideo:(BOOL)display
{
    if (aUrlString == nil)
        return nil;

    self = [super init];
    if (self) {
        _videoDisplay = display;
        emmp_global_init();
        emmp_global_set_inject_callback(ijkff_inject_callback);
        [EMFFMoviePlayerController checkIfFFmpegVersionMatch:NO];

        if (options == nil)
            options = [IJKFFOptions optionsByDefault];

        _options = options;
        // EMFFIOStatRegister(EMFFIOStatDebugCallback);
        // EMFFIOStatCompleteRegister(EMFFIOStatCompleteDebugCallback);

        // init fields
        _scalingMode = IJKMPMovieScalingModeAspectFit;
        _shouldAutoplay = YES;
        memset(&_asyncStat, 0, sizeof(_asyncStat));

        _monitor = [[IJKFFMonitor alloc] init];

        // init media resource
        _urlString = aUrlString;

        // init player
        _mediaPlayer = emmp_ios_create(em_media_player_msg_loop);
        _msgPool = [[IJKFFMoviePlayerMessagePool alloc] init];
        
        _isPause = NO;
        _isStandby = NO;

        emmp_set_weak_thiz(_mediaPlayer, (__bridge_retained void *) self);
        emmp_set_inject_opaque(_mediaPlayer, (__bridge_retained void *) self);
        if (!display) {
            emmp_set_video_frame_present_callback(_mediaPlayer, onPresentVideoFrame);
        }
        emmp_set_audio_frame_present_callback(_mediaPlayer, onPresentAudioFrame);
        emmp_set_option_int(_mediaPlayer, IJKMP_OPT_CATEGORY_PLAYER, "start-on-prepared", _shouldAutoplay ? 1 : 0);
        // init video sink
        if (_videoDisplay) {
            if (_glView == nil) {
                _glView = [[IJKSDLGLView alloc] initWithFrame:[[UIScreen mainScreen] bounds]];
            }
            _glView.shouldShowHudView = NO;
            _view   = _glView;
            [_glView setHudValue:nil forKey:@"scheme"];
            [_glView setHudValue:nil forKey:@"host"];
            [_glView setHudValue:nil forKey:@"path"];
            [_glView setHudValue:nil forKey:@"ip"];
            [_glView setHudValue:nil forKey:@"tcp-info"];
            [_glView setHudValue:nil forKey:@"http"];
            [_glView setHudValue:nil forKey:@"tcp-spd"];
            [_glView setHudValue:nil forKey:@"t-prepared"];
            [_glView setHudValue:nil forKey:@"t-render"];
            [_glView setHudValue:nil forKey:@"t-preroll"];
            [_glView setHudValue:nil forKey:@"t-http-open"];
            [_glView setHudValue:nil forKey:@"t-http-seek"];
            self.shouldShowHudView = options.showHudView;
            
            emmp_ios_set_glview(_mediaPlayer, _glView);
            emmp_set_option(_mediaPlayer, IJKMP_OPT_CATEGORY_PLAYER, "overlay-format", "fcc-_es2");
            
            // init extra
            _keepScreenOnWhilePlaying = YES;
            [self setScreenOn:YES];
        }
        

#ifdef DEBUG
 //       [EMFFMoviePlayerController setLogLevel:k_IJK_LOG_DEBUG];
#else
        //[EMFFMoviePlayerController setLogLevel:k_IJK_LOG_SILENT];
#endif
        // init audio sink
        //[[IJKAudioKit sharedInstance] setupAudioSession];

        [options applyTo:_mediaPlayer];
        _pauseInBackground = NO;

        _notificationManager = [[IJKNotificationManager alloc] init];
        [self registerApplicationObservers];
        [self initNetStatInfo];
        _operation_queue = dispatch_queue_create(nil, nil);
    }
    return self;
}

- (void)setVideoDisplay:(BOOL)videoDisplay
{
    _videoDisplay = videoDisplay;
    if (videoDisplay) {
        emmp_set_video_frame_present_callback(_mediaPlayer, nil);
    } else {
        emmp_set_video_frame_present_callback(_mediaPlayer, onPresentVideoFrame);
    }
    if (videoDisplay && !_glView) {
        _glView = [[IJKSDLGLView alloc] initWithFrame:[[UIScreen mainScreen] bounds]];
        _glView.shouldShowHudView = NO;
        _view   = _glView;
        [_glView setHudValue:nil forKey:@"scheme"];
        [_glView setHudValue:nil forKey:@"host"];
        [_glView setHudValue:nil forKey:@"path"];
        [_glView setHudValue:nil forKey:@"ip"];
        [_glView setHudValue:nil forKey:@"tcp-info"];
        [_glView setHudValue:nil forKey:@"http"];
        [_glView setHudValue:nil forKey:@"tcp-spd"];
        [_glView setHudValue:nil forKey:@"t-prepared"];
        [_glView setHudValue:nil forKey:@"t-render"];
        [_glView setHudValue:nil forKey:@"t-preroll"];
        [_glView setHudValue:nil forKey:@"t-http-open"];
        [_glView setHudValue:nil forKey:@"t-http-seek"];
        [_glView setupRotateMode:(_rorationDegree + _videoRotation) % 360];
        self.shouldShowHudView = _options.showHudView;
        emmp_ios_set_glview(_mediaPlayer, _glView);
        emmp_set_option(_mediaPlayer, IJKMP_OPT_CATEGORY_PLAYER, "overlay-format", "fcc-_es2");
        
        // init extra
        _keepScreenOnWhilePlaying = YES;
        [self setScreenOn:YES];
        
    } else if (!videoDisplay && _glView){
        _glView = nil;
        _view = nil;
    }
}


- (void)initNetStatInfo
{
    self.netStatInfo = [NSMutableDictionary dictionaryWithObjectsAndKeys:@20, NET_STATUS_CPU_USAGE,
                        @0, NET_STATUS_VIDEO_BITRATE,
                        @0, NET_STATUS_AUDIO_BITRATE,
                        @0, NET_STATUS_VIDEO_FPS,
                        @0, NET_STATUS_NET_SPEED,
                        @0, NET_STATUS_NET_JITTER,
                        @0, NET_STATUS_CACHE_SIZE,
                        @0, NET_STATUS_DROP_SIZE,
                        @0, NET_STATUS_VIDEO_WIDTH,
                        @0, NET_STATUS_VIDEO_HEIGHT,
                        @"0.0.0.0", NET_STATUS_SERVER_IP,
                        @0, NET_STATUS_CODEC_CACHE,
                        @0, NET_STATUS_CODEC_DROP_CNT, nil];
}

- (void)setScreenOn: (BOOL)on
{
    [IJKMediaModule sharedModule].mediaModuleIdleTimerDisabled = on;
    // [UIApplication sharedApplication].idleTimerDisabled = on;
}

- (void)dealloc
{
    NSLog(@"ffmovie player deaaloc");
//    [self unregisterApplicationObservers];
}

- (void)setShouldAutoplay:(BOOL)shouldAutoplay
{
    _shouldAutoplay = shouldAutoplay;

    if (!_mediaPlayer)
        return;

    emmp_set_option_int(_mediaPlayer, IJKMP_OPT_CATEGORY_PLAYER, "start-on-prepared", _shouldAutoplay ? 1 : 0);
}

- (BOOL)shouldAutoplay
{
    return _shouldAutoplay;
}

- (void)prepareToPlay
{
    if (!_mediaPlayer)
        return;

    [self setScreenOn:_keepScreenOnWhilePlaying];

    emmp_set_data_source(_mediaPlayer, [_urlString UTF8String]);
    emmp_set_option(_mediaPlayer, IJKMP_OPT_CATEGORY_FORMAT, "safe", "0"); // for concat demuxer

    _monitor.prepareStartTick = (int64_t)SDL_GetTickHR();
    emmp_prepare_async(_mediaPlayer);
}

- (void)prepareToPlay:(int)playType
{
    if (!_mediaPlayer)
        return;
    
    [self setScreenOn:_keepScreenOnWhilePlaying];
    emmp_set_data_source(_mediaPlayer, [_urlString UTF8String]);
    emmp_set_option(_mediaPlayer, IJKMP_OPT_CATEGORY_FORMAT, "safe", "0"); // for concat demuxer
    
    _monitor.prepareStartTick = (int64_t)SDL_GetTickHR();
    emmp_set_play_mode(_mediaPlayer, playType);
    emmp_prepare_async(_mediaPlayer);

}

- (void)setHudUrl:(NSString *)urlString
{
    __weak __typeof(&*self)weakself = self;
    if ([[NSThread currentThread] isMainThread]) {
        NSURL *url = [NSURL URLWithString:urlString];
        [_glView setHudValue:url.scheme forKey:@"scheme"];
        [_glView setHudValue:url.host   forKey:@"host"];
        [_glView setHudValue:url.path   forKey:@"path"];
    } else {
        dispatch_async(dispatch_get_main_queue(), ^{
            [weakself setHudUrl:urlString];
        });
    }
}

- (void)play
{
    self.isPause = NO;
    self.isStandby = NO;
    if (!_mediaPlayer)
        return;

    [self setScreenOn:_keepScreenOnWhilePlaying];

    [self startHudTimer];
    emmp_start(_mediaPlayer);
}

- (void)pause
{
    self.isPause = YES;
    if (!_mediaPlayer)
        return;

//    [self stopHudTimer];
    emmp_pause(_mediaPlayer);
}

- (void)stop
{
    if (!_mediaPlayer)
        return;

    [self setScreenOn:NO];
    [self stopHudTimer];
    emmp_stop(_mediaPlayer);
}

- (void)setPlayChannelMode:(int)mode
{
    if (!_mediaPlayer)
        return;
    emmp_set_play_channel_mode(_mediaPlayer, mode);
}

- (BOOL)isPlaying
{
    if (!_mediaPlayer)
        return NO;

    return emmp_is_playing(_mediaPlayer);
}

- (void)standby
{
    self.isStandby = YES;
    if (_mediaPlayer) {
        emmp_standby(_mediaPlayer);
    }
}

- (void)setLoop:(int)loopCount
{
    if (_mediaPlayer) {
        emmp_set_loop(_mediaPlayer, loopCount);
    }
}

- (void)mute:(BOOL)enable
{
    if (_mediaPlayer) {
        emmp_mute(_mediaPlayer, enable ? 1: 0);
    }
}

- (int)changeVideoSource:(NSString *)addr withType:(int)playType;
{
    if (!_mediaPlayer)
        return -1;
    self.isPause = NO;
    self.isStandby = NO;
    return emmp_change_video_source(_mediaPlayer, (char *)[addr UTF8String], playType);
}

- (int)prepareNewVideoSource:(NSString *)addr withType:(int)playType;
{
    if (!_mediaPlayer)
        return -1;
    return emmp_prepare_new_video_source(_mediaPlayer, (char *)[addr UTF8String], playType);
}

- (int)changeVideoSourceWithPreparedIndex:(int)index
{
    if (!_mediaPlayer)
        return -1;
    self.isPause = NO;
    self.isStandby = NO;
    return emmp_change_video_source_with_prepared_index(_mediaPlayer, index);
}

- (int)deletePreparedVideoSource:(int)index
{
    if (!_mediaPlayer)
        return -1;
    return emmp_delete_prepared_video_source(_mediaPlayer, index);
}

- (void)setPauseInBackground:(BOOL)pause
{
    _pauseInBackground = pause;
}

- (BOOL)isVideoToolboxOpen
{
    if (!_mediaPlayer)
        return NO;

    return _isVideoToolboxOpen;
}

inline static int getPlayerOption(IJKFFOptionCategory category)
{
    int mp_category = -1;
    switch (category) {
        case kIJKFFOptionCategoryFormat:
            mp_category = IJKMP_OPT_CATEGORY_FORMAT;
            break;
        case kIJKFFOptionCategoryCodec:
            mp_category = IJKMP_OPT_CATEGORY_CODEC;
            break;
        case kIJKFFOptionCategorySws:
            mp_category = IJKMP_OPT_CATEGORY_SWS;
            break;
        case kIJKFFOptionCategoryPlayer:
            mp_category = IJKMP_OPT_CATEGORY_PLAYER;
            break;
        default:
            ALOGI("unknown option category: %d\n", category);
    }
    return mp_category;
}

- (void)setOptionValue:(NSString *)value
                forKey:(NSString *)key
            ofCategory:(IJKFFOptionCategory)category
{
    assert(_mediaPlayer);
    if (!_mediaPlayer)
        return;

    emmp_set_option(_mediaPlayer, getPlayerOption(category), [key UTF8String], [value UTF8String]);
}

- (void)setOptionIntValue:(int64_t)value
                   forKey:(NSString *)key
               ofCategory:(IJKFFOptionCategory)category
{
    assert(_mediaPlayer);
    if (!_mediaPlayer)
        return;

    emmp_set_option_int(_mediaPlayer, getPlayerOption(category), [key UTF8String], value);
}

+ (void)setLogReport:(BOOL)preferLogReport
{
    emmp_global_set_log_report(preferLogReport ? 1 : 0);
}

+ (void)setLogLevel:(IJKLogLevel)logLevel
{
    emmp_global_set_log_level(logLevel);
}

+ (void)setLogOutput:(LogBlock)block
{
    gLogBlock = block;
    emmp_global_set_log_callback(emffplay_log_callback);
}

+ (BOOL)checkIfFFmpegVersionMatch:(BOOL)showAlert;
{
    const char *actualVersion = av_em_version_info();
    const char *expectVersion = kIJKFFRequiredFFmpegVersion;
    if (0 == strcmp(actualVersion, expectVersion)) {
        return YES;
    } else {
        NSString *message = [NSString stringWithFormat:@"actual: %s\n expect: %s\n", actualVersion, expectVersion];
        ALOGW("\n!!!!!!!!!!\n%@\n!!!!!!!!!!\n", message);
        if (showAlert) {
            UIAlertView *alertView = [[UIAlertView alloc] initWithTitle:@"Unexpected FFmpeg version"
                                                                message:message
                                                               delegate:nil
                                                      cancelButtonTitle:@"OK"
                                                      otherButtonTitles:nil];
            [alertView show];
        }
        return NO;
    }
}

+ (BOOL)checkIfPlayerVersionMatch:(BOOL)showAlert
                            major:(unsigned int)major
                            minor:(unsigned int)minor
                            micro:(unsigned int)micro
{
    unsigned int actualVersion = emmp_version_int();
    if (actualVersion == AV_VERSION_INT(major, minor, micro)) {
        return YES;
    } else {
        if (showAlert) {
            NSString *message = [NSString stringWithFormat:@"actual: %s\n expect: %d.%d.%d\n",
                                 emmp_version_ident(), major, minor, micro];
            UIAlertView *alertView = [[UIAlertView alloc] initWithTitle:@"Unexpected ijkplayer version"
                                                                message:message
                                                               delegate:nil
                                                      cancelButtonTitle:@"OK"
                                                      otherButtonTitles:nil];
            [alertView show];
        }
        return NO;
    }
}

- (void)shutdown
{
    if (!_mediaPlayer)
        return;

    [self stopHudTimer];
    [self unregisterApplicationObservers];
    [self setScreenOn:NO];
    __weak typeof(self) _weakself = self;
    dispatch_async(_operation_queue, ^() {
        [_weakself shutdownWaitStop:_weakself];
    });
}

- (void)shutdownWaitStop:(EMFFMoviePlayerController *) mySelf
{
    if (!_mediaPlayer)
        return;

    emmp_stop(_mediaPlayer);
    emmp_shutdown(_mediaPlayer);
    EMFFMoviePlayerController * pself = (__bridge_transfer EMFFMoviePlayerController *)emmp_set_inject_opaque(_mediaPlayer, NULL);
    pself = nil;
    emmp_dec_ref_p(&_mediaPlayer);
    [self performSelectorOnMainThread:@selector(shutdownClose:) withObject:self waitUntilDone:YES];
}

- (void)shutdownClose:(EMFFMoviePlayerController *) mySelf
{
    _segmentOpenDelegate    = nil;
    _tcpOpenDelegate        = nil;
    _httpOpenDelegate       = nil;
    _liveOpenDelegate       = nil;
    _nativeInvokeDelegate   = nil;
    _operation_queue = nil;
    [self didShutdown];
}

- (void)didShutdown
{
    NSLog(@"did shut down");
}

- (IJKMPMoviePlaybackState)playbackState
{
    if (!_mediaPlayer)
        return NO;

    IJKMPMoviePlaybackState mpState = IJKMPMoviePlaybackStateStopped;
    int state = emmp_get_state(_mediaPlayer);
    switch (state) {
        case MP_STATE_STOPPED:
        case MP_STATE_COMPLETED:
        case MP_STATE_ERROR:
        case MP_STATE_END:
            mpState = IJKMPMoviePlaybackStateStopped;
            break;
        case MP_STATE_IDLE:
        case MP_STATE_INITIALIZED:
        case MP_STATE_ASYNC_PREPARING:
        case MP_STATE_PAUSED:
            mpState = IJKMPMoviePlaybackStatePaused;
            break;
        case MP_STATE_PREPARED:
        case MP_STATE_STARTED: {
            if (_seeking)
                mpState = IJKMPMoviePlaybackStateSeekingForward;
            else
                mpState = IJKMPMoviePlaybackStatePlaying;
            break;
        }
    }
    // IJKMPMoviePlaybackStatePlaying,
    // IJKMPMoviePlaybackStatePaused,
    // IJKMPMoviePlaybackStateStopped,
    // IJKMPMoviePlaybackStateInterrupted,
    // IJKMPMoviePlaybackStateSeekingForward,
    // IJKMPMoviePlaybackStateSeekingBackward
    return mpState;
}

- (void)setCurrentPlaybackTime:(NSTimeInterval)aCurrentPlaybackTime
{
    if (!_mediaPlayer)
        return;

    _seeking = YES;
    NSDictionary *dic = [NSDictionary dictionaryWithObjectsAndKeys:@(0), @"BUFFERING_STATE", nil];
    [[NSNotificationCenter defaultCenter]
     postNotificationName:EMMPMoviePlayerPlaybackStateDidChangeNotification
     object:self userInfo:dic];

    _bufferingPosition = 0;
    emmp_seek_to(_mediaPlayer, aCurrentPlaybackTime * 1000);
}

- (NSTimeInterval)currentPlaybackTime
{
    if (!_mediaPlayer)
        return 0.0f;

    NSTimeInterval ret = emmp_get_current_position(_mediaPlayer);
    if (isnan(ret) || isinf(ret))
        return -1;

    return ret / 1000;
}

- (NSTimeInterval)duration
{
    if (!_mediaPlayer)
        return 0.0f;

    NSTimeInterval ret = emmp_get_duration(_mediaPlayer);
    if (isnan(ret) || isinf(ret))
        return -1;

    return ret / 1000;
}

- (NSTimeInterval)playableDuration
{
    if (!_mediaPlayer)
        return 0.0f;

    NSTimeInterval demux_cache = ((NSTimeInterval)emmp_get_playable_duration(_mediaPlayer)) / 1000;

    int64_t buf_forwards = _asyncStat.buf_forwards;
    if (buf_forwards > 0) {
        int64_t bit_rate = emmp_get_property_int64(_mediaPlayer, FFP_PROP_INT64_BIT_RATE, 0);
        if (bit_rate > 0) {
            NSTimeInterval io_cache = ((float)buf_forwards) * 8 / bit_rate;
            return io_cache + demux_cache;
        }
    }

    return demux_cache;
}

- (CGSize)naturalSize
{
    return _naturalSize;
}

- (void)changeNaturalSize
{
    [self willChangeValueForKey:@"naturalSize"];
    if (_sampleAspectRatioNumerator > 0 && _sampleAspectRatioDenominator > 0) {
        self->_naturalSize = CGSizeMake(1.0f * _videoWidth * _sampleAspectRatioNumerator / _sampleAspectRatioDenominator, _videoHeight);
    } else {
        self->_naturalSize = CGSizeMake(_videoWidth, _videoHeight);
    }
    [self didChangeValueForKey:@"naturalSize"];

    if (self->_naturalSize.width > 0 && self->_naturalSize.height > 0) {
        [[NSNotificationCenter defaultCenter]
         postNotificationName:EMMPMovieNaturalSizeAvailableNotification
         object:self];
    }
}

- (void)setScalingMode: (IJKMPMovieScalingMode) aScalingMode
{
    IJKMPMovieScalingMode newScalingMode = aScalingMode;
    switch (aScalingMode) {
        case IJKMPMovieScalingModeNone:
            [_view setContentMode:UIViewContentModeCenter];
            break;
        case IJKMPMovieScalingModeAspectFit:
            [_view setContentMode:UIViewContentModeScaleAspectFit];
            break;
        case IJKMPMovieScalingModeAspectFill:
            [_view setContentMode:UIViewContentModeScaleAspectFill];
            break;
        case IJKMPMovieScalingModeFill:
            [_view setContentMode:UIViewContentModeScaleToFill];
            break;
        default:
            newScalingMode = _scalingMode;
    }

    _scalingMode = newScalingMode;
}

- (void)setRotateMode: (int)degree;
{
    _rorationDegree = degree;
    [_glView setupRotateMode:(_videoRotation + _rorationDegree) % 360];
}

// deprecated, for MPMoviePlayerController compatiable
- (UIImage *)thumbnailImageAtTime:(NSTimeInterval)playbackTime timeOption:(IJKMPMovieTimeOption)option
{
    return nil;
}

- (UIImage *)thumbnailImageAtCurrentTime
{
    if ([_view isKindOfClass:[IJKSDLGLView class]]) {
        IJKSDLGLView *glView = (IJKSDLGLView *)_view;
        return [glView snapshot];
    }

    return nil;
}

- (CGFloat)fpsAtOutput
{
    return _glView.fps;
}

inline static NSString *formatedDurationMilli(int64_t duration) {
    if (duration >=  1000) {
        return [NSString stringWithFormat:@"%.2f sec", ((float)duration) / 1000];
    } else {
        return [NSString stringWithFormat:@"%ld msec", (long)duration];
    }
}

inline static NSString *formatedDurationBytesAndBitrate(int64_t bytes, int64_t bitRate) {
    if (bitRate <= 0) {
        return @"inf";
    }
    return formatedDurationMilli(((float)bytes) * 8 * 1000 / bitRate);
}

inline static NSString *formatedSize(int64_t bytes) {
    if (bytes >= 100 * 1000) {
        return [NSString stringWithFormat:@"%.2f MB", ((float)bytes) / 1000 / 1000];
    } else if (bytes >= 100) {
        return [NSString stringWithFormat:@"%.1f KB", ((float)bytes) / 1000];
    } else {
        return [NSString stringWithFormat:@"%ld B", (long)bytes];
    }
}

inline static NSString *formatedSpeed(int64_t bytes, int64_t elapsed_milli) {
    if (elapsed_milli <= 0) {
        return @"N/A";
    }

    if (bytes <= 0) {
        return @"0";
    }

    float bytes_per_sec = ((float)bytes) * 1000.f /  elapsed_milli;
    if (bytes_per_sec >= 1000 * 1000) {
        return [NSString stringWithFormat:@"%.2f MB/s", ((float)bytes_per_sec) / 1000 / 1000];
    } else if (bytes_per_sec >= 1000) {
        return [NSString stringWithFormat:@"%.1f KB/s", ((float)bytes_per_sec) / 1000];
    } else {
        return [NSString stringWithFormat:@"%ld B/s", (long)bytes_per_sec];
    }
}

- (void)refreshNetInfo
{
    if (_mediaPlayer == nil) {
        return;
    }
    float   vfps = emmp_get_property_float(_mediaPlayer, FFP_PROP_FLOAT_VIDEO_OUTPUT_FRAMES_PER_SECOND, .0f);
    [self.netStatInfo setObject:[NSNumber numberWithFloat:vfps] forKey:NET_STATUS_VIDEO_FPS];
    int64_t vcacheb = emmp_get_property_int64(_mediaPlayer, FFP_PROP_INT64_VIDEO_CACHED_BYTES, 0);
    int64_t acacheb = emmp_get_property_int64(_mediaPlayer, FFP_PROP_INT64_AUDIO_CACHED_BYTES, 0);
    [self.netStatInfo setObject:[NSNumber numberWithLongLong:(vcacheb+acacheb)/1000] forKey:NET_STATUS_CODEC_CACHE];
    int64_t tcpSpeed = emmp_get_property_int64(_mediaPlayer, FFP_PROP_INT64_TCP_SPEED, 0);
    [self.netStatInfo setObject:[NSNumber numberWithLongLong:tcpSpeed*8/1000] forKey:NET_STATUS_NET_SPEED];
    int64_t bitRate = emmp_get_property_int64(_mediaPlayer, FFP_PROP_INT64_VIDEO_BITRATE, 0);
    [self.netStatInfo setObject:[NSNumber numberWithLongLong:bitRate*8/1000] forKey:NET_STATUS_VIDEO_BITRATE];
    bitRate = emmp_get_property_int64(_mediaPlayer, FFP_PROP_INT64_AUDIO_BITRATE, 0);
    [self.netStatInfo setObject:[NSNumber numberWithLongLong:bitRate*8/1000] forKey:NET_STATUS_AUDIO_BITRATE];
    [self.netStatInfo setObject:[NSNumber numberWithLongLong:cpu_usage()] forKey:NET_STATUS_CPU_USAGE];
    //NSLog(@"netinfo:%@\n", self.netStatInfo);
    if (self.nativeInvokeDelegate && [self.nativeInvokeDelegate respondsToSelector:@selector(NetInfoStatus:)]){
        [self.nativeInvokeDelegate NetInfoStatus:self.netStatInfo];
    }
}

- (void)refreshHudView
{
    if (_mediaPlayer == nil)
        return;

    int64_t vdec = emmp_get_property_int64(_mediaPlayer, FFP_PROP_INT64_VIDEO_DECODER, FFP_PROPV_DECODER_UNKNOWN);
    float   vdps = emmp_get_property_float(_mediaPlayer, FFP_PROP_FLOAT_VIDEO_DECODE_FRAMES_PER_SECOND, .0f);
    float   vfps = emmp_get_property_float(_mediaPlayer, FFP_PROP_FLOAT_VIDEO_OUTPUT_FRAMES_PER_SECOND, .0f);
    switch (vdec) {
        case FFP_PROPV_DECODER_VIDEOTOOLBOX:
            [_glView setHudValue:@"VideoToolbox" forKey:@"vdec"];
            break;
        case FFP_PROPV_DECODER_AVCODEC:
            [_glView setHudValue:[NSString stringWithFormat:@"avcodec %d.%d.%d",
                                  LIBavcodec_em_version_MAJOR,
                                  LIBavcodec_em_version_MINOR,
                                  LIBavcodec_em_version_MICRO]
                          forKey:@"vdec"];
            break;
        default:
            [_glView setHudValue:@"N/A" forKey:@"vdec"];
            break;
    }

    [_glView setHudValue:[NSString stringWithFormat:@"%.2f / %.2f", vdps, vfps] forKey:@"fps"];

    int64_t vcacheb = emmp_get_property_int64(_mediaPlayer, FFP_PROP_INT64_VIDEO_CACHED_BYTES, 0);
    int64_t acacheb = emmp_get_property_int64(_mediaPlayer, FFP_PROP_INT64_AUDIO_CACHED_BYTES, 0);
    int64_t vcached = emmp_get_property_int64(_mediaPlayer, FFP_PROP_INT64_VIDEO_CACHED_DURATION, 0);
    int64_t acached = emmp_get_property_int64(_mediaPlayer, FFP_PROP_INT64_AUDIO_CACHED_DURATION, 0);
    int64_t vcachep = emmp_get_property_int64(_mediaPlayer, FFP_PROP_INT64_VIDEO_CACHED_PACKETS, 0);
    int64_t acachep = emmp_get_property_int64(_mediaPlayer, FFP_PROP_INT64_AUDIO_CACHED_PACKETS, 0);
    [_glView setHudValue:[NSString stringWithFormat:@"%@, %@, %"PRId64" packets",
                          formatedDurationMilli(vcached),
                          formatedSize(vcacheb),
                          vcachep]
                  forKey:@"v-cache"];
    [_glView setHudValue:[NSString stringWithFormat:@"%@, %@, %"PRId64" packets",
                          formatedDurationMilli(acached),
                          formatedSize(acacheb),
                          acachep]
                  forKey:@"a-cache"];
    //[self.netStatInfo setValue:[NSNumber numberWithLongLong:vcacheb+acacheb] forKey:NET_STATUS_CODEC_CACHE];
    float avdelay = emmp_get_property_float(_mediaPlayer, FFP_PROP_FLOAT_AVDELAY, .0f);
    float avdiff  = emmp_get_property_float(_mediaPlayer, FFP_PROP_FLOAT_AVDIFF, .0f);
    [_glView setHudValue:[NSString stringWithFormat:@"%.3f %.3f", avdelay, -avdiff] forKey:@"delay"];

    int64_t bitRate = emmp_get_property_int64(_mediaPlayer, FFP_PROP_INT64_BIT_RATE, 0);
    [_glView setHudValue:[NSString stringWithFormat:@"-%@, %@",
                          formatedSize(_asyncStat.buf_backwards),
                          formatedDurationBytesAndBitrate(_asyncStat.buf_backwards, bitRate)]
                  forKey:@"async-backward"];
    [_glView setHudValue:[NSString stringWithFormat:@"+%@, %@",
                          formatedSize(_asyncStat.buf_forwards),
                          formatedDurationBytesAndBitrate(_asyncStat.buf_forwards, bitRate)]
                  forKey:@"async-forward"];

    int64_t tcpSpeed = emmp_get_property_int64(_mediaPlayer, FFP_PROP_INT64_TCP_SPEED, 0);
    //[self.netStatInfo setValue:[NSNumber numberWithLongLong:tcpSpeed] forKey:NET_STATUS_NET_SPEED];
    [_glView setHudValue:[NSString stringWithFormat:@"%@", formatedSpeed(tcpSpeed, 1000)]
                  forKey:@"tcp-spd"];

    [_glView setHudValue:formatedDurationMilli(_monitor.prepareDuration) forKey:@"t-prepared"];
    [_glView setHudValue:formatedDurationMilli(_monitor.firstVideoFrameLatency) forKey:@"t-render"];
    [_glView setHudValue:formatedDurationMilli(_monitor.lastPrerollDuration) forKey:@"t-preroll"];
    [_glView setHudValue:[NSString stringWithFormat:@"%@ / %d",
                          formatedDurationMilli(_monitor.lastHttpOpenDuration),
                          _monitor.httpOpenCount]
                  forKey:@"t-http-open"];
    [_glView setHudValue:[NSString stringWithFormat:@"%@ / %d",
                          formatedDurationMilli(_monitor.lastHttpSeekDuration),
                          _monitor.httpSeekCount]
                  forKey:@"t-http-seek"];
}

- (void)startHudTimer
{
    /*if (!_shouldShowHudView)
        return;*/

    if (_hudTimer != nil)
        return;
    __weak __typeof(&*self)weakself = self;
    if ([[NSThread currentThread] isMainThread]) {
        if (_shouldShowHudView) {
            _glView.shouldShowHudView = YES;
        }
        _hudTimer = [NSTimer scheduledTimerWithTimeInterval:.5f
                                                     target:self
                                                   selector:@selector(refreshNetInfoAndHUD)
                                                   userInfo:nil
                                                    repeats:YES];
    } else {
        dispatch_async(dispatch_get_main_queue(), ^{
            [weakself startHudTimer];
        });
    }
}

- (void)refreshNetInfoAndHUD
{
    if (_shouldShowHudView) {
        [self refreshHudView];
    }
    [self refreshNetInfo];
}

- (void)stopHudTimer
{
    if (_hudTimer == nil)
        return;

    if ([[NSThread currentThread] isMainThread]) {
        _glView.shouldShowHudView = NO;
        [_hudTimer invalidate];
        _hudTimer = nil;
    } else {
        __weak __typeof(&*self)weakSelf = self;
        dispatch_async(dispatch_get_main_queue(), ^{
            [weakSelf stopHudTimer];
        });
    }
}

- (void)setShouldShowHudView:(BOOL)shouldShowHudView
{
    if (shouldShowHudView == _shouldShowHudView) {
        return;
    }
    _shouldShowHudView = shouldShowHudView;
    if (shouldShowHudView)
        [self startHudTimer];
    else
        [self stopHudTimer];
}

- (BOOL)shouldShowHudView
{
    return _shouldShowHudView;
}

- (void)setPlaybackRate:(float)playbackRate
{
    if (!_mediaPlayer)
        return;

    return emmp_set_playback_rate(_mediaPlayer, playbackRate);
}

- (float)playbackRate
{
    if (!_mediaPlayer)
        return 0.0f;

    return emmp_get_property_float(_mediaPlayer, FFP_PROP_FLOAT_PLAYBACK_RATE, 0.0f);
}

inline static void fillMetaInternal(NSMutableDictionary *meta, IjkMediaMeta *rawMeta, const char *name, NSString *defaultValue)
{
    if (!meta || !rawMeta || !name)
        return;

    NSString *key = [NSString stringWithUTF8String:name];
    const char *value = ijkmeta_get_string_l(rawMeta, name);
    if (value) {
        [meta setObject:[NSString stringWithUTF8String:value] forKey:key];
    } else if (defaultValue) {
        [meta setObject:defaultValue forKey:key];
    } else {
        [meta removeObjectForKey:key];
    }
}

- (void)postEvent: (EMMediaPlayer *)mp withMessage:(IJKFFMoviePlayerMessage *)msg
{
    if (!mp || !msg)
        return;

    AVMessage *avmsg = &msg->_msg;
    switch (avmsg->what) {
        case FFP_MSG_FLUSH:
            break;
        case FFP_MSG_ERROR: {
            ALOGE("FFP_MSG_ERROR: %d\n", avmsg->arg1);

            [self setScreenOn:NO];

            [[NSNotificationCenter defaultCenter]
             postNotificationName:EMMPMoviePlayerPlaybackStateDidChangeNotification
             object:self];

            [[NSNotificationCenter defaultCenter]
                postNotificationName:EMMPMoviePlayerPlaybackDidFinishNotification
                object:self
                userInfo:@{
                    EMMPMoviePlayerPlaybackDidFinishReasonUserInfoKey: @(IJKMPMovieFinishReasonPlaybackError),
                    @"error": @(avmsg->arg1)}];
            break;
        }
        case FFP_MSG_PREPARED: {
            ALOGI("FFP_MSG_PREPARED:\n");

            _monitor.prepareDuration = (int64_t)SDL_GetTickHR() - _monitor.prepareStartTick;

            IjkMediaMeta *rawMeta = emmp_get_meta_l(_mediaPlayer);
            if (rawMeta) {
                ijkmeta_lock(rawMeta);

                NSMutableDictionary *newMediaMeta = [[NSMutableDictionary alloc] init];

                fillMetaInternal(newMediaMeta, rawMeta, IJKM_KEY_FORMAT, nil);
                fillMetaInternal(newMediaMeta, rawMeta, IJKM_KEY_DURATION_US, nil);
                fillMetaInternal(newMediaMeta, rawMeta, IJKM_KEY_START_US, nil);
                fillMetaInternal(newMediaMeta, rawMeta, IJKM_KEY_BITRATE, nil);

                fillMetaInternal(newMediaMeta, rawMeta, IJKM_KEY_VIDEO_STREAM, nil);
                fillMetaInternal(newMediaMeta, rawMeta, IJKM_KEY_AUDIO_STREAM, nil);

                int64_t video_stream = ijkmeta_get_int64_l(rawMeta, IJKM_KEY_VIDEO_STREAM, -1);
                int64_t audio_stream = ijkmeta_get_int64_l(rawMeta, IJKM_KEY_AUDIO_STREAM, -1);

                NSMutableArray *streams = [[NSMutableArray alloc] init];
                
                int width  = 0;
                int height = 0;

                size_t count = ijkmeta_get_children_count_l(rawMeta);
                for(size_t i = 0; i < count; ++i) {
                    IjkMediaMeta *streamRawMeta = ijkmeta_get_child_l(rawMeta, i);
                    NSMutableDictionary *streamMeta = [[NSMutableDictionary alloc] init];

                    if (streamRawMeta) {
                        fillMetaInternal(streamMeta, streamRawMeta, IJKM_KEY_TYPE, k_IJKM_VAL_TYPE__UNKNOWN);
                        const char *type = ijkmeta_get_string_l(streamRawMeta, IJKM_KEY_TYPE);
                        if (type) {
                            fillMetaInternal(streamMeta, streamRawMeta, IJKM_KEY_CODEC_NAME, nil);
                            fillMetaInternal(streamMeta, streamRawMeta, IJKM_KEY_CODEC_PROFILE, nil);
                            fillMetaInternal(streamMeta, streamRawMeta, IJKM_KEY_CODEC_LONG_NAME, nil);
                            fillMetaInternal(streamMeta, streamRawMeta, IJKM_KEY_BITRATE, nil);

                            if (0 == strcmp(type, IJKM_VAL_TYPE__VIDEO)) {
                                fillMetaInternal(streamMeta, streamRawMeta, IJKM_KEY_WIDTH, nil);
                                fillMetaInternal(streamMeta, streamRawMeta, IJKM_KEY_HEIGHT, nil);
                                fillMetaInternal(streamMeta, streamRawMeta, IJKM_KEY_FPS_NUM, nil);
                                fillMetaInternal(streamMeta, streamRawMeta, IJKM_KEY_FPS_DEN, nil);
                                fillMetaInternal(streamMeta, streamRawMeta, IJKM_KEY_TBR_NUM, nil);
                                fillMetaInternal(streamMeta, streamRawMeta, IJKM_KEY_TBR_DEN, nil);
                                fillMetaInternal(streamMeta, streamRawMeta, IJKM_KEY_SAR_NUM, nil);
                                fillMetaInternal(streamMeta, streamRawMeta, IJKM_KEY_SAR_DEN, nil);

                                if (video_stream == i) {
                                    _monitor.videoMeta = streamMeta;

                                    int64_t fps_num = ijkmeta_get_int64_l(streamRawMeta, IJKM_KEY_FPS_NUM, 0);
                                    int64_t fps_den = ijkmeta_get_int64_l(streamRawMeta, IJKM_KEY_FPS_DEN, 0);
                                    if (fps_num > 0 && fps_den > 0) {
                                        _fpsInMeta = ((CGFloat)(fps_num)) / fps_den;
                                        ALOGI("fps in meta %f\n", _fpsInMeta);
                                    }
                                }
                                
                                width  = [streamMeta[@(IJKM_KEY_WIDTH)] intValue];
                                height = [streamMeta[@(IJKM_KEY_HEIGHT)] intValue];

                            } else if (0 == strcmp(type, IJKM_VAL_TYPE__AUDIO)) {
                                fillMetaInternal(streamMeta, streamRawMeta, IJKM_KEY_SAMPLE_RATE, nil);
                                fillMetaInternal(streamMeta, streamRawMeta, IJKM_KEY_CHANNEL_LAYOUT, nil);

                                if (audio_stream == i) {
                                    _monitor.audioMeta = streamMeta;
                                }
                            }
                        }
                    }

                    [streams addObject:streamMeta];
                }

                [newMediaMeta setObject:streams forKey:kk_IJKM_KEY_STREAMS];

                ijkmeta_unlock(rawMeta);
                _monitor.mediaMeta = newMediaMeta;
                
                int64_t duration1 = [newMediaMeta[@(IJKM_KEY_DURATION_US)] longLongValue] / 1000;
                NSDictionary *dic = [NSDictionary dictionaryWithObjectsAndKeys:@(width), @"EVT_VIDEO_WIDTH",
                                     @(height), @"EVT_VIDEO_HEIGHT",
                                     @(duration1), @"EVT_PLAY_DURATION",
                                     @"", @"EVT_MSG", nil];
                [[NSNotificationCenter defaultCenter] postNotificationName:EMMPMediaPlaybackPreparedToPlayNotification object:self userInfo:dic];
            }

            [self startHudTimer];
            _isPreparedToPlay = YES;

            [[NSNotificationCenter defaultCenter] postNotificationName:EMMPMediaPlaybackIsPreparedToPlayDidChangeNotification object:self];

            _loadState = IJKMPMovieLoadStatePlayable | IJKMPMovieLoadStatePlaythroughOK;
            [[NSNotificationCenter defaultCenter] postNotificationName:EMMPMoviePlayerLoadStateDidChangeNotification object:self];
              
            if(_shouldAutoplay){
                if (self.isStandby) {
                    [self standby];
                } else if (self.isPause) {
                    [self pause];
                } else {
                    [self play];
                }
            }
            break;
        }
        case FFP_MSG_COMPLETED: {

            [self setScreenOn:NO];

            [[NSNotificationCenter defaultCenter]
             postNotificationName:EMMPMoviePlayerPlaybackStateDidChangeNotification
             object:self];

            [[NSNotificationCenter defaultCenter]
             postNotificationName:EMMPMoviePlayerPlaybackDidFinishNotification
             object:self
             userInfo:@{EMMPMoviePlayerPlaybackDidFinishReasonUserInfoKey: @(IJKMPMovieFinishReasonPlaybackEnded)}];
            break;
        }
        case FFP_MSG_VIDEO_SIZE_CHANGED:
            ALOGI("FFP_MSG_VIDEO_SIZE_CHANGED: %d, %d\n", avmsg->arg1, avmsg->arg2);
            if (avmsg->arg1 > 0)
                _videoWidth = avmsg->arg1;
            if (avmsg->arg2 > 0)
                _videoHeight = avmsg->arg2;
            [_netStatInfo setObject:[NSNumber numberWithInteger:_videoWidth] forKey:NET_STATUS_VIDEO_WIDTH];
            [_netStatInfo setObject:[NSNumber numberWithInteger:_videoHeight] forKey:NET_STATUS_VIDEO_HEIGHT];
            [self changeNaturalSize];
            break;
        case FFP_MSG_SAR_CHANGED:
            ALOGI("FFP_MSG_SAR_CHANGED: %d, %d\n", avmsg->arg1, avmsg->arg2);
            if (avmsg->arg1 > 0)
                _sampleAspectRatioNumerator = avmsg->arg1;
            if (avmsg->arg2 > 0)
                _sampleAspectRatioDenominator = avmsg->arg2;
            [self changeNaturalSize];
            break;
        case FFP_MSG_BUFFERING_START: {
            ALOGI("FFP_MSG_BUFFERING_START:\n");

            _monitor.lastPrerollStartTick = (int64_t)SDL_GetTickHR();

            _loadState = IJKMPMovieLoadStateStalled;

            [[NSNotificationCenter defaultCenter]
             postNotificationName:EMMPMoviePlayerLoadStateDidChangeNotification
             object:self];
            break;
        }
        case FFP_MSG_OPEN_HW_DECODER_FAILED: {
            ALOGW("open hw decoder failed.\n");
            [[NSNotificationCenter defaultCenter]
             postNotificationName:IJKMPMoviePlayerOpenHWVideoDecoderFailedNotification
             object:self];
            break;
        }
        case FFP_MSG_ERROR_NET_DISCONNECT: {
            ALOGE("error network disconnected.\n");
            [[NSNotificationCenter defaultCenter]
             postNotificationName:IJKMPMoviePlayerNetDisconnectNotification object:self];
            break;
        }
        case FFP_MSG_WARN_RECONNECT:{
            ALOGW("warn network reconnect.\n");
            [[NSNotificationCenter defaultCenter]
            postNotificationName:IJKMPMoviePlayerNetReconnectNotification object:self];
            break;
        }
        case FFP_MSG_ERROR_CONNECT_FAILD: {
            if (avmsg->arg1 == -403) {
                ALOGE("error connect forbidden.\n");
                [[NSNotificationCenter defaultCenter]
                 postNotificationName:IJKMPMoviePlayerNetForbiddenNotification object:self];
            }
            else{
                ALOGE("error connect failed.\n");
                [[NSNotificationCenter defaultCenter]
                 postNotificationName:IJKMPMoviePlayerNetDisconnectNotification object:self];
            }
            break;
        }
        case FFP_MSG_ERROR_UNSUPPORTED_FORMAT:{
            ALOGE("error FFP_MSG_ERROR_UNSUPPORTED_FORMAT");
            break;
        }
        case FFP_MSG_BUFFERING_END: {
            ALOGI("FFP_MSG_BUFFERING_END:\n");

            _monitor.lastPrerollDuration = (int64_t)SDL_GetTickHR() - _monitor.lastPrerollStartTick;

            _loadState = IJKMPMovieLoadStatePlayable | IJKMPMovieLoadStatePlaythroughOK;

            
            [[NSNotificationCenter defaultCenter]
             postNotificationName:EMMPMoviePlayerLoadStateDidChangeNotification
             object:self];
            NSDictionary *dic = [NSDictionary dictionaryWithObjectsAndKeys:@(1), @"BUFFERING_STATE", nil];
            [[NSNotificationCenter defaultCenter]
             postNotificationName:EMMPMoviePlayerPlaybackStateDidChangeNotification
             object:self userInfo:dic];
            break;
        }
        case FFP_MSG_BUFFERING_UPDATE:
            _bufferingPosition = avmsg->arg1;
            _bufferingProgress = avmsg->arg2;
            // NSLog(@"FFP_MSG_BUFFERING_UPDATE: %d, %%%d\n", _bufferingPosition, _bufferingProgress);
            break;
        case FFP_MSG_BUFFERING_BYTES_UPDATE:
            // NSLog(@"FFP_MSG_BUFFERING_BYTES_UPDATE: %d\n", avmsg->arg1);
            break;
        case FFP_MSG_BUFFERING_TIME_UPDATE:
            _bufferingTime       = avmsg->arg1;
            // NSLog(@"FFP_MSG_BUFFERING_TIME_UPDATE: %d\n", avmsg->arg1);
            break;
        case FFP_MSG_PLAYBACK_STATE_CHANGED:
            [[NSNotificationCenter defaultCenter]
             postNotificationName:EMMPMoviePlayerPlaybackStateDidChangeNotification
             object:self];
            if (self.playbackState == IJKMPMoviePlaybackStatePlaying) {
                if (self.isStandby) {
                    [self standby];
                } else if (self.isPause) {
                    [self pause];
                }
            }
            break;
        case FFP_CHANGE_VIDEO_SOURCE_SUCCESS:
            NSLog(@"change video source success, %d, %d\n", self.isStandby, self.isPause);
            if (self.isStandby) {
                [self standby];
            } else if (self.isPause) {
                [self pause];
            } else if(self.shouldAutoplay){
                [self play];
            }
            break;
        case FFP_MSG_SEEK_COMPLETE: {
            ALOGI("FFP_MSG_SEEK_COMPLETE:\n");
            [[NSNotificationCenter defaultCenter]
             postNotificationName:EMMPMoviePlayerDidSeekCompleteNotification
             object:self
             userInfo:@{EMMPMoviePlayerDidSeekCompleteTargetKey: @(avmsg->arg1),
                        EMMPMoviePlayerDidSeekCompleteErrorKey: @(avmsg->arg2)}];
            _seeking = NO;
            break;
        }
        case FFP_MSG_VIDEO_DECODER_OPEN: {
            _isVideoToolboxOpen = avmsg->arg1;
            ALOGI("FFP_MSG_VIDEO_DECODER_OPEN: %@\n", _isVideoToolboxOpen ? @"true" : @"false");
            [[NSNotificationCenter defaultCenter]
             postNotificationName:EMMPMoviePlayerVideoDecoderOpenNotification
             object:self];
            break;
        }
        case FFP_MSG_VIDEO_RENDERING_START: {
            ALOGI("FFP_MSG_VIDEO_RENDERING_START:\n");
            _monitor.firstVideoFrameLatency = (int64_t)SDL_GetTickHR() - _monitor.prepareStartTick;
            [[NSNotificationCenter defaultCenter]
             postNotificationName:EMMPMoviePlayerFirstVideoFrameRenderedNotification
             object:self];
            break;
        }
        case FFP_MSG_AUDIO_RENDERING_START: {
            ALOGI("FFP_MSG_AUDIO_RENDERING_START:\n");
            [[NSNotificationCenter defaultCenter]
             postNotificationName:EMMPMoviePlayerFirstAudioFrameRenderedNotification
             object:self];
            break;
        }
        case FFP_MSG_PROGRESS: {
            extern time_t time(time_t *);
            int64_t curtimeus = time(NULL);
            NSTimeInterval playProgress = avmsg->arg1 / (float)1000;
            NSTimeInterval duration1 = avmsg->arg2 / (float)1000;
            NSDictionary *dic = [NSDictionary dictionaryWithObjectsAndKeys:@(curtimeus),@"EVT_TIME",@((float)duration1), @"EVT_PLAY_DURATION",
                                 @((float)playProgress), @"EVT_PLAY_PROGRESS", @"", @"EVT_MSG", nil];
            [[NSNotificationCenter defaultCenter] postNotificationName:IJKMPMoviePlayerPlayProgressNotification object:self userInfo:dic];
            break;
        }
        case FFP_MSG_STREAM_UNIX_TIME: {
            extern time_t time(time_t *);
            int64_t curtimeus = time(NULL);
            unsigned int before = (unsigned int)avmsg->arg1;
            unsigned int after = (unsigned int)avmsg->arg2;
            int64_t unixTime = ((int64_t)before << 32) | after;
            NSDictionary *dic = [NSDictionary dictionaryWithObjectsAndKeys:@(curtimeus),@"EVT_TIME",@(unixTime), @"EVT_PLAY_STREAM_UNIX_TIME", @"", @"EVT_MSG", nil];
            [[NSNotificationCenter defaultCenter] postNotificationName:IJKMPMoviePlayerPlayStreamUnixTimeNotification object:self userInfo:dic];
            break;
        }
        case FFP_MSG_VIDEO_ROTATION_CHANGED:
        {
            ALOGI("video rotation degree:%d", avmsg->arg1);
            _videoRotation = avmsg->arg1;
            if (_glView)
                [_glView setupRotateMode:(_videoRotation + _rorationDegree) % 360];
            break;
        }
        default:
            // NSLog(@"unknown FFP_MSG_xxx(%d)\n", avmsg->what);
            break;
    }

    [_msgPool recycle:msg];
}

- (IJKFFMoviePlayerMessage *) obtainMessage {
    return [_msgPool obtain];
}

- (void)presentVideoFrame:(uint8_t *)data :(int)size :(int)width :(int)height :(int64_t)timems
{
    if (_videoFrameDelegate && [_videoFrameDelegate respondsToSelector:@selector(onVideoFrameRendering:::::)]) {
        [_videoFrameDelegate onVideoFrameRendering:data :size :width :height :timems];
    }
}

- (void)presentAudioFrame:(uint8_t *)data :(int)size :(int)sr :(int)channels :(int64_t)timems
{
    if (_audioFrameDelegate && [_audioFrameDelegate respondsToSelector:@selector(onAudioFrameRendering:::::)]) {
        [_audioFrameDelegate onAudioFrameRendering:data :size :sr :channels :timems];
    }
}

inline static EMFFMoviePlayerController *ffplayerRetain(void *arg) {
    return (__bridge_transfer EMFFMoviePlayerController *) arg;
}

int em_media_player_msg_loop(void* arg)
{
    @autoreleasepool {
        __block EMMediaPlayer *mp = (EMMediaPlayer*)arg;
        __weak EMFFMoviePlayerController *ffpController = ffplayerRetain(emmp_set_weak_thiz(mp, NULL));
        while (ffpController) {
            @autoreleasepool {
                __block IJKFFMoviePlayerMessage *msg = [ffpController obtainMessage];
                if (!msg)
                    break;

                int retval = emmp_get_msg(mp, &msg->_msg, 1);
                if (retval < 0)
                    break;

                // block-get should never return 0
                assert(retval > 0);
                //[ffpController performSelectorOnMainThread:@selector(postEvent:) withObject:param waitUntilDone:NO];
//                dispatch_async(dispatch_get_main_queue(), ^() {
//                    [ffpController postEvent:mp withMessage:msg];
//                });
                dispatch_async(dispatch_get_global_queue(0, 0), ^() {
                    [ffpController postEvent:mp withMessage:msg];
                });
            }
        }

        // retained in prepare_async, before SDL_CreateThreadEx
        emmp_dec_ref_p(&mp);
        return 0;
    }
}

#pragma mark av_format_control_message

static void onPresentVideoFrame(void *opaque, uint8_t *data, int size, int width, int height, int64_t timems)
{
    EMFFMoviePlayerController *mpc = (__bridge EMFFMoviePlayerController*)opaque;
    if (!mpc) {
        return;
    }
    [mpc presentVideoFrame:data :size :width :height :timems];
}

static void onPresentAudioFrame(void *opaque, uint8_t *data, int size, int sr, int channels, int64_t timems)
{
    EMFFMoviePlayerController *mpc = (__bridge EMFFMoviePlayerController*)opaque;
    if (!mpc) {
        return;
    }
    [mpc presentAudioFrame:data :size :sr :channels :timems];
}


static int onInjectIOControl(EMFFMoviePlayerController *mpc, id<IJKMediaUrlOpenDelegate> delegate, int type, void *data, size_t data_size)
{
    AVAppIOControl *realData = data;
    assert(realData);
    assert(sizeof(AVAppIOControl) == data_size);
    realData->is_handled     = NO;
    realData->is_url_changed = NO;

    if (delegate == nil)
        return 0;

    NSString *urlString = [NSString stringWithUTF8String:realData->url];

    IJKMediaUrlOpenData *openData =
    [[IJKMediaUrlOpenData alloc] initWithUrl:urlString
                                       event:(IJKMediaEvent)type
                                segmentIndex:realData->segment_index
                                retryCounter:realData->retry_counter];

    [delegate willOpenUrl:openData];
    if (openData.error < 0)
        return -1;

    if (openData.isHandled) {
        realData->is_handled = YES;
        if (openData.isUrlChanged && openData.url != nil) {
            realData->is_url_changed = YES;
            const char *newUrlUTF8 = [openData.url UTF8String];
            strlcpy(realData->url, newUrlUTF8, sizeof(realData->url));
            realData->url[sizeof(realData->url) - 1] = 0;
        }
    }
    
    return 0;
}

static int onInjectTcpIOControl(EMFFMoviePlayerController *mpc, id<IJKMediaUrlOpenDelegate> delegate, int type, void *data, size_t data_size)
{
    AVAppTcpIOControl *realData = data;
    assert(realData);
    assert(sizeof(AVAppTcpIOControl) == data_size);

    switch (type) {
        case IJKMediaCtrl_WillTcpOpen:

            break;
        case IJKMediaCtrl_DidTcpOpen:
            mpc->_monitor.tcpError = realData->error;
            mpc->_monitor.remoteIp = [NSString stringWithUTF8String:realData->ip];
            [mpc->_glView setHudValue: mpc->_monitor.remoteIp forKey:@"ip"];
            [mpc->_netStatInfo setObject:mpc->_monitor.remoteIp forKey:NET_STATUS_SERVER_IP];
            [[NSNotificationCenter defaultCenter] postNotificationName:IJKMPMoviePlayerConnectSeverSuccessNotification object:mpc];
            break;
        default:
            assert(!"unexcepted type for tcp io control");
            break;
    }
    if (delegate == nil)
        return 0;

    NSString *urlString = [NSString stringWithUTF8String:realData->ip];

    IJKMediaUrlOpenData *openData =
    [[IJKMediaUrlOpenData alloc] initWithUrl:urlString
                                       event:(IJKMediaEvent)type
                                segmentIndex:0
                                retryCounter:0];
    openData.fd = realData->fd;

    [delegate willOpenUrl:openData];
    if (openData.error < 0)
        return -1;
    [mpc->_glView setHudValue: [NSString stringWithFormat:@"fd:%d %@", openData.fd, openData.msg?:@"unknown"] forKey:@"tcp-info"];
    return 0;
}

static int onInjectAsyncStatistic(EMFFMoviePlayerController *mpc, int type, void *data, size_t data_size)
{
    AVAppAsyncStatistic *realData = data;
    assert(realData);
    assert(sizeof(AVAppAsyncStatistic) == data_size);

    mpc->_asyncStat = *realData;
    return 0;
}

static int64_t calculateElapsed(int64_t begin, int64_t end)
{
    if (begin <= 0)
        return -1;

    if (end < begin)
        return -1;

    return end - begin;
}

static int onInjectOnHttpEvent(EMFFMoviePlayerController *mpc, int type, void *data, size_t data_size)
{
    AVAppHttpEvent *realData = data;
    assert(realData);
    assert(sizeof(AVAppHttpEvent) == data_size);

    NSMutableDictionary *dict = [[NSMutableDictionary alloc] init];
    NSURL        *nsurl   = nil;
    IJKFFMonitor *monitor = mpc->_monitor;
    NSString     *url  = monitor.httpUrl;
    NSString     *host = monitor.httpHost;
    int64_t       elapsed = 0;

    id<IJKMediaNativeInvokeDelegate> delegate = mpc.nativeInvokeDelegate;

    switch (type) {
        case AVAPP_EVENT_WILL_HTTP_OPEN:
            url   = [NSString stringWithUTF8String:realData->url];
            nsurl = [NSURL URLWithString:url];
            host  = nsurl.host;

            monitor.httpUrl      = url;
            monitor.httpHost     = host;
            monitor.httpOpenTick = SDL_GetTickHR();
            [mpc setHudUrl:url];

            if (delegate != nil) {
                dict[IJKMediaEventAttrKey_host]         = [NSString ijk_stringBeEmptyIfNil:host];
                [delegate invoke:type attributes:dict];
            }
            break;
        case AVAPP_EVENT_DID_HTTP_OPEN:
            elapsed = calculateElapsed(monitor.httpOpenTick, SDL_GetTickHR());
            monitor.httpError = realData->error;
            monitor.httpCode  = realData->http_code;
            monitor.httpOpenCount++;
            monitor.httpOpenTick = 0;
            monitor.lastHttpOpenDuration = elapsed;
            [mpc->_glView setHudValue:@(realData->http_code).stringValue forKey:@"http"];

            if (delegate != nil) {
                dict[IJKMediaEventAttrKey_time_of_event]    = @(elapsed).stringValue;
                dict[IJKMediaEventAttrKey_url]              = [NSString ijk_stringBeEmptyIfNil:monitor.httpUrl];
                dict[IJKMediaEventAttrKey_host]             = [NSString ijk_stringBeEmptyIfNil:host];
                dict[IJKMediaEventAttrKey_error]            = @(realData->error).stringValue;
                dict[IJKMediaEventAttrKey_http_code]        = @(realData->http_code).stringValue;
                [delegate invoke:type attributes:dict];
            }
            break;
        case AVAPP_EVENT_WILL_HTTP_SEEK:
            monitor.httpSeekTick = SDL_GetTickHR();

            if (delegate != nil) {
                dict[IJKMediaEventAttrKey_host]         = [NSString ijk_stringBeEmptyIfNil:host];
                dict[IJKMediaEventAttrKey_offset]       = @(realData->offset).stringValue;
                [delegate invoke:type attributes:dict];
            }
            break;
        case AVAPP_EVENT_DID_HTTP_SEEK:
            elapsed = calculateElapsed(monitor.httpSeekTick, SDL_GetTickHR());
            monitor.httpError = realData->error;
            monitor.httpCode  = realData->http_code;
            monitor.httpSeekCount++;
            monitor.httpSeekTick = 0;
            monitor.lastHttpSeekDuration = elapsed;
            [mpc->_glView setHudValue:@(realData->http_code).stringValue forKey:@"http"];

            if (delegate != nil) {
                dict[IJKMediaEventAttrKey_time_of_event]    = @(elapsed).stringValue;
                dict[IJKMediaEventAttrKey_url]              = [NSString ijk_stringBeEmptyIfNil:monitor.httpUrl];
                dict[IJKMediaEventAttrKey_host]             = [NSString ijk_stringBeEmptyIfNil:host];
                dict[IJKMediaEventAttrKey_offset]           = @(realData->offset).stringValue;
                dict[IJKMediaEventAttrKey_error]            = @(realData->error).stringValue;
                dict[IJKMediaEventAttrKey_http_code]        = @(realData->http_code).stringValue;
                [delegate invoke:type attributes:dict];
            }
            break;
    }

    return 0;
}

// NOTE: could be called from multiple thread
static int ijkff_inject_callback(void *opaque, int message, void *data, size_t data_size)
{
    EMFFMoviePlayerController *mpc = (__bridge EMFFMoviePlayerController*)opaque;
    if (!mpc) {
        return 0;
    }
    switch (message) {
        case AVAPP_CTRL_WILL_CONCAT_SEGMENT_OPEN:
            return onInjectIOControl(mpc, mpc.segmentOpenDelegate, message, data, data_size);
        case AVAPP_CTRL_WILL_TCP_OPEN:
            return onInjectTcpIOControl(mpc, mpc.tcpOpenDelegate, message, data, data_size);
        case AVAPP_CTRL_WILL_HTTP_OPEN:
            return onInjectIOControl(mpc, mpc.httpOpenDelegate, message, data, data_size);
        case AVAPP_CTRL_WILL_LIVE_OPEN:
            return onInjectIOControl(mpc, mpc.liveOpenDelegate, message, data, data_size);
        case AVAPP_EVENT_ASYNC_STATISTIC:
            return onInjectAsyncStatistic(mpc, message, data, data_size);
        case AVAPP_CTRL_DID_TCP_OPEN:{
            return onInjectTcpIOControl(mpc, mpc.tcpOpenDelegate, message, data, data_size);
        }
        case AVAPP_EVENT_WILL_HTTP_OPEN:
        case AVAPP_EVENT_DID_HTTP_OPEN:
        case AVAPP_EVENT_WILL_HTTP_SEEK:
        case AVAPP_EVENT_DID_HTTP_SEEK:
            return onInjectOnHttpEvent(mpc, message, data, data_size);
        default: {
            return 0;
        }
    }
}

#pragma mark Airplay

-(BOOL)allowsMediaAirPlay
{
    if (!self)
        return NO;
    return _allowsMediaAirPlay;
}

-(void)setAllowsMediaAirPlay:(BOOL)b
{
    if (!self)
        return;
    _allowsMediaAirPlay = b;
}

-(BOOL)airPlayMediaActive
{
    if (!self)
        return NO;
    if (_isDanmakuMediaAirPlay) {
        return YES;
    }
    return NO;
}

-(BOOL)isDanmakuMediaAirPlay
{
    return _isDanmakuMediaAirPlay;
}

-(void)setIsDanmakuMediaAirPlay:(BOOL)isDanmakuMediaAirPlay
{
    _isDanmakuMediaAirPlay = isDanmakuMediaAirPlay;
    if (_isDanmakuMediaAirPlay) {
        _glView.scaleFactor = 1.0f;
    }
    else {
        CGFloat scale = [[UIScreen mainScreen] scale];
        if (scale < 0.1f)
            scale = 1.0f;
        _glView.scaleFactor = scale;
    }
     [[NSNotificationCenter defaultCenter] postNotificationName:EMMPMoviePlayerIsAirPlayVideoActiveDidChangeNotification object:nil userInfo:nil];
}


#pragma mark Option Conventionce

- (void)setFormatOptionValue:(NSString *)value forKey:(NSString *)key
{
    [self setOptionValue:value forKey:key ofCategory:kIJKFFOptionCategoryFormat];
}

- (void)setCodecOptionValue:(NSString *)value forKey:(NSString *)key
{
    [self setOptionValue:value forKey:key ofCategory:kIJKFFOptionCategoryCodec];
}

- (void)setSwsOptionValue:(NSString *)value forKey:(NSString *)key
{
    [self setOptionValue:value forKey:key ofCategory:kIJKFFOptionCategorySws];
}

- (void)setPlayerOptionValue:(NSString *)value forKey:(NSString *)key
{
    [self setOptionValue:value forKey:key ofCategory:kIJKFFOptionCategoryPlayer];
}

- (void)setFormatOptionIntValue:(int64_t)value forKey:(NSString *)key
{
    [self setOptionIntValue:value forKey:key ofCategory:kIJKFFOptionCategoryFormat];
}

- (void)setCodecOptionIntValue:(int64_t)value forKey:(NSString *)key
{
    [self setOptionIntValue:value forKey:key ofCategory:kIJKFFOptionCategoryCodec];
}

- (void)setSwsOptionIntValue:(int64_t)value forKey:(NSString *)key
{
    [self setOptionIntValue:value forKey:key ofCategory:kIJKFFOptionCategorySws];
}

- (void)setPlayerOptionIntValue:(int64_t)value forKey:(NSString *)key
{
    [self setOptionIntValue:value forKey:key ofCategory:kIJKFFOptionCategoryPlayer];
}

- (void)setMaxBufferSize:(int)maxBufferSize
{
    [self setPlayerOptionIntValue:maxBufferSize forKey:@"max-buffer-size"];
}

#pragma mark app state changed

- (void)registerApplicationObservers
{
    //音频打断
    [_notificationManager addObserver:self
                             selector:@selector(audioSessionInterrupt:)
                                 name:AVAudioSessionInterruptionNotification
                               object:nil];

    [_notificationManager addObserver:self
                             selector:@selector(applicationWillEnterForeground)
                                 name:UIApplicationWillEnterForegroundNotification
                               object:nil];

    [_notificationManager addObserver:self
                             selector:@selector(applicationDidBecomeActive)
                                 name:UIApplicationDidBecomeActiveNotification
                               object:nil];

    [_notificationManager addObserver:self
                             selector:@selector(applicationWillResignActive)
                                 name:UIApplicationWillResignActiveNotification
                               object:nil];

    [_notificationManager addObserver:self
                             selector:@selector(applicationDidEnterBackground)
                                 name:UIApplicationDidEnterBackgroundNotification
                               object:nil];

    [_notificationManager addObserver:self
                             selector:@selector(applicationWillTerminate)
                                 name:UIApplicationWillTerminateNotification
                               object:nil];
}

- (void)unregisterApplicationObservers
{
    [_notificationManager removeAllObservers:self];
}

- (void)audioSessionInterrupt:(NSNotification *)notification
{
    int reason = [[[notification userInfo] valueForKey:AVAudioSessionInterruptionTypeKey] intValue];
    switch (reason) {
        case AVAudioSessionInterruptionTypeBegan: {
            ALOGI("EMFFMoviePlayerController:audioSessionInterrupt: begin\n");
            switch (self.playbackState) {
                case IJKMPMoviePlaybackStatePaused:
                case IJKMPMoviePlaybackStateStopped:
                    break;
                default:
                    _playingBeforeInterruption = YES;
                    break;
            }
            [self pause];
            break;
        }
        case AVAudioSessionInterruptionTypeEnded: {
            ALOGI("EMFFMoviePlayerController:audioSessionInterrupt: end\n");
            
            [[AVAudioSession sharedInstance] setCategory:AVAudioSessionCategoryPlayback
                                             withOptions:AVAudioSessionCategoryOptionMixWithOthers
                                                   error:nil];
            [[AVAudioSession sharedInstance] setActive:YES error:nil];
            /* 设置为 AVAudioSessionCategoryOptionMixWithOthers，接到中断结束事件，可正确play，否则会报错,
                AVAudioSessionErrorCodeCannotInterruptOthers OSStatus 560557684。
             
                如果一开始就设置为 AVAudioSessionCategoryOptionMixWithOthers ，则收不到 中断结束事件。
             */

            if (_playingBeforeInterruption) {
//                NSLog(@" *****************    play    ***********************");
                [self play];
                _playingBeforeInterruption = NO;
            }
            break;
        }
    }
}

- (void)applicationWillEnterForeground
{
    ALOGI("EMFFMoviePlayerController:applicationWillEnterForeground: %d", (int)[UIApplication sharedApplication].applicationState);
}

- (void)applicationDidBecomeActive
{
    ALOGI("EMFFMoviePlayerController:applicationDidBecomeActive: %d", (int)[UIApplication sharedApplication].applicationState);
}

- (void)applicationWillResignActive
{
    ALOGI("EMFFMoviePlayerController:applicationWillResignActive: %d", (int)[UIApplication sharedApplication].applicationState);
    __weak __typeof(&*self)weakSelf = self;
    dispatch_async(dispatch_get_main_queue(), ^{
        if (_pauseInBackground) {
            [weakSelf pause];
        }
    });
}

- (void)applicationDidEnterBackground
{
    ALOGI("EMFFMoviePlayerController:applicationDidEnterBackground: %d", (int)[UIApplication sharedApplication].applicationState);
    __weak __typeof(&*self)weakSelf = self;
    dispatch_async(dispatch_get_main_queue(), ^{
        if (_pauseInBackground) {
            [weakSelf pause];
        }
    });
}

- (void)applicationWillTerminate
{
    ALOGI("EMFFMoviePlayerController:applicationWillTerminate: %d", (int)[UIApplication sharedApplication].applicationState);
    __weak __typeof(&*self)weakSelf = self;
    dispatch_async(dispatch_get_main_queue(), ^{
        if (_pauseInBackground) {
            [weakSelf pause];
        }
    });
}

- (UIImage *)captureFrame
{
    return [_glView captureFrame:2 :0 :0];
}

@end

