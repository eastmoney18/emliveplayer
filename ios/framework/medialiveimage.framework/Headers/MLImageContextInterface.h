//
//  MLImageContextInterface.hpp
//  medialiveimage
//
//  Created by 陈海东 on 18/1/31.
//  Copyright © 2018年 biankantech. All rights reserved.
//

#ifndef MLImageContextInterface_hpp
#define MLImageContextInterface_hpp


#include <UIKit/UIKit.h>
@class MLImageView;
@class MLImageCamera;
@class MLImageBufferSource;

typedef NS_ENUM(NSInteger, MLImageRenderMode){
    ML_IMAGE_RENDER_MODE_STRETCH = 0,
    ML_IMAGE_RENDER_MODE_PRESERVE_AR_FILL = 1,
    ML_IMAGE_RENDER_MODE_PRESERVE_AR = 2,
    ML_IMAGE_RENDER_MODE_PRESERVE_AR_FILL_BLUR = 3,
};

typedef NS_ENUM(NSInteger, MLImageRenderRotation) {
    ML_IMAGE_NO_ROTATION = 0,
    ML_IMAGE_ROTATION_CLOCKWISE_90_DEGREE = 1,
    ML_IMAGE_ROTATION_CLOCKWISE_180_DEGREE = 2,
    ML_IMAGE_ROTATION_CLOCKWISE_270_DEGREE = 3
};

typedef void(^LogoutBlock)(int level, NSString *content);

@protocol MLImageContextVideoDataOutputDelegate <NSObject>

- (void)onVideoDataOutput:(void *)data :(int)width :(int)height :(int)type :(int64_t)timems;

@end

@interface MediaLiveImageContext : NSObject

- (instancetype)init;

+ (void)setLogoutBlock:(LogoutBlock)log;

- (void)stopProcess;

- (void)clearLastFrame;

- (BOOL)setImageView:(MLImageView *)view;

- (void)viewSizeChanged:(NSInteger)width :(NSInteger)height;

- (void)outputSizeChanged:(NSInteger)width :(NSInteger)height;

- (void)setUseFastViewRender:(BOOL)on;

- (void)setImageViewRenderMode:(NSInteger)renderMode;

- (void)setImageViewRotation:(NSInteger)videoRotation :(BOOL)mirror;

- (void)setImageSourceRotation:(NSInteger)videoRotation :(BOOL)mirror;

- (void)setRotateValue:(int)value;

- (void)enableRoundCorner:(BOOL)enabled :(float)radius;

- (MLImageBufferSource *)getNativeBufferSource;

- (BOOL)startPictureSource:(UIImage *)image :(float)fps :(NSInteger)width :(NSInteger)height;

- (BOOL)startBufferSource:(int)bufferType :(int)width :(int)height;

- (UIImage *)captureViewPicture;

- (BOOL)setWaterMark:(UIImage *)image pos:(CGRect)rect mirror:(BOOL)mirror;


@end


#endif /* MLImageContextInterface_hpp */
