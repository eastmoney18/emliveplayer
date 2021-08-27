package com.eastmoney.rtmp.demo;

import android.Manifest;
import android.app.Activity;
import android.content.Intent;
import android.content.pm.PackageManager;
import android.os.Build;
import android.os.Bundle;
import android.view.View;
import android.view.View.OnClickListener;

import com.eastmoney.rtmp.demo.play.LivePlayerActivity;

import org.apache.log4j.Logger;

/**
 * sdk demo主列表界面
 * <p>
 * <略>
 *
 * @author : company
 * @date : 2020/2/13
 */
public class MainActivity extends Activity {

    private static final String TAG = MainActivity.class.getName();

    Logger logger = Logger.getLogger(MainActivity.class);

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);

        setContentView(R.layout.activity_main);
        findViewById(R.id.btnPlayTest).setOnClickListener(new OnClickListener() {
            @Override
            public void onClick(View v) {
                final Intent intent = new Intent(MainActivity.this, LivePlayerActivity.class);
                intent.putExtra("TITLE", "测试");
                MainActivity.this.startActivity(intent);
            }
        });

        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.M) {
            if (checkSelfPermission(Manifest.permission.WRITE_EXTERNAL_STORAGE) != PackageManager.PERMISSION_GRANTED) {
                requestPermissions(new String[] {Manifest.permission.WRITE_EXTERNAL_STORAGE,
                        Manifest.permission.CAMERA}, 1);
            }
        }
    }
}
