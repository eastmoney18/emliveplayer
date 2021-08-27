package com.eastmoney.emlivesdkandroid;

import android.graphics.Bitmap;

/**
 * Created by eastmoney on 17/6/5.
 */

/**
 * 拍照回调接口
 */
public interface IEMLiveTakePictureListener {
    /**
     * 拍照时，获取图片的回调方法，主线程下执行
     * @param bitmap 当前的拍照图片
     * @param width 图片宽度
     * @param height 图片高度
     */
    public void onPictureTaked(Bitmap bitmap, int width, int height);
}
