//
//  MLImage.h
//  medialiveimage
//
//  Created by 陈海东 on 18/2/9.
//  Copyright © 2018年 biankantech. All rights reserved.
//

#ifndef MLImage_h
#define MLImage_h

#import "MLImageView.h"
#import "MLImageContextInterface.h"

typedef NS_ENUM(NSUInteger, ML_ROTATION) {
    ML_NO_ROTATION = 0x0,
    ML_ROTATION_CLOCKWISE_90_DEGREE = 0x1,
    ML_ROTATION_CLOCKWISE_180_DEGREE = 0x2,
    ML_ROTATION_CLOCKWISE_270_DEGREE = 0x3
};

typedef NS_ENUM(NSUInteger,ML_VIEW_RENDER_MODE) {
    ML_IMAGE_VIEW_RENDER_MODE_STRETCH = 0,
    ML_IMAGE_VIEW_RENDER_MODE_PRESERVE_AR_FILL = 1,
    ML_IMAGE_VIEW_RENDER_MODE_PRESERVE_AR = 2,
};

#endif /* MLImage_h */
