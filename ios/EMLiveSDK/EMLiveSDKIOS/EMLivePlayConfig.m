//
//  EMLivePlayConfig.m
//  EMLiveSDKIOS
//
//  Created by 陈海东 on 16/9/8.
//  Copyright © 2016年 eastmoney. All rights reserved.
//

#import <Foundation/Foundation.h>

#import <Foundation/Foundation.h>
#import "EMLivePlayConfig.h"
#import "EMLiveSDKTypeDef.h"

@implementation EMLivePlayConfig

- (instancetype)init
{
    self = [super init];
    if (self != nil) {
        self.cacheTime = 3;
        self.bAutoAdjustCacheTime = YES;
        self.maxAutoAdjustCacheTime = 5;
        self.minAutoAdjustCacheTime = 5;
        self.connectRetryInterval = 3;
        self.connectRetryCount = 3;
        self.loopCount = 1;
        self.playChannelMode = 0;
        self.loadingStrategy = EM_PLAY_LOADING_STARTEGY_DEFAULT;
        self.customLoadingValue = 300;
        self.dnsCacheCount = 100;
        self.dnsCacheValidTime = 300;
        self.autoPlay = true;
        self.viewFirstVideoFrameOnPrepare = false;
    }
    return self;
}

- (id)copyWithZone:(NSZone *)zone
{
    EMLivePlayConfig *copy = [[[self class] allocWithZone: zone] init];
    copy.cacheTime = self.cacheTime;
    copy.bAutoAdjustCacheTime = self.bAutoAdjustCacheTime;
    copy.maxAutoAdjustCacheTime = self.maxAutoAdjustCacheTime;
    copy.minAutoAdjustCacheTime = self.minAutoAdjustCacheTime;
    copy.connectRetryCount = self.connectRetryCount;
    copy.connectRetryInterval = self.connectRetryInterval;
    copy.loopCount = self.loopCount;
    copy.playChannelMode = self.playChannelMode;
    copy.loadingStrategy = self.loadingStrategy;
    copy.customLoadingValue = self.customLoadingValue;
    copy.dnsCacheCount = self.dnsCacheCount;
    copy.dnsCacheValidTime = self.dnsCacheValidTime;
    copy.autoPlay = self.autoPlay;
    copy.viewFirstVideoFrameOnPrepare = self.viewFirstVideoFrameOnPrepare;
    return copy;
}

@end
