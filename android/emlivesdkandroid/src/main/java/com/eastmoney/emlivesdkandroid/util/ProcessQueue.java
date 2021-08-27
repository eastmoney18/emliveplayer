package com.eastmoney.emlivesdkandroid.util;

import android.os.Build;
import android.util.Log;

import java.util.Queue;
import java.util.concurrent.LinkedBlockingQueue;

public class ProcessQueue
        implements Runnable
{
    public static final String logTag = "ProcessQueue";
    private Thread mRunThread;
    private Queue<ExcuteBlockUnit> mExcuteBlockUnits;
    private String queneName;
    private Object mSync;
    private boolean mNeedStop = false;
    private boolean mWaitAllTask = false;
    private int mMaxExcuteBlock;

    public ProcessQueue(String name)
    {
        this(name, -1);
    }

    public ProcessQueue(String name, int maxExcute) {
        this.queneName = name;
        this.mSync = new Object();
        this.mMaxExcuteBlock = maxExcute;
        this.mExcuteBlockUnits = new LinkedBlockingQueue();
    }

    public void Start() {
        this.mRunThread = new Thread(this, this.queneName);
        this.mRunThread.start();
    }

    public void Stop(boolean wait) {
        synchronized (this.mSync) {
            this.mNeedStop = true;
            this.mWaitAllTask = true;
            this.mSync.notifyAll();
        }
        if (wait) {
            try {
                this.mRunThread.join();
            } catch (InterruptedException e) {
                e.printStackTrace();
            }
        }
        this.mRunThread = null;
    }

    public void Stop() {
        synchronized (this.mSync) {
            this.mNeedStop = true;
            this.mWaitAllTask = false;
            this.mSync.notifyAll();
        }
        try {
            this.mRunThread.join();
        } catch (InterruptedException e) {
            e.printStackTrace();
        }
        this.mRunThread = null;
    }

    public int runSync(excuteBlock block, int waitMilis) {
        boolean isCallFromMain = false;
        if ((this.mRunThread != null) && (this.mRunThread.getState() != Thread.State.NEW) && (this.mRunThread.getState() != Thread.State.TERMINATED)) {
            if (Thread.currentThread().getId() == this.mRunThread.getId()) {
                block.excute();
                return 0;
            }if ((Build.VERSION.SDK_INT < 21) && (Thread.currentThread().getName().contentEquals("main")) && (Thread.currentThread().getThreadGroup().getName().contentEquals("main"))) {
                Log.w("ProcessQueue", "Android api under 21 can not runSync on main thread.");
                isCallFromMain = true;
            }
        }
        ExcuteBlockUnit unit = new ExcuteBlockUnit(block, true);
        synchronized (this.mSync) {
            this.mExcuteBlockUnits.add(unit);
            this.mSync.notifyAll();
        }
        synchronized (unit.mBlockSync) {
            if (!unit.mExcuteComplete) {
                try {
                    if (waitMilis > 0) {
                        unit.mBlockSync.wait(waitMilis);
                    } else if (isCallFromMain) {
                        int waitTime = 0;
                        while ((waitTime < 1000) && (!unit.mExcuteComplete)) {
                            unit.mBlockSync.wait(100L);
                            waitTime += 100;
                        }
                        if (waitTime >= 1000)
                            Log.e("ProcessQueue", "wait excute block finish failed.");
                    }
                    else {
                        unit.mBlockSync.wait();
                    }
                } catch (InterruptedException e) {
                    e.printStackTrace();
                }
            }
            if (!unit.mExcuteComplete) {
                Log.e("ProcessQueue", "excute block is not complete after " + waitMilis + "ms.");
                return -1;
            }
        }
        return 0;
    }

    public int runSync(excuteBlock block) {
        return runSync(block, 0);
    }

    public boolean runAsync(excuteBlock block) {
        synchronized (this.mSync) {
            int excuteBlockCount = this.mExcuteBlockUnits.size();
            if ((this.mMaxExcuteBlock > 0) && (excuteBlockCount == this.mMaxExcuteBlock)) {
                Log.d("ProcessQueue", "Processing queue exceedmax excute block size.");
                return false;
            }
            ExcuteBlockUnit unit = new ExcuteBlockUnit(block, false);
            this.mExcuteBlockUnits.add(unit);
            this.mSync.notifyAll();
            return true;
        }
    }

    public void emptyTask() {
        synchronized (this.mSync) {
            for (ExcuteBlockUnit unitBlock : this.mExcuteBlockUnits) {
                synchronized (unitBlock.mBlockSync) {
                    unitBlock.mExcuteComplete = true;
                    unitBlock.mBlockSync.notifyAll();
                }
            }
            this.mExcuteBlockUnits.clear();
        }
    }

    public void run()
    {
        boolean needStop = false;
        boolean waitAllTask = false;
        int taskCount = 0;
        while (true)
        {
            synchronized (this.mSync) {
                needStop = this.mNeedStop;
                waitAllTask = this.mWaitAllTask;
                taskCount = this.mExcuteBlockUnits.size();
                if ((taskCount <= 0) && (!needStop)) {
                    try {
                        this.mSync.wait();
                    } catch (InterruptedException e) {
                        e.printStackTrace();
                    }
                    continue;
                }
            }
            if (needStop) {
                if (!waitAllTask)
                    break;
                if (taskCount <= 0)
                    break;
            }
            if (taskCount > 0)
            {
                ExcuteBlockUnit unitBlock;
                synchronized (this.mSync) {
                    unitBlock = (ExcuteBlockUnit)this.mExcuteBlockUnits.poll();
                }
                if (unitBlock != null) {
                    unitBlock.mBlock.excute();
                    synchronized (unitBlock.mBlockSync) {
                        unitBlock.mExcuteComplete = true;
                        unitBlock.mBlockSync.notifyAll();
                    }
                }
            }
        }
        Log.e(getClass().getName(), "exit process queue " + this.queneName);
        synchronized (this.mSync) {
            this.mExcuteBlockUnits.clear();
        }
    }

    private class ExcuteBlockUnit
    {
        private ProcessQueue.excuteBlock mBlock;
        private Object mBlockSync;
        private boolean mNeedWait = false;
        private boolean mExcuteComplete = false;

        public ExcuteBlockUnit(ProcessQueue.excuteBlock block, boolean sync) { this.mBlock = block;
            this.mBlockSync = new Object();
            this.mNeedWait = sync;
            this.mExcuteComplete = false;
        }
    }

    public static abstract interface excuteBlock
    {
        public abstract void excute();
    }
}