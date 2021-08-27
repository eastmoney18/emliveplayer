//
//  EMParameter.m
//  EMLiveSDKIOS
//
//  Created by 陈海东 on 16/9/7.
//  Copyright © 2016年 eastmoney. All rights reserved.
//

#import <Foundation/Foundation.h>
#import <AVFoundation/AVFoundation.h>
#import "EMParameter.h"
#import "EMLiveSDKTypeDef.h"

#import "CocoaLumberjack/CocoaLumberjack.h"
#ifdef DEBUG
static const DDLogLevel ddLogLevel = DDLogLevelDebug;
#else
static const DDLogLevel ddLogLevel = DDLogLevelInfo;
#endif

void emlivesdk_log_printf(NSString *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    NSString *content = [[NSString alloc]initWithFormat:fmt arguments:args];
    va_end(args);
#ifdef DEBUG
    DDLogDebug(@"[ELSDK] %@", content);
#else
    DDLogInfo(@"[ELSDK] %@", content);
#endif
}

void emlivesdk_log_printf(const char* format, va_list args)
{
    NSString *fmt = [[NSString alloc]initWithUTF8String:format];
    NSString *content = [[NSString alloc]initWithFormat:fmt arguments:args];
#ifdef DEBUG
    DDLogDebug(@"[ELSDK] %@", content);
#else
    DDLogInfo(@"[ELSDK] %@", content);
#endif
}

typedef NS_ENUM(NSInteger, VCEvent)
{
    VCSessionEventOpenCameraSuccess,
    VCSessionEventOpenCameraFailed,
    VCSessionEventOpenMicFailed,
    VCSessionEventUnsupportedResolution,
    VCSessionEventUnsupportedSamplerate,
    VCSessionEventCheckParameterFailed,
    VSCessionEventServerConnectFail,
};

@interface EMParameter()

@end

@implementation EMParameter


+(NSArray *)getSDKVersion
{
    return [NSArray arrayWithObjects:@1, @5, @48, @39, nil];
}

+ (IJKMPMovieScalingMode)getIJKScalingModeFromRenderMode:(EM_Enum_Type_RenderMode)renderMode
{
    if (renderMode == EM_RENDER_MODE_FILL_SCREEN) {
        return IJKMPMovieScalingModeAspectFill;
    }else if (renderMode == EM_RENDER_MODE_FILL_EDGE) {
        return IJKMPMovieScalingModeAspectFit;    }
    return IJKMPMovieScalingModeAspectFit;
}

+ (int)getRotateDegreeFromOrientation:(EM_Enum_Type_HomeOrientation)oritation
{
    switch (oritation) {
        case EM_Enum_HOME_ORIENTATION_DOWN:
            return 0;
        case EM_Enum_HOME_ORIENTATION_LEFT:
            return 90;
        case EM_Enum_HOME_ORIENTATION_UP:
            return 180;
        case EM_Enum_HOME_ORIENTATION_RIGHT:
            return 270;
        default:
            break;
    }
}

+ (int)getEMEventFromVCSessionEvent:(int)vcSessionEvt
{
    switch (vcSessionEvt) {
        case VCSessionEventUnsupportedSamplerate:
            return EM_PUSH_ERR_UNSUPPORTED_SAMPLERATE;
            
        case VCSessionEventOpenMicFailed:
            return EM_PUSH_ERR_OPEN_MIC_FAIL;
            
        case VCSessionEventOpenCameraFailed:
            return EM_PUSH_ERR_OPEN_CAMERA_FAIL;
            
        case VCSessionEventOpenCameraSuccess:
            return EM_PUSH_EVT_OPEN_CAMERA_SUCC;
            
        case VCSessionEventUnsupportedResolution:
            return EM_PUSH_ERR_UNSUPPORTED_RESOLUTION;
        
        case VSCessionEventServerConnectFail:
            return EM_PUSH_WARNING_SEVER_CONN_FAIL;
            
        case VCSessionEventCheckParameterFailed:
            return EM_PUSH_ERR_CHECK_PARAMETER_FAIL;
        default:
            break;
    }
    return 0;
}
@end

