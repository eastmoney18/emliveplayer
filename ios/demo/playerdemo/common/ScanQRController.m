//
//  ScanQRController.m
//  playerdemo
//
//  Created by caocl on 2021/7/12.
//

#import "ScanQRController.h"

#import <AVFoundation/AVFoundation.h>

@interface ScanQRController ()<AVCaptureMetadataOutputObjectsDelegate>

@property (nonatomic, strong) AVCaptureSession *captureSession;
@property (nonatomic, strong) AVCaptureVideoPreviewLayer *videoPreviewLayer;
@property (nonatomic, assign) BOOL qrResult;

@end

@implementation ScanQRController


- (void)viewDidLoad
{
    [super viewDidLoad];
    
    [self startScanQRCode];
    
    CGSize size = [[UIScreen mainScreen] bounds].size;
    int c_x = size.width/2;
    int c_y = size.height/2;
    int roi = 160/2;
    
    UIView* top = [[UIView alloc] initWithFrame:CGRectMake(0, 0, size.width, c_y - roi)];
    top.backgroundColor = [UIColor blackColor];
    top.alpha = 0.8;
    [self.view addSubview:top];
    
    UIView* left = [[UIView alloc] initWithFrame:CGRectMake(0, c_y - roi, c_x - roi, 2*roi)];
    left.backgroundColor = [UIColor blackColor];
    left.alpha = 0.8;
    [self.view addSubview:left];

    UIView* right = [[UIView alloc] initWithFrame:CGRectMake(c_x + roi , c_y-roi, c_x - roi + 1, 2*roi)];
    right.backgroundColor = [UIColor blackColor];
    right.alpha = 0.8;
    [self.view addSubview:right];
    
    UIView* bottom = [[UIView alloc] initWithFrame:CGRectMake(0, c_y + roi, size.width, c_y - roi + 1)];
    bottom.backgroundColor = [UIColor blackColor];
    bottom.alpha = 0.8;
    [self.view addSubview:bottom];
}

-(void)viewWillAppear:(BOOL)animated
{
    [super viewWillAppear:animated];
    self.navigationController.navigationBarHidden = YES;
}

- (void)viewDidAppear:(BOOL)animated
{
    if ([self.navigationController respondsToSelector:@selector(interactivePopGestureRecognizer)]) {
        self.navigationController.interactivePopGestureRecognizer.enabled = NO;
    }
}


- (void)startScanQRCode
{
    NSError *error;
    AVCaptureDevice *captureDevice = [AVCaptureDevice defaultDeviceWithMediaType:AVMediaTypeVideo];
    
    if([captureDevice lockForConfiguration:nil]) {
        if(captureDevice && [captureDevice isFocusModeSupported:AVCaptureFocusModeContinuousAutoFocus]) {
            [captureDevice setFocusMode:AVCaptureFocusModeContinuousAutoFocus];
        }
        else if(captureDevice && [captureDevice isFocusModeSupported:AVCaptureFocusModeAutoFocus]){
            [captureDevice setFocusMode:AVCaptureFocusModeAutoFocus];
        }
        [captureDevice unlockForConfiguration];
    }
    
    AVCaptureDeviceInput *input = [AVCaptureDeviceInput deviceInputWithDevice:captureDevice error:&error];
    if (!input) {
        NSLog(@"%@", [error localizedDescription]);
        return ;
    }
    
    _captureSession = [[AVCaptureSession alloc] init];
    [_captureSession addInput:input];
    AVCaptureMetadataOutput *captureMetadataOutput = [[AVCaptureMetadataOutput alloc] init];
    [_captureSession addOutput:captureMetadataOutput];
    
    [captureMetadataOutput setMetadataObjectsDelegate:self queue:dispatch_get_global_queue(0, 0)];
    [captureMetadataOutput setMetadataObjectTypes:[NSArray arrayWithObject:AVMetadataObjectTypeQRCode]];
    captureMetadataOutput.rectOfInterest = CGRectMake(0.25, 0.25, 0.5, 0.5);
    _videoPreviewLayer = [[AVCaptureVideoPreviewLayer alloc] initWithSession:_captureSession];
    [_videoPreviewLayer setVideoGravity:AVLayerVideoGravityResizeAspectFill];
    [_videoPreviewLayer setFrame:self.view.layer.bounds];
    [self.view.layer addSublayer:_videoPreviewLayer];
    
    [_captureSession startRunning];
}

- (void)stopScanQRCode
{
    [_captureSession stopRunning];
    _captureSession = nil;
    
    [_videoPreviewLayer removeFromSuperlayer];
    _videoPreviewLayer = nil;
}

- (void)handleScanResult:(NSString *)result
{
    [self stopScanQRCode];
    if (_qrResult) {
        return;
    }
    _qrResult = YES;

    if ([self.delegate respondsToSelector:@selector(onScanResult:)]) {
        [self.delegate onScanResult:result];
    }
    [self.navigationController popViewControllerAnimated:NO];
}


- (void)captureOutput:(AVCaptureOutput *)captureOutput didOutputMetadataObjects:(NSArray *)metadataObjects fromConnection:(AVCaptureConnection *)connection {
    if (metadataObjects.count>0) {
        AVMetadataMachineReadableCodeObject *metadataObj = [metadataObjects objectAtIndex:0];
        if ([[metadataObj type] isEqualToString:AVMetadataObjectTypeQRCode]) {
            [self performSelectorOnMainThread:@selector(handleScanResult:) withObject:metadataObj.stringValue waitUntilDone:NO];
        }
    }
}

- (void) touchesBegan:(NSSet<UITouch *> *)touches withEvent:(UIEvent *)event
{
    [self stopScanQRCode];
    [self.navigationController popViewControllerAnimated:NO];
}


@end
