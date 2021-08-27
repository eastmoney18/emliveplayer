//
//  EMParameter.h
//  EMLiveSDKIOS
//
//  Created by 陈海东 on 16/9/7.
//  Copyright © 2016年 eastmoney. All rights reserved.
//
#import <Foundation/NSObject.h>
#import "EMLiveSDKTypeDef.h"
#import "IJKMediaFramework.h"


@interface EMParameter : NSObject

+(NSArray *)getSDKVersion;

+ (IJKMPMovieScalingMode)getIJKScalingModeFromRenderMode:(EM_Enum_Type_RenderMode)renderMode;

+ (int)getRotateDegreeFromOrientation:(EM_Enum_Type_HomeOrientation)oritation;

+ (int)getEMEventFromVCSessionEvent:(int)vcSessionEvt;

@end
