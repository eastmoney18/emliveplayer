package com.eastmoney.emlivesdkandroid;

import android.os.Bundle;

/**
 * Created by eastmoney on 16/10/21.
 */
public interface IEMLivePlayListener {

    /**
     * 播放事件回调
     * @param evt 事件类型
     * @param bundle 其他参数
     */
    void onPlayEvent(int evt, Bundle bundle);

    /**
     * 网络参数回调
     * @param bundle 各个参数值
     */
    void onNetStatus(Bundle bundle);
}
