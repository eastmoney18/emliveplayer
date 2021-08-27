//
//  MLImageBufferSource.h
//  medialiveimage
//
//  Created by 陈海东 on 18/3/1.
//  Copyright © 2018年 biankantech. All rights reserved.
//

#import <Foundation/Foundation.h>

typedef NS_ENUM(NSInteger, MLImageBufferType)
{
    ML_IMAGE_BUFFER_TYPE_RGBA32 = 0x1,
    ML_IMAGE_BUFFER_TYPE_NV21,
    ML_IMAGE_BUFFER_TYPE_IOS_CVPixelImage,
    ML_IMAGE_BUFFER_ANDROID_SurfaceTexture,
};

@interface MLImageBufferSource : NSObject

- (void)feedInputBuffer: (void *)data :(size_t)dataLength :(int)width :(int)height :(int64_t)timems;

@end
