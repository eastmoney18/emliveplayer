//
//  UIImage+Extend.m
//  playerdemo
//
//  Created by caocl on 2021/7/8.
//

#import "UIImage+Extend.h"

@implementation UIImage (Extend)

+(UIImage *)em_imageWithColor:(UIColor *)aColor
{
    return [UIImage em_imageWithColor:aColor withFrame:CGRectMake(0, 0, 1, 1)];
}

+(UIImage *)em_imageWithColor:(UIColor *)aColor withFrame:(CGRect)aFrame
{
    UIGraphicsBeginImageContext(aFrame.size);
    CGContextRef context = UIGraphicsGetCurrentContext();
    CGContextSetFillColorWithColor(context, [aColor CGColor]);
    CGContextFillRect(context, aFrame);
    UIImage *img = UIGraphicsGetImageFromCurrentImageContext();
    UIGraphicsEndImageContext();
    return img;
}

@end
