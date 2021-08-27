package com.eastmoney.rtmp.demo;

import android.app.Application;
/**
 * sdk demo application
 * <p>
 * <略>
 *
 * @author : company
 * @date : 2020/2/13
 */
public class DemoApplication extends Application
{

    private static DemoApplication instance;

    @Override
    public void onCreate() {
        super.onCreate();

        instance = this;
    }

    public static DemoApplication getApplication() {
        return instance;
    }

}
