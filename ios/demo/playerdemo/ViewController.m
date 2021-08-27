//
//  ViewController.m
//  playerdemo
//
//  Created by caocl on 2021/7/8.
//

#import "ViewController.h"

#import <AVFoundation/AVFoundation.h>
#import <Masonry/Masonry.h>
#import <EMLiveSDKIOS/EMLiveSDKIOS.h>

#import "macrodef.h"
#import "UIImage+Extend.h"
#import "UIAlertView+Extend.h"
#import "ScanQRController.h"

@interface ViewController ()<EMLivePlayListener, ScanQRDelegate>

@property (nonatomic, strong) EMLivePlayer2 *player;
@property (nonatomic, strong) UIView        *viewContainer;
@property (nonatomic, assign) BOOL         bHWDec;
@property (nonatomic, assign) BOOL         startSeek;
@property (nonatomic, strong) UIButton    *btnPlayMode;
@property (nonatomic, strong) UIButton    *btnHWDec;
@property (nonatomic, strong) UIButton    *btnCapture;
@property (nonatomic, strong) UIButton    *btnPlay;
@property (nonatomic, strong) UIButton    *btnStop;
@property (nonatomic, strong) UIButton    *btnScanQR;
@property (nonatomic, strong) UIButton    *btnPlayRate;
@property (nonatomic, strong) UISlider    *sliderPlayProgress;
@property (nonatomic, strong) UILabel     *lbPlayTime;
@property (nonatomic, strong) UITextField *txtUrl;
@property (nonatomic, strong) UIImageView *imageView;

@end

@implementation ViewController

- (void)viewDidLoad
{
    [super viewDidLoad];
    // Do any additional setup after loading the view.
    
    [self initUI];
    [self initPlayer];
    
    self.title = @"播放器Demo";
}

-(void)viewWillAppear:(BOOL)animated
{
    [super viewWillAppear:animated];
    [self.navigationController setNavigationBarHidden:NO animated:NO];
}

- (void)initPlayer
{
    EMLivePlayConfig *config = [[EMLivePlayConfig alloc]init];
    _player = [[EMLivePlayer2 alloc] init];
    _player.enableHWAcceleration = YES;
    _player.config = config;
    _player.delegate = self;
}

- (void)initUI
{
    _viewContainer = [UIView new];
    _viewContainer.backgroundColor = [UIColor whiteColor];
    [self.view addSubview:_viewContainer];
    
    [_viewContainer mas_makeConstraints:^(MASConstraintMaker *make) {
        make.left.and.right.and.top.and.bottom.mas_equalTo(self.view);
    }];
    
    _txtUrl = [UITextField new];
    _txtUrl.borderStyle = UITextBorderStyleRoundedRect;
    _txtUrl.placeholder = @"输入播放地址";
    _txtUrl.backgroundColor = LKRGB_A(255, 255, 255, 0.5);
    _txtUrl.clearButtonMode = UITextFieldViewModeAlways;
    _txtUrl.font = [UIFont fontWithName:@"PingFangSC" size:17];
    [self.view addSubview:_txtUrl];
    [_txtUrl mas_makeConstraints:^(MASConstraintMaker *make) {
        make.left.mas_equalTo(self.view).offset(10);
        make.right.mas_equalTo(self.view.mas_right).offset(-80);
        make.top.mas_equalTo(self.mas_topLayoutGuide).offset(10);
        make.height.mas_equalTo(54);
    }];
    
    _btnScanQR = [[UIButton alloc] init];
    _btnScanQR.titleLabel.textColor = [UIColor whiteColor];
    _btnScanQR.layer.cornerRadius  = 8;
    _btnScanQR.layer.masksToBounds = YES;
    _btnScanQR.titleLabel.font = [UIFont fontWithName:@"PingFangSC-Medium" size:16];
    [_btnScanQR setBackgroundImage:[UIImage em_imageWithColor:LKRGB_A(0x33, 0x81, 0xe3, 0.5)] forState:UIControlStateNormal];
    [_btnScanQR setBackgroundImage:[UIImage em_imageWithColor:LKRGB_A(0x33, 0x81, 0xe3, 0.5)] forState:UIControlStateDisabled];
    [_btnScanQR setTitle:@"扫描" forState:UIControlStateNormal];
    [_btnScanQR addTarget:self action:@selector(scanQR) forControlEvents:UIControlEventTouchUpInside];
    [self.view addSubview:_btnScanQR];
    [_btnScanQR mas_makeConstraints:^(MASConstraintMaker *make) {
        make.left.mas_equalTo(_txtUrl.mas_right).offset(10);
        make.right.mas_equalTo(self.view.mas_right).offset(-10);
        make.top.mas_equalTo(self.mas_topLayoutGuide).offset(10);
        make.height.mas_equalTo(54);
    }];
    
    UIView *viewBottomContainer = [UIView new];
    viewBottomContainer.backgroundColor = LKRGB_A(0, 0, 0, 0.5);
    [self.view addSubview:viewBottomContainer];
    [viewBottomContainer mas_makeConstraints:^(MASConstraintMaker *make) {
        make.bottom.mas_equalTo(self.view).offset(0);
        make.left.mas_equalTo(self.view).offset(0);
        make.right.mas_equalTo(self.view).offset(0);
        make.height.mas_equalTo(64);
    }];

    _btnPlay = [[UIButton alloc] init];
    _btnPlay.titleLabel.textColor = [UIColor whiteColor];
    _btnPlay.layer.cornerRadius  = 8;
    _btnPlay.layer.masksToBounds = YES;
    _btnPlay.titleLabel.font = [UIFont fontWithName:@"PingFangSC-Medium" size:16];
    [_btnPlay setBackgroundImage:[UIImage em_imageWithColor:LKRGB_HEX(0x3381E3)] forState:UIControlStateNormal];
    [_btnPlay setBackgroundImage:[UIImage em_imageWithColor:LKRGB_A(0x33, 0x81, 0xe3, 0.5)] forState:UIControlStateDisabled];
    [_btnPlay setTitle:@"播放" forState:UIControlStateNormal];
    [_btnPlay addTarget:self action:@selector(play) forControlEvents:UIControlEventTouchUpInside];
    [viewBottomContainer addSubview:_btnPlay];

    _btnStop = [[UIButton alloc] init];
    _btnStop.titleLabel.textColor = [UIColor whiteColor];
    _btnStop.layer.cornerRadius  = 8;
    _btnStop.layer.masksToBounds = YES;
    _btnStop.titleLabel.font = [UIFont fontWithName:@"PingFangSC-Medium" size:16];
    [_btnStop setBackgroundImage:[UIImage em_imageWithColor:LKRGB_HEX(0xFF0000)] forState:UIControlStateNormal];
    [_btnStop setBackgroundImage:[UIImage em_imageWithColor:LKRGB_A(0x33, 0x81, 0xe3, 0.5)] forState:UIControlStateDisabled];
    [_btnStop setTitle:@"停止" forState:UIControlStateNormal];
    [_btnStop addTarget:self action:@selector(stop) forControlEvents:UIControlEventTouchUpInside];
    [viewBottomContainer addSubview:_btnStop];
    
    _btnHWDec = [[UIButton alloc] init];
    _btnHWDec.titleLabel.textColor = [UIColor whiteColor];
    _btnHWDec.layer.cornerRadius  = 8;
    _btnHWDec.layer.masksToBounds = YES;
    _btnHWDec.titleLabel.font = [UIFont fontWithName:@"PingFangSC-Medium" size:14];
    [_btnHWDec setBackgroundImage:[UIImage em_imageWithColor:LKRGB_HEX(0x3381E3)] forState:UIControlStateNormal];
    [_btnHWDec setBackgroundImage:[UIImage em_imageWithColor:LKRGB_A(0x33, 0x81, 0xe3, 0.5)] forState:UIControlStateDisabled];
    [_btnHWDec setTitle:@"hw-on" forState:UIControlStateNormal];
    [_btnHWDec addTarget:self action:@selector(enableHWAcceleration) forControlEvents:UIControlEventTouchUpInside];
    [viewBottomContainer addSubview:_btnHWDec];
    
    _btnCapture = [[UIButton alloc] init];
    _btnCapture.titleLabel.textColor = [UIColor whiteColor];
    _btnCapture.layer.cornerRadius  = 8;
    _btnCapture.layer.masksToBounds = YES;
    _btnCapture.titleLabel.font = [UIFont fontWithName:@"PingFangSC-Medium" size:14];
    [_btnCapture setBackgroundImage:[UIImage em_imageWithColor:LKRGB_HEX(0x3381E3)] forState:UIControlStateNormal];
    [_btnCapture setBackgroundImage:[UIImage em_imageWithColor:LKRGB_A(0x33, 0x81, 0xe3, 0.5)] forState:UIControlStateDisabled];
    [_btnCapture setTitle:@"抓图" forState:UIControlStateNormal];
    [_btnCapture addTarget:self action:@selector(captureImage) forControlEvents:UIControlEventTouchUpInside];
    [viewBottomContainer addSubview:_btnCapture];
    
    _btnPlayRate = [[UIButton alloc] init];
    _btnPlayRate.titleLabel.textColor = [UIColor whiteColor];
    _btnPlayRate.layer.cornerRadius  = 8;
    _btnPlayRate.layer.masksToBounds = YES;
    _btnPlayRate.titleLabel.font = [UIFont fontWithName:@"PingFangSC-Medium" size:14];
    [_btnPlayRate setBackgroundImage:[UIImage em_imageWithColor:LKRGB_HEX(0x3381E3)] forState:UIControlStateNormal];
    [_btnPlayRate setBackgroundImage:[UIImage em_imageWithColor:LKRGB_A(0x33, 0x81, 0xe3, 0.5)] forState:UIControlStateDisabled];
    [_btnPlayRate setTitle:@"1x" forState:UIControlStateNormal];
    [_btnPlayRate addTarget:self action:@selector(changePlayRate) forControlEvents:UIControlEventTouchUpInside];
    [viewBottomContainer addSubview:_btnPlayRate];
    
    [_btnPlay mas_makeConstraints:^(MASConstraintMaker *make) {
        make.left.mas_equalTo(viewBottomContainer).offset(10);
        make.width.mas_equalTo(64);
        make.height.mas_equalTo(32);
        make.centerY.mas_equalTo(viewBottomContainer);
    }];
    
    [_btnStop mas_makeConstraints:^(MASConstraintMaker *make) {
        make.bottom.mas_equalTo(_btnPlay);
        make.left.mas_equalTo(_btnPlay.mas_right).offset(10);
        make.width.and.height.mas_equalTo(_btnPlay);
        make.centerY.mas_equalTo(viewBottomContainer);
    }];
    
    [_btnHWDec mas_makeConstraints:^(MASConstraintMaker *make) {
        make.bottom.mas_equalTo(_btnPlay);
        make.left.mas_equalTo(_btnStop.mas_right).offset(10);
        make.width.and.height.mas_equalTo(_btnPlay);
        make.centerY.mas_equalTo(viewBottomContainer);
    }];
    
    [_btnCapture mas_makeConstraints:^(MASConstraintMaker *make) {
        make.bottom.mas_equalTo(_btnPlay);
        make.left.mas_equalTo(_btnHWDec.mas_right).offset(10);
        make.width.and.height.mas_equalTo(_btnPlay);
        make.centerY.mas_equalTo(viewBottomContainer);
    }];
    
    [_btnPlayRate mas_makeConstraints:^(MASConstraintMaker *make) {
        make.bottom.mas_equalTo(_btnPlay);
        make.left.mas_equalTo(_btnCapture.mas_right).offset(10);
        make.width.and.height.mas_equalTo(_btnPlay);
        make.centerY.mas_equalTo(viewBottomContainer);
    }];
    
    _sliderPlayProgress = [UISlider new];
    _sliderPlayProgress.continuous = NO;
    _sliderPlayProgress.minimumTrackTintColor = LKRGB_A(0x33, 0x81, 0xe3, 0.5);
    _sliderPlayProgress.maximumTrackTintColor = LKRGB_A(255, 255, 255, 0.5);
    _sliderPlayProgress.thumbTintColor = [UIColor purpleColor];
    UIImage *thumbImageNormal = [UIImage em_imageWithColor:[UIColor systemRedColor] withFrame:CGRectMake(0, 0, 8, 8)];
    UIImage *thumbImageHL = [UIImage em_imageWithColor:[UIColor systemBlueColor] withFrame:CGRectMake(0, 0, 8, 8)];
    [_sliderPlayProgress setThumbImage:thumbImageNormal forState:UIControlStateNormal];
    [_sliderPlayProgress setThumbImage:thumbImageHL forState:UIControlStateHighlighted];
    [_sliderPlayProgress addTarget:self action:@selector(onSeek:) forControlEvents:(UIControlEventValueChanged)];
    [_sliderPlayProgress addTarget:self action:@selector(onSeekBegin:) forControlEvents:(UIControlEventTouchDown)];
    [_sliderPlayProgress addTarget:self action:@selector(onDrag:) forControlEvents:UIControlEventTouchDragInside];

    [self.view addSubview:_sliderPlayProgress];
    [_sliderPlayProgress mas_makeConstraints:^(MASConstraintMaker *make) {
        make.bottom.mas_equalTo(viewBottomContainer.mas_top);
        make.left.mas_equalTo(self.view);
        make.right.mas_equalTo(self.view);
    }];
    
    _lbPlayTime = [UILabel new];
    _lbPlayTime.textColor = [UIColor grayColor];
    _lbPlayTime.text = @"00:00/00:00";
    _lbPlayTime.font = [UIFont systemFontOfSize:12];
    [self.view addSubview:_lbPlayTime];
    [_lbPlayTime mas_makeConstraints:^(MASConstraintMaker *make) {
        make.bottom.mas_equalTo(_sliderPlayProgress.mas_top);
        make.centerX.mas_equalTo(self.view);
    }];
    
    _imageView = [UIImageView new];
    _imageView.contentMode = UIViewContentModeScaleAspectFit;
    [self.view addSubview:_imageView];
    [_imageView mas_makeConstraints:^(MASConstraintMaker *make) {
        make.left.mas_equalTo(self.view);
        make.centerY.mas_equalTo(self.view);
        make.width.and.height.mas_equalTo(240);
    }];
    _imageView.hidden = YES;
}

#pragma mark --click event
- (void)play
{
    NSString *url = _txtUrl.text;
    if(url.length == 0){
        url = @"http://cctvalih5ca.v.myalicdn.com/live/cctv1_2/index.m3u8";
    }
    
    if(self.player.isPlaying){
        
    }
    else{
        _player.enableHWAcceleration = _bHWDec;
        [self.player setupVideoWidget:CGRectZero containView: _viewContainer insertIndex:0];
        [self.player setRenderMode:EM_RENDER_MODE_BLUR_FILL_SCREEN];
        /**
         * start type 根据实际播放地址填写
         *  EM_PLAY_TYPE_LIVE_RTMP = 0,    //RTMP直播
         *  EM_PLAY_TYPE_LIVE_FLV,              //FLV直播
         *  EM_PLAY_TYPE_VOD_FLV,              //FLV点播
         *  EM_PLAY_TYPE_VOD_HLS,             //HLS点播/直播
         *  EM_PLAY_TYPE_VOD_MP4,            //MP4点播
         *  EM_PLAY_TYPE_LOCAL_VIDEO,    //本地播放
         */
        [self.player startPlay:url type:EM_PLAY_TYPE_VOD_HLS];
        [self startLoadingAnimation];
    }
}

- (void)stop
{
    [self.player stopPlay];
}

- (void)scanQR
{
    AVAuthorizationStatus status = [AVCaptureDevice authorizationStatusForMediaType:AVMediaTypeVideo];
    if(status == AVAuthorizationStatusAuthorized){
        ScanQRController *vc = [[ScanQRController alloc] init];
        vc.delegate = self;
        [self.navigationController pushViewController:vc animated:NO];
    }
    else{
        if(status == AVAuthorizationStatusDenied){
            
        }
        else{
            [AVCaptureDevice requestAccessForMediaType:AVMediaTypeVideo completionHandler:^(BOOL granted) {}];
        }
    }
}

- (void)enableHWAcceleration
{
    _bHWDec = !_bHWDec;
    [self stop];
    [self play];
    NSString *title = [NSString stringWithFormat:@"%@", _bHWDec ? @"hw-off" : @"hw-on"];
    [_btnHWDec setTitle:title forState:UIControlStateNormal];
}

- (void)captureImage
{
#if TARGET_IPHONE_SIMULATOR
    
#elif TARGET_OS_IPHONE
    if(_player.isPlaying){
        UIImage *image = [_player captureFrame];
        if(image){
            _imageView.image = image;
            self.imageView.hidden = NO;
            LKWeakSelf(self);
            dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)(3 * NSEC_PER_SEC)), dispatch_get_main_queue(), ^{
                LKStrongSelf(self);
                self.imageView.hidden = YES;
            });
        }
    }
#endif
}

- (void)changePlayRate
{
    static float sPlayRate[]  = {0.5f, 0.8, 1.0f, 1.2f, 1.5f, 2.0f};
    static int sPlayRateIndex = 2;
    
    if(_player.isPlaying){
        ++sPlayRateIndex;
        sPlayRateIndex %= 6;
        float playrate = sPlayRate[sPlayRateIndex];
        [_btnPlayRate setTitle:[NSString stringWithFormat:@"%.2fx", playrate] forState:UIControlStateNormal];
        [_player setPlaybackRate:playrate];
    }
}

#pragma mark--deleagte
- (void)onPlayEvent:(int)EvtID withParam:(NSDictionary*)param
{
    LKWeakSelf(self);
    dispatch_async(dispatch_get_main_queue(), ^{
        LKStrongSelf(self);
        if(EvtID == EM_PLAY_EVT_PLAY_PROGRESS){
            if(self.startSeek){
                return;
            }
            float progress = [param[EVT_PLAY_PROGRESS] floatValue];
            float duration = [param[EVT_PLAY_DURATION] floatValue];
            int intProgress = progress + 0.5;
            int intDuration = duration + 0.5;
            
            self.lbPlayTime.text = [NSString stringWithFormat:@"%02d:%02d/%02d:%02d",
                                (int)(intProgress / 60),
                                (int)(intProgress % 60),
                                (int)(intDuration / 60),
                                (int)(intDuration % 60)];
            
            if (duration > 0 && self.sliderPlayProgress.maximumValue != duration) {
                [self.sliderPlayProgress setMaximumValue:duration];
            }
            [self.sliderPlayProgress setValue:progress];
        }
        else if(EvtID == EM_PLAY_EVT_PLAY_BEGIN){
            [self stopLoadingAnimation];
        }
        else if(EvtID == EM_PLAY_EVT_RCV_FIRST_I_FRAME){
            
        }
        else if(EvtID == EM_PLAY_EVT_PLAY_LOADING){
            [self startLoadingAnimation];
        }
    });
}

- (void)stopLoadingAnimation
{
    [self.view sb_hiddenTips];
}

- (void)startLoadingAnimation
{
    [self.view sb_showTips:@"" showIndicator:YES hiddenAfterSeconds:0 isBlackBg:NO completion:nil];
}

- (void)onNetStatus:(NSDictionary*) param
{
  //  NSLog(@"%s:%@", __func__, param);
}

- (void)onScanResult:(NSString *)result
{
    if(result.length > 0){
        _txtUrl.text = result;
    }
    NSLog(@"scan result:%@.", result);
}

- (void) touchesBegan:(NSSet<UITouch *> *)touches withEvent:(UIEvent *)event
{
    [self.txtUrl resignFirstResponder];
}

-(void)onSeek:(UISlider *)slider
{
    [_player seek:slider.value];
    _startSeek = NO;
    NSLog(@"vod seek drag end");
}

-(void)onSeekBegin:(UISlider *)slider
{
    _startSeek = YES;
    NSLog(@"vod seek drag begin");
}

-(void)onDrag:(UISlider *)slider
{
    float progress  = slider.value;
    float duration  = slider.maximumValue;
    int intProgress = progress + 0.5;
    int intDuration = duration + 0.5;
    self.lbPlayTime.text = [NSString stringWithFormat:@"%02d:%02d/%02d:%02d",
                        (int)(intProgress / 60),
                        (int)(intProgress % 60),
                        (int)(intDuration / 60),
                        (int)(intDuration % 60)];
    
}


@end
