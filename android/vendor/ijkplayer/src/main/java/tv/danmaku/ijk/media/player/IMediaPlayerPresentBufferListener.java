package tv.danmaku.ijk.media.player;

/**
 * Created by eastmoney on 17/2/24.
 */

public interface IMediaPlayerPresentBufferListener {

    void onDrawFrameBuffer(int colorFormat, byte[] pic, int width, int height);
    void onPostAudioFrameBuffer(byte[] pcm, int length, int mspts, int sr, int channels);
}
