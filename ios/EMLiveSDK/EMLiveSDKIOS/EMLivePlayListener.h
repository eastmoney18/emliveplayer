//
//  EMLivePlayListener.h
//  EMLiveSDKIOS
//
//  Created by 陈海东 on 16/9/7.
//  Copyright © 2016年 eastmoney. All rights reserved.
//


#import "EMLiveSDKTypeDef.h"

@protocol  EMLivePlayListener <NSObject>

/**
 *
 *
 */
-(void) onPlayEvent:(int)EvtID withParam:(NSDictionary*)param;

/**
 *
 *
 */
-(void) onNetStatus:(NSDictionary*) param;


/**
 *
 */
@optional
-(void) onRecvConnectNofity;

@end
