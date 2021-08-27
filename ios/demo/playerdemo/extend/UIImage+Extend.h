//
//  UIImage+Extend.h
//  playerdemo
//
//  Created by caocl on 2021/7/8.
//

#import <UIKit/UIKit.h>

NS_ASSUME_NONNULL_BEGIN

@interface UIImage (Extend)

+(UIImage *)em_imageWithColor:(UIColor *)aColor;

+(UIImage *)em_imageWithColor:(UIColor *)aColor withFrame:(CGRect)aFrame;

@end

NS_ASSUME_NONNULL_END
