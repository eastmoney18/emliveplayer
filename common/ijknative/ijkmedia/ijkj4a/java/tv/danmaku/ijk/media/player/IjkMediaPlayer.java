package tv.danmaku.ijk.media.player;

import android.os.Bundle;

@SimpleCClassName
public class EMMediaPlayer {
    private long mNativeMediaPlayer;
    private long mNativeMediaDataSource;
    private static void postEventFromNative(Object weakThiz, int what, int arg1, int arg2, Object obj);
    private static void postPresentBufferFromNative(Object weakThiz, int colorFormat, byte[] frame, int width, int height);
    private  static void postPresentAudioBufferFromNative(Object weakThiz, byte[] frame, int length, int mspts);
    private static String onSelectCodec(Object weakThiz, String mimeType, int profile, int level);
    private static boolean onNativeInvoke(Object weakThiz, int what, Bundle args);
}
