//
//  marcodef.h
//  playerdemo
//
//  Created by caocl on 2021/7/8.
//

#ifndef macrodef_h
#define macrodef_h

#define LKRGB_A(r, g, b, a) [UIColor colorWithRed:(CGFloat)(r)/255.0f green:(CGFloat)(g)/255.0f blue:(CGFloat)(b)/255.0f alpha:(CGFloat)(a)]
#define LKRGB(r, g, b) LKRGB_A(r, g, b, 1)
#define LKRGB_HEX(__h__) LKRGB((__h__ >> 16) & 0xFF, (__h__ >> 8) & 0xFF, __h__ & 0xFF)
/** 弱引用自己 */
#define LKWS(weakSelf)  __weak __typeof(&*self)weakSelf = self;

//弱引用
#define LKWeakSelf(type)  __weak typeof(type) weak##type = type
#define LKStrongSelf(type)  __strong typeof(type) type = weak##type

#endif /* macrodef_h */
