//
//  UIView+Extend.m
//  playerdemo
//
//  Created by caocl on 2021/7/8.
//

#import "UIView+Extend.h"

@implementation UIView (Extend)

- (void)setBackgroundImage:(UIImage *)image
{
    UIGraphicsBeginImageContext(self.frame.size);
    [image drawInRect:self.bounds];
    UIImage *bgImage = UIGraphicsGetImageFromCurrentImageContext();
    UIGraphicsEndImageContext();
    
    self.backgroundColor = [UIColor colorWithPatternImage:bgImage];
}


@end
