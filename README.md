## 基本介绍
EMLiveSDK 是基于ijkplayer基础修改和优化的android/ios视频播放器；SDK封装了播放接口，简化调用，易于集成到项目中使用。已在主要项目中使用多年。

*ios*     
ios 8 - ios 14      
armv7 arm64 i386 x86_64

*android*:        
andorid 18+     
armv5 armv7 arm64

## 目录结构
*ios*       
framework - medialiveimage.framework 视频渲染模块（暂未开放源码，源码开放计划中...)     
EMLiveSDK - 播放器SDK   
demo - 播放器测试demo

*android*   
emlivesdkandroid - 播放器SDK    
emlibso - 播放器jni相关代码
aar - medialivelib-release.aar 视频渲染模块（暂未开放源码，源码开放计划中...)     
app - 播放器测试demo

*common*        
ffmpeg-build - ffmpeg and openssl code 和编译脚本   
ijknative - ijkplayer 播放器（ios andord共用）

## 编译
*ios*     
编译环境    
macOS 10.15.7   
xcode 12.2      
ffmpeg和openssl编译     
        
    cd common/ffmpeg-build/ios
    sh +x compile-openssl.sh all
    sh +x compile-ffmpeg.sh all

编译完成后有生成的库会自动lipo模拟器版本    
支持架构 armv7 arm64 i386 x86_64   
注意 先编译openssl，后编译ffmpeg。否则ffmpeg可能不支持https

    cd ios/EMLiveSDK
    sh +x make.sh Release

生成播放器SDK

*android*    
编译环境    
macOS 10.15.7   
android studio 4.2.2
ndk ndk-r13b     
ffmpeg和openssl编译

    cd common/ffmpeg-build/android
    sh +x compile-openssl.sh armv5 (armv7a/arm64)
    sh +x compile-ffmpeg.sh armv5 (armv7a/arm64)
注意 先编译openssl，后编译ffmpeg。否则ffmpeg可能不支持https

    cd android
    sh +x gradlew as (或通过IDE编译)
生成播放器sdk (emlivesdkandroid/outputs/aar/emlivesdkandroid-release.aar)   
支持架构 armeabi armv7 arm64   (高版本NDK已不支持armeabi) 

## 集成使用
*ios*     
前置依赖
    
    CocoaLumberjack 3.6.0 
    libc++.tbd
    libz.tbd
    AudioToolbox.framework
    AVFoundation.framework
    ...

Enable bitcode:NO       
工程添加

    EMLiveSDKIOS.framework
    medialiveimage.framework

```objc
EMLivePlayConfig *config = [[EMLivePlayConfig alloc]init];
self.player = [[EMLivePlayer2 alloc] init];
self.player.enableHWAcceleration = YES;
self.player.config = config;
self.player.delegate = self;
[self.player setupVideoWidget:CGRectZero containView: _viewContainer insertIndex:0];
self.player startPlay:url type:type]; // 

type 根据设置URL选择以下type
typedef NS_ENUM(NSInteger, EM_Enum_PlayType) {
    EM_PLAY_TYPE_LIVE_RTMP = 0,          //RTMP直播
    EM_PLAY_TYPE_LIVE_FLV,               //FLV直播
    EM_PLAY_TYPE_VOD_FLV,                //FLV点播
    EM_PLAY_TYPE_VOD_HLS,                //HLS点播/直播
    EM_PLAY_TYPE_VOD_MP4,                //MP4点播
    EM_PLAY_TYPE_LOCAL_VIDEO,            //本地播放
};

```
更多功能请参见demo

*android*     
gradle 
```xml
dependencies{
    // 添加emlivesdkandroid.aar medlivelib.aar
    implementation fileTree(dir:'xxx', include:['*.aar']) 
    implementation 'log4j:log4j:1.2.17'
    implementation 'de.mindpipe.android:android-logging-log4j:1.0.3'
    ...
}
```

xml
``` xml
<LinearLayout
    android:id="@+id/layout"
    android:layout_width="match_parent"
    android:layout_height="match_parent"
    android:orientation="vertical">

    <com.eastmoney.emlivesdkandroid.ui.EMLiveVideoView2
        android:id="@+id/video_view"
        android:layout_width="match_parent"
        android:layout_height="match_parent"
        android:layout_centerInParent="true"
        android:layout_weight="1"
        android:visibility="visible" />
</LinearLayout>
```

```java
mLivePlayer = new EMLivePlayer2(this);
mPlayerView = (EMLiveVideoView2) findViewById(R.id.video_view);
mLivePlayer.setPlayerView((ViewGroup) mPlayerView);
mLivePlayer.setConfig(mPlayConfig);
mLivePlayer.startPlay(playUrl, mPlayType);

//mPlayType
public static final int PLAY_TYPE_LIVE_RTMP = 0;
public static final int PLAY_TYPE_LIVE_FLV = 1;
public static final int PLAY_TYPE_VOD_FLV = 2;
public static final int PLAY_TYPE_VOD_HLS = 3;
public static final int PLAY_TYPE_VOD_MP4 = 4;
public static final int PLAY_TYPE_LOCAL_VIDEO = 5;

```
更多功能请参见demo


## 问题反馈
请在 github 上公开提出相关[技术问题](https://github.com/eastmoney18/emliveplayer/pulls/)

## License
LGPLv2.1 or later

LGPL    
[ffmpeg](http://ffmpeg.org/)  
[ijkplayer](https://github.com/bilibili/ijkplayer)

