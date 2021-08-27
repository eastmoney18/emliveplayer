# Add project specific ProGuard rules here.
# By default, the flags in this file are appended to flags specified
# in /Users/xuechongliu/Library/Android/sdk/tools/proguard/proguard-android.txt
# You can edit the include path and order by changing the proguardFiles
# directive in build.gradle.
#
# For more details, see
#   http://developer.android.com/guide/developing/tools/proguard.html

# Add any project specific keep options here:

# If your project uses WebView with JS, uncomment the following
# and specify the fully qualified class name to the JavaScript interface
# class:
#-keepclassmembers class fqcn.of.javascript.interface.for.webview {
#   public *;
#}

-keepclassmembers class ** {@android.webkit.JavascriptInterface *;}
-keep public class * extends android.content.BroadcastReceiver

-ignorewarnings

-dontoptimize

-keepattributes Signature,SourceFile,LineNumberTable,*Annotation*,EnclosingMethod


-dontwarn de.mindpipe.android.logging.log4j.**
-keep class de.mindpipe.android.logging.log4j.** { *; }

-keep class **.R$* {   *;  }

-dontwarn android.support.**
-keep class android.support.** { *; }

-dontwarn android.support.v4.**
-keep class android.support.v4.** { *; }
-keep public class * extends android.support.v4.**
-keep public class * extends android.app.Fragment



# module sdk_im

-keep public class * extends android.app.Activity
-keep public class * extends android.app.Application
-keep public class * extends android.app.Service
-keep public class * extends android.content.BroadcastReceiver
-keep public class * extends android.content.ContentProvider
-keep public class * extends android.app.backup.BackupAgentHelper
-keep public class * extends android.preference.Preference
-dontwarn android.webkit.**
-keep class android.webkit.** { *; }

-keepclasseswithmembernames class * {
    native <methods>;
}
-keepclasseswithmembernames class * {
    public <init>(android.content.Context, android.util.AttributeSet);
}
-keepclasseswithmembernames class * {
    public <init>(android.content.Context, android.util.AttributeSet, int);
}
-keepclassmembers enum * {
    public static **[] values();
    public static ** valueOf(java.lang.String);
}
-keep class * implements android.os.Parcelable { *; }
-keep class * implements java.io.Serializable { *; }

-keepclassmembers class ** {@android.webkit.JavascriptInterface *;}

# BuildConfig
-keep class com.eastmoney.emlivesdkandroid.BuildConfig{ *; }

# Wasp
# -keepattributes *Annotation*
# -keep class com.orhanobut.wasp.** { *; }
# -keepclassmembernames interface * {
#     @com.orhanobut.wasp.http.* <methods>;
# }

# Retrofit (http://square.github.io/retrofit/)
# Platform calls Class.forName on types which do not exist on Android to determine platform.
-dontnote retrofit2.Platform
# Platform used when running on RoboVM on iOS. Will not be used at runtime.
# Platform used when running on Java 8 VMs. Will not be used at runtime.
# Retain generic type information for use by reflection by converters and adapters.
-keepattributes Signature
# Retain declared checked exceptions for use by a Proxy instance.
-keepattributes Exceptions

# Keep native methods
-keepclassmembers class * {
    native <methods>;
}


-keepattributes *Annotation*

# EventBus custom config
-keepclassmembers class ** {
    public void onEvent*(***);
}

# ijkplayer
-keep class tv.danmaku.ijk.media.player.** { *; }

# custom view
-keepclassmembers public class * extends android.view.View {
   void set*(***);
   *** get*();
}

#media live lib
-keepclasseswithmembernames class com.medialivelib.** {*;}

#yt process
-keepclasseswithmembernames class com.ytfaceimagefilter.** {*;}

# emliveandroidsdk
-keep class com.eastmoney.emlivesdkandroid.EMLiveConstants {*;}
-keep class com.eastmoney.emlivesdkandroid.EMLivePlayConfig {*;}

-keep interface com.eastmoney.emlivesdkandroid.IEMLiveTakePictureListener {*;}
-keep interface com.eastmoney.emlivesdkandroid.IEMLivePlayListener {*;}
