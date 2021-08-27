//
//  MLImageView.h
//  medialiveimage
//
//  Created by 陈海东 on 18/1/30.
//  Copyright © 2018年 biankantech. All rights reserved.
//

#import <UIKit/UIKit.h>

@class MediaLiveImageContext;

@interface MLImageView : UIView

@property (nonatomic,weak) MediaLiveImageContext *imageContext;

@end
