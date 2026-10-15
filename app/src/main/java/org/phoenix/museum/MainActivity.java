package org.phoenix.museum;

import android.app.*;
import android.os.*;
import android.content.*;
import android.content.res.AssetManager;
import android.graphics.Color;
import android.graphics.Bitmap;
import android.opengl.GLES30;
import android.provider.MediaStore;
import android.opengl.GLSurfaceView;
import android.view.*;
import android.widget.*;
import org.json.*;
import java.io.*;
import java.util.*;
import java.nio.ByteBuffer;
import javax.microedition.khronos.egl.EGLConfig;
import javax.microedition.khronos.opengles.GL10;

public class MainActivity extends Activity {
    static { System.loadLibrary("museum"); }
    static native void initialize(AssetManager assets, String files, String nativeDir);
    static native String render(String options, float yaw, float pitch, float zoom, int width, int height);
    static native String status();
    static native void pauseProcessing(boolean pause);
    static native void saveCapture();
    static native String devices();
    static native void contextCreated();
    final Handler handler=new Handler(Looper.getMainLooper());
    GLSurfaceView surface; TextView stats,notice,leftBadge,rightBadge; LinearLayout menu; ScrollView scroller;
    volatile String options="{}"; volatile float yaw=.42f,pitch=0,zoom=7.6f;
    volatile boolean ready=false,capturePending=false; JSONObject config=new JSONObject();
    final String[] qualities={"Very light","Middle","Beauty","Light + detail","Light + detail + soft shadows","Room bounce + contact","Sparse GI + reflections"};
    final String[] neural={"Phoenix off","Protected surfaces","Full frame"};
    ArrayList<String> labels=new ArrayList<>();
    String selectedDevice=""; int focused=-1; long fpsStart=0; int fpsFrames=0; volatile double fps=0;
    private int dp(float n){return (int)(getResources().getDisplayMetrics().density*n+.5f);}
    void put(String key,Object value){try{config.put(key,value); options=config.toString(); getPreferences(0).edit().putString("options",options).apply();}catch(Exception e){throw new RuntimeException(e);}}
    @Override public void onCreate(Bundle state){
        super.onCreate(state);
        getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        getWindow().getDecorView().setSystemUiVisibility(5894);
        try {config=new JSONObject(getPreferences(0).getString("options","{}"));}catch(Exception ignored){}
        if(!config.has("backend")) {put("backend","vulkan");put("view",1);put("resolution",320);put("left_quality",3);put("right_quality",3);put("left_phoenix",0);put("right_phoenix",0);put("animation",true);put("faces",true);put("lights",true);put("orbit",false);put("focus",-1);put("framing",0);put("left_detail",1);put("right_detail",1);put("passes",1);put("repeats",new JSONArray(Arrays.asList(1,1,1,1,1,1,1,1)));put("intensity",1.0);put("local_tone",1.0);put("local_structure",1.0);put("skin_structure",-1.0);put("auto_mask",true);put("style",0);put("seed",0);put("cpu_threads",4);}
        try {String overrides=getIntent().getStringExtra("test_options");if(overrides!=null){JSONObject changes=new JSONObject(overrides);Iterator<String> keys=changes.keys();while(keys.hasNext()){String key=keys.next();config.put(key,changes.get(key));}}}catch(Exception e){throw new RuntimeException("Invalid test options",e);}
        options=config.toString(); focused=config.optInt("focus",-1);zoom=focused<0?7.6f:1f;yaw=focused<0?.42f:.3f;
        FrameLayout root=new FrameLayout(this);setContentView(root);
        surface=new GLSurfaceView(this); surface.setEGLContextClientVersion(3);surface.setPreserveEGLContextOnPause(true);
        surface.setRenderer(new GLSurfaceView.Renderer(){
            public void onSurfaceCreated(GL10 gl,EGLConfig cfg){contextCreated();}
            int w=1,h=1;
            public void onSurfaceChanged(GL10 gl,int width,int height){w=width;h=height;}
            public void onDrawFrame(GL10 gl){
                if(!ready||w<h)return;
                String error=render(options,yaw,pitch,zoom,w,h);
                if(!error.isEmpty())handler.post(()->notice.setText(error));
                if(capturePending&&error.isEmpty()){capturePending=false;captureImage(w,h);}
                long now=System.nanoTime();if(fpsStart==0)fpsStart=now;
                fpsFrames++;if(now-fpsStart>500000000L){fps=fpsFrames*1e9/(now-fpsStart);fpsFrames=0;fpsStart=now;}
            }
        });root.addView(surface);
        stats=new TextView(this);stats.setTextColor(Color.WHITE);stats.setTextSize(13);stats.setPadding(dp(10),dp(6),dp(10),dp(6));stats.setBackgroundColor(0xb8000000);
        FrameLayout.LayoutParams sp=new FrameLayout.LayoutParams(-2,-2,Gravity.TOP|Gravity.RIGHT);sp.setMargins(0,dp(10),dp(12),0);root.addView(stats,sp);
        notice=new TextView(this);notice.setText("Preparing museum assets…");notice.setTextColor(0xffffcf7e);notice.setTextSize(12);notice.setBackgroundColor(0xb8000000);notice.setPadding(dp(10),dp(6),dp(10),dp(6));
        FrameLayout.LayoutParams np=new FrameLayout.LayoutParams(-2,-2,Gravity.BOTTOM|Gravity.CENTER_HORIZONTAL);np.setMargins(dp(20),0,dp(20),dp(10));root.addView(notice,np);
        LinearLayout badges=new LinearLayout(this);badges.setOrientation(0);leftBadge=badge();rightBadge=badge();badges.addView(leftBadge,new LinearLayout.LayoutParams(0,-2,1));badges.addView(rightBadge,new LinearLayout.LayoutParams(0,-2,1));FrameLayout.LayoutParams bp=new FrameLayout.LayoutParams(-1,-2,Gravity.BOTTOM);bp.setMargins(dp(16),0,dp(16),dp(48));root.addView(badges,bp);
        LinearLayout panel=new LinearLayout(this);panel.setOrientation(1);panel.setBackgroundColor(0xe51b2029);
        FrameLayout.LayoutParams pp=new FrameLayout.LayoutParams(dp(296),-1,Gravity.LEFT);pp.setMargins(dp(8),dp(8),0,dp(8));root.addView(panel,pp);
        Button toggle=new Button(this);toggle.setText("Museum controls ▾");panel.addView(toggle,new LinearLayout.LayoutParams(-1,dp(46)));
        scroller=new ScrollView(this);menu=new LinearLayout(this);menu.setPadding(dp(10),0,dp(10),dp(16));menu.setOrientation(1);scroller.addView(menu);panel.addView(scroller,new LinearLayout.LayoutParams(-1,0,1));scroller.setVisibility(View.GONE);pp.height=dp(48);panel.setLayoutParams(pp);
        toggle.setOnClickListener(v->{boolean show=scroller.getVisibility()!=View.VISIBLE;scroller.setVisibility(show?View.VISIBLE:View.GONE);pp.height=show?-1:dp(48);panel.setLayoutParams(pp);toggle.setText(show?"Museum controls ▴":"Museum controls ▾");});
        ScaleGestureDetector pinch=new ScaleGestureDetector(this,new ScaleGestureDetector.SimpleOnScaleGestureListener(){public boolean onScale(ScaleGestureDetector d){zoom=Math.max(focused<0?2.4f:.35f,Math.min(focused<0?18f:3f,zoom/d.getScaleFactor()));return true;}});
        surface.setOnTouchListener(new View.OnTouchListener(){float x,y;public boolean onTouch(View v,android.view.MotionEvent e){pinch.onTouchEvent(e);if(e.getActionMasked()==MotionEvent.ACTION_DOWN){x=e.getX();y=e.getY();}else if(e.getActionMasked()==MotionEvent.ACTION_MOVE&&!pinch.isInProgress()&&e.getPointerCount()==1){yaw+=(e.getX()-x)*.006f;pitch=Math.max(-.8f,Math.min(.8f,pitch+(e.getY()-y)*.004f));x=e.getX();y=e.getY();}return true;}});
        new Thread(()->{try{
            copyAssets("runtime",new File(getFilesDir(),"runtime"));copyAssets("model",new File(getFilesDir(),"model"));copyAssets("model.json",new File(getFilesDir(),"model.json"));
            initialize(getAssets(),getFilesDir().getAbsolutePath(),getApplicationInfo().nativeLibraryDir);
            JSONObject catalogue=new JSONObject(readAsset("catalogue.json"));labels.add("Gallery overview");JSONArray people=catalogue.getJSONArray("people");for(int i=0;i<people.length();i++)labels.add(people.getJSONObject(i).getString("label"));labels.add("Beagle exhibit");labels.add("Deer exhibit");
            JSONArray ds=new JSONArray(devices());for(int i=0;i<ds.length();i++){JSONObject d=ds.getJSONObject(i);if(selectedDevice.isEmpty()){selectedDevice=d.getString("id");}}
            handler.post(()->{put("device",selectedDevice);buildMenu();ready=true;notice.setText("Drag to orbit · pinch to zoom · Phoenix starts off");});
        }catch(Exception e){handler.post(()->notice.setText("Setup failed: "+e.getMessage()));}},"Museum setup").start();
        handler.postDelayed(new Runnable(){public void run(){if(ready){try{JSONObject s=new JSONObject(status());String state=s.optString("state","idle");String backend=config.optString("backend");stats.setText(String.format(Locale.US,"%.1f display FPS\n%s · %d px · %s\n%.2f Phoenix FPS · %.1f ms\nResult age %.1f s",fps,backend,config.optInt("resolution"),state,s.optDouble("inference_fps"),s.optDouble("frame_ms"),s.optDouble("age_seconds")));
                    updateBadge(leftBadge,s,0);updateBadge(rightBadge,s,1);
                    String error=s.optString("error"); if(!error.isEmpty())notice.setText(error);else if(state.equals("processing")||state.equals("loading")||state.equals("finishing previous call"))notice.setText(String.format(Locale.US,"Phoenix %s · %s · %.0f s · waiting for model call",s.optString("active_backend",backend),state,s.optDouble("processing_seconds")));else if(config.optInt("left_phoenix",0)==0&&config.optInt("right_phoenix",0)==0)notice.setText("PHOENIX OFF — original scene only");else notice.setText("Processed result held for comparison · result age shown above");
                    File f=new File(getExternalFilesDir(null),"status.json");try(FileWriter out=new FileWriter(f)){s.put("display_fps",fps);s.put("config",config);out.write(s.toString(2));}
                }catch(Exception ignored){}}handler.postDelayed(this,500);}},500);
    }
    TextView badge(){TextView t=new TextView(this);t.setTextColor(Color.WHITE);t.setTextSize(14);t.setGravity(Gravity.CENTER);t.setPadding(dp(10),dp(8),dp(10),dp(8));t.setBackgroundColor(0xcc272c34);return t;}
    void updateBadge(TextView badge,JSONObject status,int side)throws JSONException{
        JSONArray sides=status.optJSONArray("displayed_sides");if(sides==null||sides.length()<=side)return;JSONObject row=sides.getJSONObject(side);badge.setVisibility(row.optBoolean("visible")?View.VISIBLE:View.INVISIBLE);String prefix=side==0?"LEFT · ":"RIGHT · ";
        if(row.optString("content").equals("phoenix")){badge.setBackgroundColor(0xe968399d);badge.setText(prefix+"PHOENIX "+config.optString("backend").toUpperCase(Locale.US)+" · RESULT #"+row.optLong("serial"));}
        else if(row.optBoolean("requested_phoenix")){badge.setBackgroundColor(0xe87b5012);badge.setText(prefix+(status.optString("error").isEmpty()?"WAITING — ORIGINAL SHOWN":"UNAVAILABLE — ORIGINAL SHOWN"));}
        else {badge.setBackgroundColor(0xcc272c34);badge.setText(prefix+"ORIGINAL"+(row.optBoolean("paired_source")?" · MATCHED "+row.optInt("width")+"×"+row.optInt("height"):" · PHOENIX OFF"));}
    }
    void compareSnapshot(){try{config.put("view",0);config.put("left_phoenix",0);config.put("right_phoenix",2);config.put("left_quality",config.optInt("right_quality",3));config.put("left_detail",config.optInt("right_detail",1));config.put("freeze",true);config.put("same_resolution",true);config.put("history",false);config.put("reset_token",System.nanoTime());options=config.toString();getPreferences(0).edit().putString("options",options).apply();menu.removeAllViews();buildMenu();}catch(Exception e){throw new RuntimeException(e);}}
    @Override protected void onNewIntent(Intent intent){super.onNewIntent(intent);setIntent(intent);String text=intent.getStringExtra("test_options");if(text==null)return;try{JSONObject changes=new JSONObject(text);Iterator<String> keys=changes.keys();while(keys.hasNext()){String key=keys.next();config.put(key,changes.get(key));}focused=config.optInt("focus",-1);zoom=focused<0?7.6f:1;yaw=focused<0?.42f:.3f;pitch=0;options=config.toString();getPreferences(0).edit().putString("options",options).apply();if(menu!=null&&ready){menu.removeAllViews();buildMenu();}}catch(JSONException e){notice.setText("Could not apply comparison settings: "+e.getMessage());}}
    void title(String label){TextView t=new TextView(this);t.setText(label);t.setTextColor(0xffcbbcff);t.setTextSize(13);t.setPadding(0,dp(16),0,dp(4));menu.addView(t);}
    void choice(String title,String key,String[] entries,int selected){title(title);Spinner spinner=new Spinner(this);ArrayAdapter<String> adapter=new ArrayAdapter<>(this,android.R.layout.simple_spinner_dropdown_item,entries);spinner.setAdapter(adapter);spinner.setSelection(selected);menu.addView(spinner,new LinearLayout.LayoutParams(-1,dp(44)));spinner.setOnItemSelectedListener(new AdapterView.OnItemSelectedListener(){public void onNothingSelected(AdapterView<?> p){}public void onItemSelected(AdapterView<?> p,View v,int pos,long id){put(key,pos);}});}
    void flag(String label,String key,boolean def){Switch b=new Switch(this);b.setText(label);b.setTextColor(Color.WHITE);b.setChecked(config.optBoolean(key,def));menu.addView(b,new LinearLayout.LayoutParams(-1,dp(48)));b.setOnCheckedChangeListener((v,on)->put(key,on));}
    void button(String text,Runnable action){Button b=new Button(this);b.setText(text);menu.addView(b,new LinearLayout.LayoutParams(-1,dp(44)));b.setOnClickListener(v->action.run());}
    void slider(String label,String key,float minimum,float maximum,float def){TextView text=new TextView(this);text.setTextColor(Color.WHITE);menu.addView(text);SeekBar b=new SeekBar(this);b.setMax(100);b.setProgress(Math.round((float)(config.optDouble(key,def)-minimum)/(maximum-minimum)*100));menu.addView(b);b.setOnSeekBarChangeListener(new SeekBar.OnSeekBarChangeListener(){public void onStartTrackingTouch(SeekBar v){}public void onStopTrackingTouch(SeekBar v){}public void onProgressChanged(SeekBar v,int progress,boolean user){float value=minimum+(maximum-minimum)*progress/100; text.setText(String.format(Locale.US,"%s %.2f",label,value));if(user)put(key,value);}});text.setText(String.format(Locale.US,"%s %.2f",label,config.optDouble(key,def)));}
    void buildMenu(){
        button("Freeze & compare original / Phoenix",()->compareSnapshot());
        button("Resume original live scene",()->{try{config.put("left_phoenix",0);config.put("right_phoenix",0);config.put("view",1);config.put("freeze",false);options=config.toString();getPreferences(0).edit().putString("options",options).apply();menu.removeAllViews();buildMenu();}catch(Exception e){throw new RuntimeException(e);}});
        title("PHOENIX BACKEND");Spinner backend=new Spinner(this);backend.setAdapter(new ArrayAdapter<>(this,android.R.layout.simple_spinner_dropdown_item,new String[]{"Vulkan GPU","CPU reference"}));backend.setSelection(config.optString("backend").equals("cpu")?1:0);menu.addView(backend);backend.setOnItemSelectedListener(new AdapterView.OnItemSelectedListener(){public void onNothingSelected(AdapterView<?> v){}public void onItemSelected(AdapterView<?> v,View w,int p,long id){put("backend",p==0?"vulkan":"cpu");}});
        flag("Vulkan compatibility mode (64/128-thread GPUs)","vulkan_workgroup_compatibility",false);
        try{JSONArray ds=new JSONArray(devices());TextView d=new TextView(this);d.setTextColor(Color.LTGRAY);d.setText(ds.length()>0?ds.getJSONObject(0).getString("name")+(ds.getJSONObject(0).getBoolean("supported")?" — standard Vulkan supported":ds.getJSONObject(0).optBoolean("compatibility_supported")?" — optional compatibility mode available":" — required compute features unavailable"):"No Vulkan device available");menu.addView(d);}catch(Exception ignored){}
        title("PROCESSING RESOLUTION");Spinner res=new Spinner(this);int[] sizes={160,320,640,960,1280,1920};res.setAdapter(new ArrayAdapter<>(this,android.R.layout.simple_spinner_dropdown_item,new String[]{"160 wide","320 wide","640 wide","960 wide","1280 wide","1920 wide"}));int index=1;for(int i=0;i<sizes.length;i++)if(config.optInt("resolution")==sizes[i])index=i;res.setSelection(index);menu.addView(res);res.setOnItemSelectedListener(new AdapterView.OnItemSelectedListener(){public void onNothingSelected(AdapterView<?> v){}public void onItemSelected(AdapterView<?> v,View w,int p,long id){put("resolution",sizes[p]);}});
        choice("VIEW","view",new String[]{"Compare left / right","Left only","Right only","Difference"},config.optInt("view",1));
        flag("Compare at the same processing resolution","same_resolution",false);
        for(String side:new String[]{"left","right"}){title(side.toUpperCase(Locale.US));choice("Rendering quality (mobile)",side+"_quality",qualities,config.optInt(side+"_quality",3));choice("Phoenix",side+"_phoenix",neural,config.optInt(side+"_phoenix"));choice("Character body detail",side+"_detail",new String[]{"Full meshes","Distance detail","Reduced bodies"},config.optInt(side+"_detail",1));}
        title("SCENE");flag("Animate visitors & exhibits","animation",true);flag("Animate facial expressions","faces",true);flag("Animate lighting","lights",true);flag("Orbit camera","orbit",false);
        title("FOCUS");Spinner focus=new Spinner(this);focus.setAdapter(new ArrayAdapter<>(this,android.R.layout.simple_spinner_dropdown_item,labels));focus.setSelection(config.optInt("focus",-1)+1);menu.addView(focus);focus.setOnItemSelectedListener(new AdapterView.OnItemSelectedListener(){public void onNothingSelected(AdapterView<?> v){}public void onItemSelected(AdapterView<?> v,View w,int p,long id){int f=p-1;if(f!=focused){focused=f;yaw=.3f;pitch=0;zoom=f<0?7.6f:1;}put("focus",f);}});
        choice("Framing","framing",new String[]{"Whole person","Upper body","Face"},config.optInt("framing",0));
        button("Reset camera",()->{yaw=focused<0?.42f:.3f;pitch=0;zoom=focused<0?7.6f:1;});button("Save image & measurements",()->{saveCapture();capturePending=true;});
        title("PHOENIX CONTROLS");slider("Intensity","intensity",0,2,1);slider("Local tone","local_tone",0,2,1);slider("Local structure","local_structure",0,2,1);slider("Skin structure","skin_structure",-1,2,-1);flag("Automatic mask","auto_mask",true);
        choice("Style","style",new String[]{"Default","Style 1","Style 2"},config.optInt("style",0));
        title("Noise seed");EditText seed=new EditText(this);seed.setSingleLine(true);seed.setInputType(android.text.InputType.TYPE_CLASS_NUMBER);seed.setText(Integer.toString(config.optInt("seed",0)));menu.addView(seed);seed.setOnFocusChangeListener((v,hasFocus)->{if(!hasFocus){try{put("seed",Math.max(0,Integer.parseInt(seed.getText().toString())));}catch(NumberFormatException ignored){seed.setText(Integer.toString(config.optInt("seed",0)));}}});
        title("Passes");Spinner passes=new Spinner(this);passes.setAdapter(new ArrayAdapter<>(this,android.R.layout.simple_spinner_dropdown_item,new String[]{"1","2","3","4"}));passes.setSelection(config.optInt("passes",1)-1);menu.addView(passes);passes.setOnItemSelectedListener(new AdapterView.OnItemSelectedListener(){public void onNothingSelected(AdapterView<?> v){}public void onItemSelected(AdapterView<?> v,View w,int p,long id){put("passes",p+1);}});
        title("BLOCK REPETITIONS (0 skips a block)");for(int i=0;i<8;i++){final int block=i;SeekBar b=new SeekBar(this);b.setMax(4);JSONArray repeats=config.optJSONArray("repeats");b.setProgress(repeats==null?1:repeats.optInt(i,1));TextView t=new TextView(this);t.setTextColor(Color.WHITE);t.setText("Block "+(i+1)+": "+b.getProgress());menu.addView(t);menu.addView(b);b.setOnSeekBarChangeListener(new SeekBar.OnSeekBarChangeListener(){public void onStartTrackingTouch(SeekBar v){}public void onStopTrackingTouch(SeekBar v){}public void onProgressChanged(SeekBar v,int value,boolean user){t.setText("Block "+(block+1)+": "+value);if(user){JSONArray r=config.optJSONArray("repeats");try{r.put(block,value);put("repeats",r);}catch(Exception ignored){}}}});}
        title("CPU THREADS");SeekBar threads=new SeekBar(this);threads.setMax(15);threads.setProgress(config.optInt("cpu_threads",4)-1);TextView tt=new TextView(this);tt.setTextColor(Color.WHITE);tt.setText("Threads: "+config.optInt("cpu_threads",4));menu.addView(tt);menu.addView(threads);threads.setOnSeekBarChangeListener(new SeekBar.OnSeekBarChangeListener(){public void onStartTrackingTouch(SeekBar v){}public void onStopTrackingTouch(SeekBar v){}public void onProgressChanged(SeekBar v,int value,boolean user){tt.setText("Threads: "+(value+1));if(user)put("cpu_threads",value+1);}});
        flag("Use temporal history","history",true);flag("Freeze source for matched benchmark","freeze",false);
        button("Reset neural history",()->put("reset_token",System.nanoTime()));
        title("ABOUT");TextView about=new TextView(this);about.setTextColor(Color.LTGRAY);about.setText("Original museum meshes, layout and animations. The scene renders at the native display resolution; the separate Phoenix width controls inference only. Low Phoenix widths produce a softer processed image. Mobile OpenGL renderer; CPU/Vulkan inference runs on a worker. Display FPS measures presentation; Phoenix FPS counts completed model calls per second, including capture/render scheduling gaps. Processed views hold the exact source/result pair while inference is busy. Two enabled comparison sides reset temporal history when switching sides because the Vulkan runtime owns one session. Mobile shadows, GI and reflections differ from Windows DXR.\n\nCredits and licenses are included in the APK.");menu.addView(about);
    }
    String readAsset(String name)throws IOException{try(InputStream in=getAssets().open(name);ByteArrayOutputStream out=new ByteArrayOutputStream()){byte[] buffer=new byte[8192];int n;while((n=in.read(buffer))>0)out.write(buffer,0,n);return out.toString("UTF-8");}}
    void captureImage(int width,int height){
        ByteBuffer raw=ByteBuffer.allocateDirect(width*height*4);GLES30.glReadBuffer(GLES30.GL_BACK);GLES30.glReadPixels(0,0,width,height,GLES30.GL_RGBA,GLES30.GL_UNSIGNED_BYTE,raw);
        int[] pixels=new int[width*height];for(int y=0;y<height;y++)for(int x=0;x<width;x++){int p=((height-1-y)*width+x)*4;pixels[y*width+x]=0xff000000|((raw.get(p)&255)<<16)|((raw.get(p+1)&255)<<8)|(raw.get(p+2)&255);}
        final String receipt=status(),settings=options;
        new Thread(()->{try{
            String name="Phoenix-Museum-"+System.currentTimeMillis();Bitmap bitmap=Bitmap.createBitmap(pixels,width,height,Bitmap.Config.ARGB_8888);String destination;
            if(Build.VERSION.SDK_INT>=29){ContentValues values=new ContentValues();values.put(MediaStore.Images.Media.DISPLAY_NAME,name+".png");values.put(MediaStore.Images.Media.MIME_TYPE,"image/png");values.put(MediaStore.Images.Media.RELATIVE_PATH,"Pictures/Phoenix Museum");values.put(MediaStore.Images.Media.IS_PENDING,1);android.net.Uri uri=getContentResolver().insert(MediaStore.Images.Media.EXTERNAL_CONTENT_URI,values);if(uri==null)throw new IOException("Could not create image");try(OutputStream out=getContentResolver().openOutputStream(uri)){if(out==null||!bitmap.compress(Bitmap.CompressFormat.PNG,100,out))throw new IOException("Could not save image");}values.clear();values.put(MediaStore.Images.Media.IS_PENDING,0);getContentResolver().update(uri,values,null,null);destination="Pictures/Phoenix Museum";
            }else{File folder=new File(getExternalFilesDir(null),"Pictures");folder.mkdirs();try(OutputStream out=new FileOutputStream(new File(folder,name+".png"))){bitmap.compress(Bitmap.CompressFormat.PNG,100,out);}destination=folder.getAbsolutePath();}
            bitmap.recycle();JSONObject report=new JSONObject(receipt);report.put("display_fps",fps);report.put("settings",new JSONObject(settings));try(FileWriter writer=new FileWriter(new File(getExternalFilesDir(null),name+".json"))){writer.write(report.toString(2));}
            final String path=destination;handler.post(()->Toast.makeText(this,"Saved to "+path,Toast.LENGTH_LONG).show());
        }catch(Exception e){handler.post(()->Toast.makeText(this,"Save failed: "+e.getMessage(),Toast.LENGTH_LONG).show());}},"Museum capture").start();
    }
    void copyAssets(String source,File target)throws IOException{String[] children=getAssets().list(source);if(children!=null&&children.length>0){target.mkdirs();for(String child:children)copyAssets(source+"/"+child,new File(target,child));}else{target.getParentFile().mkdirs();try(InputStream in=getAssets().open(source)){if(target.exists()&&target.length()==in.available())return;File temp=new File(target+".tmp");try(OutputStream out=new FileOutputStream(temp)){byte[] bytes=new byte[65536];int n;while((n=in.read(bytes))>0)out.write(bytes,0,n);}if(!temp.renameTo(target))throw new IOException("Could not install "+source);}}}
    @Override protected void onPause(){super.onPause();pauseProcessing(true);surface.onPause();fps=0;}
    @Override protected void onResume(){super.onResume();if(surface!=null)surface.onResume();pauseProcessing(false);}
}
