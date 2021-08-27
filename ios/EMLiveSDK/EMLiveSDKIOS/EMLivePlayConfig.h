//
//  EMLivePlayConfig.h
//  EMLiveSDKIOS
//
//  Created by 陈海东 on 16/9/7.
//  Copyright © 2016年 eastmoney. All rights reserved.
//

#import <Foundation/NSObject.h>

@interface EMLivePlayConfig : NSObject

//设置播放次数， 0表示无限重复，1默认值
@property (nonatomic, assign) int                   loopCount;

//播放器缓存时间 : 单位秒，取值需要大于0
@property (nonatomic, assign) int                   cacheTime;

//播放器播放立体声声道选择，默认立体声，取值范围 EM_Enum_Type_Play_Channel_Mode
@property (nonatomic, assign) int                   playChannelMode;

//是否自动调整播放器缓存时间 : YES:启用自动调整，自动调整的最大值和最小值可以分别通过修改maxCacheTime和minCacheTime来设置；
//                         NO:关闭自动调整，采用默认的指定缓存时间(1s)，可以通过修改cacheTime来调整缓存时间.
@property (nonatomic, assign) BOOL                  bAutoAdjustCacheTime;

//播放器缓存自动调整的最大时间 : 单位秒，取值需要大于0
@property (nonatomic, assign) int                   maxAutoAdjustCacheTime;

//播放器缓存自动调整的最小时间 : 单位秒，取值需要大于0
@property (nonatomic, assign) int                   minAutoAdjustCacheTime;

//播放器连接重试次数 : 最小值为 1， 最大值为 10, 默认值为 3
@property (nonatomic, assign) int                   connectRetryCount;

//播放器连接重试间隔 : 单位秒，最小值为 3, 最大值为 30， 默认值为 3
@property (nonatomic, assign) int                   connectRetryInterval;

//播放加载速度 EM_Enum_Type_Play_Loading_Strategy 默认为EM_PLAY_LOADING_STARTEGY_DEFAULT
@property (nonatomic, assign) int loadingStrategy;

// 用户自定义加载策略设置的值，单位KB，1080P及以上可设置300KB以上，确保视频能够正常播放，自定义值不可小于10KB
@property (nonatomic, assign) int customLoadingValue;

// dns缓存上限，如果设置0，则不启用dns cache功能 默认100
@property (nonatomic, assign) int dnsCacheCount;

// dnscache中dns 有效时间，超过这个时间会自动更新dns，单位:s 默认300(5min)
@property (nonatomic, assign) int dnsCacheValidTime;

// autoPlay 调用startplay后是否立即播放，false；不立即播放，调用resume后才会播放,默认为true
@property (nonatomic, assign) bool autoPlay;

// 在autoPlay:false情况下，是否看视频第一帧，默认false，用在使用视频第一帧作为封面
@property (nonatomic, assign) bool viewFirstVideoFrameOnPrepare;
@end
