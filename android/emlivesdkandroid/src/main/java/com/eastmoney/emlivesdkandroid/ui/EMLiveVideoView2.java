package com.eastmoney.emlivesdkandroid.ui;

import android.annotation.TargetApi;
import android.content.Context;
import android.util.AttributeSet;
import android.util.Log;
import android.view.Gravity;
import android.view.MotionEvent;
import android.view.TouchDelegate;
import android.view.View;
import android.widget.FrameLayout;

import com.eastmoney.emlivesdkandroid.EMLiveConstants;
import com.medialivelib.image.IMediaImageView;
import com.medialivelib.image.MLImageContext;
import com.medialivelib.image.MLImageSurfaceView;
import com.medialivelib.image.MLImageTextureView;

import tv.danmaku.ijk.media.player.IMediaPlayer;

/**
 * Created by eastmoneyd_pcfs on 2018/1/26.
 */

public class EMLiveVideoView2 extends FrameLayout {
    public static final String TAG = "EMLiveVideoView2";
    public static final int USER_DEFINED_UI_MESSAGE = 0x123456;
    public static final int RENDER_MODE_FASTIMAGE_SURFACE_VIEW = 2;
    public static final int RENDER_MODE_FASTIMAGE_TEXTURE_VIEW = 3;
    private IMediaImageView mRenderView = null;
    private int mVideoRotationDegree = 0;
    private Context mContext = null;
    private MLImageContext mImageContext = null;
    private int mRenderMode = EMLiveConstants.RENDER_MODE_FILL_PRESERVE_AR_FILL;
    private int mRenderRotation = EMLiveConstants.RENDER_ROTATION_PORTRAIT;
    private boolean mRenderMirror = false;
    private boolean mEnableRoundCorner = false;
    private int mCornerRadius = 0;
    private int mDisplayType = 0;
    private IMediaPlayer mMediaPlayer;
  //  private GLTextureOutputRenderer mTextureOutput;
   // private boolean mRenderViewCreated = false;
   // private EMTouchFocusView mFocusIndicatorView;

    private void initVideoView(Context context) {
        this.mContext = context;
    }

    public EMLiveVideoView2(Context context) {
        super(context);
        initVideoView(context);
    }

    public EMLiveVideoView2(Context context, AttributeSet attrs) {
        super(context, attrs);
        initVideoView(context);
    }

    public EMLiveVideoView2(Context context, AttributeSet attrs, int defStyleAttr) {
        super(context, attrs, defStyleAttr);
        initVideoView(context);
    }

    @TargetApi(21)
    public EMLiveVideoView2(Context context, AttributeSet attrs, int defStyleAttr, int defStyleRes) {
        super(context, attrs, defStyleAttr, defStyleRes);
        initVideoView(context);
    }

    /**
     * 设置要显示的底层view类型，可选textureview 和 surfaceview
     * @param type 要显示的Mode
     */
    public void setDisplayType(int type) {
        if (type != mDisplayType || this.mRenderView == null) {
            if (this.mRenderView != null) {
                removeView((View)mRenderView);
                mRenderView = null;
            }
            IMediaImageView renderView = null;
            if (type == RENDER_MODE_FASTIMAGE_TEXTURE_VIEW) {
                renderView = new MLImageTextureView(mContext);
            }else if (type == RENDER_MODE_FASTIMAGE_SURFACE_VIEW) {
                renderView = new MLImageSurfaceView(mContext);
            }else {
                throw new RuntimeException("Invalid render mode.");
            }
            setRenderView(renderView);
            this.mRenderView = renderView;
            mDisplayType = type;
        }
    }

    /**
     * 设置要显示的模式
     * @param mode
     */
    public void setRenderMode(int mode) {
        if (mRenderView != null && mImageContext != null) {
            mImageContext.setRenderMode(mode);
        }
        mRenderMode = mode;
    }

    /**
     * 获取当前图像的绘制方式
     * @return
     */
    public int getRenderMode() {
        return mRenderMode;
    }

    /**
     * 设置要显示的方向
     * @param rotation
     */
    public void setRenderRotation(int rotation) {
        setRenderRotation(rotation , false);
    }

    /**
     * 设置显示的方向和镜像
     * @param rotation
     * @param mirror
     */
    public void setRenderRotation(int rotation , boolean mirror) {
        if (mRenderView != null && mImageContext != null) {
            mImageContext.setVideoRotation(rotation, mirror);
        }
        mRenderMirror = mirror;
        mRenderRotation = rotation;
    }

    /**
     * 设置镜像
     * @param mirror
     */
    public void setRenderMirror(boolean mirror){
        if (mRenderView != null && mImageContext != null) {
            mImageContext.setVideoRotation(mRenderRotation, mirror);
        }
        mRenderMirror = mirror;
    }

    private void setRenderView(IMediaImageView renderView) {
        if (renderView == mRenderView) {
            return;
        }
        if (mRenderView != null) {
            removeView((View)mRenderView);
        }
        if (renderView == null) {
            if (mImageContext != null) {
                mImageContext.setImageView(null);
            }
            return;
        }
        mRenderView = renderView;
        View renderUIView = (View)mRenderView;
        FrameLayout.LayoutParams lp = new FrameLayout.LayoutParams(
                FrameLayout.LayoutParams.WRAP_CONTENT,
                FrameLayout.LayoutParams.WRAP_CONTENT,
                Gravity.CENTER);
        renderUIView.setLayoutParams(lp);
        addView(renderUIView);
        if (mImageContext != null) {
            mImageContext.setImageView(mRenderView);
            mImageContext.setRenderMode(mRenderMode);
            mImageContext.setVideoRotation(mRenderRotation, mRenderMirror);
            mImageContext.enableRoundCorner(mEnableRoundCorner, mCornerRadius);
        }
    }

    public void enableRoundCorner(boolean enable, int cornerRadius) {
        if (mImageContext != null) {
            mImageContext.enableRoundCorner(enable, cornerRadius);
        }
        mEnableRoundCorner = enable;
        mCornerRadius = cornerRadius;
    }

    /**
     * 要绑定的MLImageContext。该接口APP不需要调用，仅供sdk内部使用
     * @param context MediaLive Image Context
     */
    public void bindToMLImageContext(MLImageContext context)
    {
        if (mImageContext == context) {
            return;
        }
        if (mImageContext != null && mImageContext != context) {
            mImageContext.setImageView(null);
        }
        mImageContext = context;
        if (mImageContext != null && mRenderView != null) {
            mImageContext.setImageView(mRenderView);
            mImageContext.setRenderMode(mRenderMode);
            mImageContext.setVideoRotation(mRenderRotation, mRenderMirror);
            mImageContext.enableRoundCorner(mEnableRoundCorner, mCornerRadius);
        }
    }

    @Override
    protected void onVisibilityChanged(View changedView, int visibility){
        super.onWindowVisibilityChanged(visibility);
        if (visibility == View.VISIBLE){
            Log.i(TAG, "emlive video view visible.");
            if (mRenderView != null) {
                if (((View)mRenderView).getParent() == null) {
                    addView((View)mRenderView);
                }
            }
        } else if(visibility == INVISIBLE){
            Log.i(TAG, "emlive video view invisible.");
        } else if (visibility == View.GONE) {
            Log.i(TAG, "emlive video view gone.");
            if (mRenderView != null) {
                if (((View)mRenderView).getParent() == null) {
                    addView((View)mRenderView);
                }
            }
        }
    }

    public boolean viewCreated() {
        if (mRenderView != null) {
            return mRenderView.viewCreated();
        }
        return false;
    }
}
