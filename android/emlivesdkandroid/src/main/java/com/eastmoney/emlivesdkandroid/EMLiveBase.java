package com.eastmoney.emlivesdkandroid;

/**
 * Created by eastmoney on 16/10/21.
 */
public class EMLiveBase {
    public IEMLiveBaseListener listener;
    public static boolean mNativeLoaded = false;
    private static EMLiveBase instance = new EMLiveBase();

    private EMLiveBase() {
    }

    public static EMLiveBase getInstance() {
        return instance;
    }
}
