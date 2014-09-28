package auto.stab.memorystress.memorytest;

import java.io.File;
import java.io.BufferedReader;
import java.io.InputStreamReader;
import java.io.PrintWriter;
import java.io.BufferedWriter;
import java.io.OutputStreamWriter;
import java.io.FileReader;
import java.io.UnsupportedEncodingException;
import java.io.IOException;

import android.graphics.Color;
import android.content.Context;
import android.app.Activity;
import android.os.Build;
import android.view.View;
import android.text.format.Formatter;
import android.util.AttributeSet;
import android.util.Log;
import android.widget.TextView;
import android.widget.ScrollView;
import android.widget.EditText;
import android.widget.Button;
import android.widget.Toast;
import android.text.TextUtils;
import android.net.Uri;
import android.widget.LinearLayout;
import android.os.Handler;
import android.os.Message;
import android.os.Bundle;
import android.os.SystemProperties;
import android.view.WindowManager;

import android.net.LocalSocket;
import android.net.LocalSocketAddress;

import auto.stab.memorystress.memorytest.R;

public class MemoryTestActivity extends Activity{

	public static final String TAG = "MemoryTestActivity";
    private static final String SOCKET_NAME = "memorytester";	
    private static final int UPDATE_LOG = 1;
    private TextView memoryInfo;
    private EditText mEditText;
    private Button mStartTestButton, mStopTestButton;
    private ScrollView mScrollView;
   
    private StringBuffer sb = new StringBuffer();
    private Handler mHandler;
    private Process mProcess;
    private BufferedReader in;
    private PrintWriter out;
    private MemoryAgingThread mMemoryAgingThread = null;
    private LocalSocket client = null;
    private LocalSocketAddress address;

    @Override
    protected void onCreate(Bundle savedInstanceState){
        super.onCreate(savedInstanceState);
        setContentView(R.layout.memory_test);
        initViews();
        getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
    }

    @Override
    public void onResume(){
        super.onResume();
    }

    @Override
    public void onPause(){
        super.onPause();
    }

    @Override
    public void onDestroy(){
        //SystemProperties.set("ctl.stop", "memorytester");
        super.onDestroy();
    }

    private void initViews(){
        mEditText = (EditText) findViewById(R.id.memory_test_size);
        mStartTestButton = (Button) findViewById(R.id.memory_test_start);
        mStopTestButton = (Button) findViewById(R.id.memory_test_stop);
        memoryInfo = (TextView) findViewById(R.id.memory_info);
        mScrollView = (ScrollView) findViewById(R.id.memory_test_scroll_info);
        mHandler = new Handler(){
           @Override
           public void handleMessage(Message msg){
               switch(msg.what){
                case UPDATE_LOG:
                    String line = (String) msg.obj;
                    sb.append(line + "\n");
                    Log.d(TAG, line);
                    memoryInfo.setText(sb.toString());
                    scrollToBottom();
                    break;
                default:
                    break;
               }
           }
       };
       mStartTestButton.setOnClickListener(new View.OnClickListener(){
           @Override
           public void onClick(View v){
               SystemProperties.set("ctl.start", "memorytester");
               if(null == mMemoryAgingThread){
                   mMemoryAgingThread = new MemoryAgingThread();
               }
               mHandler.postDelayed(new Runnable(){
                   @Override
                   public void run(){
                       startMemoryTest();
                   }
               }, 1000);
           }
       });

       mStopTestButton.setOnClickListener(new View.OnClickListener(){
           @Override
           public void onClick(View v){
               if(out != null){
                   out.println("stop");
                   out.flush();
               }
               mHandler.postDelayed(new Runnable(){
                   @Override
                   public void run(){
                      SystemProperties.set("ctl.stop", "memorytester"); 
                   }
               }, 1000);
               Toast.makeText(MemoryTestActivity.this, "stop memory test!", Toast.LENGTH_SHORT).show();
               stopTest();
           }
       });
    }

	public void startMemoryTest() {
        String memorySizeStr = "";
        memorySizeStr = mEditText.getText().toString();
        if(TextUtils.isEmpty(memorySizeStr)){
           Toast.makeText(MemoryTestActivity.this, getResources().getString(R.string.memory_test_size_is_null_alert), Toast.LENGTH_SHORT).show();
           return;
        }
        try{
            client = new LocalSocket();
            address = new LocalSocketAddress(SOCKET_NAME, LocalSocketAddress.Namespace.RESERVED);
            client.connect(address);
            InputStreamReader isr = new InputStreamReader(client.getInputStream());
            in = new BufferedReader(isr);
            out = new PrintWriter(client.getOutputStream());
            String message = "-p " + memorySizeStr + "M"; // read memory size & loop times
            out.println(message); //send message to start socket server
            out.flush();
        }catch(IOException e){
            Log.d(TAG, e.toString());
        }
        mMemoryAgingThread.setIn(in);
        mMemoryAgingThread.setHandler(mHandler);
        mMemoryAgingThread.start();
        mMemoryAgingThread.setRunningState(MemoryAgingThread.RUNNING);
    }

    public void stopTest(){
        //stop memory aging thread, close resources of socket
        if(null != mMemoryAgingThread){
            mMemoryAgingThread.setRunningState(MemoryAgingThread.EXIT);
            mMemoryAgingThread = null;
        }
    }

    private void scrollToBottom(){
        if(mScrollView == null || memoryInfo == null){
            Log.d(TAG, "scroll view | memoryInfo text is null");
            return;
        }
        int offset = memoryInfo.getMeasuredHeight() - mScrollView.getMeasuredHeight();
        if(offset < 0){
            offset = 0;
        }
        Log.d(TAG, "offset = " + offset);
        mScrollView.scrollTo(0, offset);
    }

    private class MemoryAgingThread extends Thread {
        
        private static final String TAG = "MemoryTestActivity:MemoryAgingThread";

        public static final int NO_START = 0;
        public static final int RUNNING = 1;
        public static final int PAUSE = 2;
        public static final int EXIT = 3;

        private BufferedReader in;
        private Handler mHandler;

        private int runningState = NO_START;
        private Object obj = new Object();

        public void setRunningState(int state){
            synchronized(obj){
                runningState = state;
            }
        }

        public int getRunningState(){
            synchronized(obj){
                return runningState;
            }
        }

        public void setIn(BufferedReader in){
            this.in = in;
        }

        public BufferedReader getIn(){
            return in;
        }

        public void setHandler(Handler handler){
            this.mHandler = handler;
        }

        public Handler getHandler(){
            return mHandler;
        }

        private synchronized void doReadStreamFromBuffer(){
            String line = "";
            //read from inputStream
            try{
                Log.d(TAG, "reading input Stream:");
                while((line = in.readLine()) != null){
                    Message msg = mHandler.obtainMessage();
                    msg.what = MemoryTestActivity.UPDATE_LOG;
                    msg.obj = line;
                    mHandler.sendMessage(msg);
                }
            }catch(IOException e){
                Log.d(TAG, e.toString());
            }
        }

        @Override
        public void run(){
            loopRun();
        }

        public synchronized void loopRun(){
            while(true){
                if(getRunningState() == NO_START){
                    try{
                        wait(500);
                    }catch(Exception e){
                        Log.d(TAG, e.toString());
                    }
                } else if (getRunningState() == RUNNING){
                    try{
                        doReadStreamFromBuffer();
                    }catch(Exception e){
                        Log.d(TAG, e.toString());
                    }
                } else if (getRunningState() == PAUSE){
                    try{
                        wait(1000);
                    }catch(Exception e){
                        Log.d(TAG, e.toString());
                    }
                } else if (getRunningState() == EXIT){
                    Log.d(TAG, "stop memory read thread!");
                    break;
                }
            }
        }
    }
}
