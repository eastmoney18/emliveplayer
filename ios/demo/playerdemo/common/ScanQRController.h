//
//  ScanQRController.h
//  playerdemo
//
//  Created by caocl on 2021/7/12.
//

#import <UIKit/UIKit.h>

NS_ASSUME_NONNULL_BEGIN

@protocol ScanQRDelegate <NSObject>

- (void)onScanResult:(NSString *)result;

@end

@interface ScanQRController : UIViewController

@property (nonatomic, weak) id<ScanQRDelegate> delegate;


@end

NS_ASSUME_NONNULL_END
