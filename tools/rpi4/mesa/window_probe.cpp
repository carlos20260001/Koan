/*
 * Copyright 2026, air/OS. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
// Pixel-exact native EGL window checks, including resize and bitmap retirement.
// --benchmark W H N waits for every BView draw; its rate is completed draws per
// second, not physical display refresh. Keep the window visible and the screen awake.
#include <Application.h>
#include <Autolock.h>
#include <Bitmap.h>
#include <Locker.h>
#include <Messenger.h>
#include <Roster.h>
#include <Screen.h>
#include <View.h>
#include <Window.h>
#include <EGL/egl.h>
#include <GLES3/gl3.h>
#include "BitmapHook.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <set>

static void require(bool ok, const char* what)
{
 if (!ok) { std::fprintf(stderr, "FAIL %s (EGL %#x GL %#x)\n", what, eglGetError(), glGetError()); if(be_app)be_app->ShowCursor(); std::exit(1); }
}
static unsigned shared_areas()
{
 ssize_t cookie=0; area_info area; unsigned count=0;
 while(get_next_area_info(B_CURRENT_TEAM,&cookie,&area)==B_OK)
  if(!std::strcmp(area.name,"Haiku EGL bitmap"))++count;
 return count;
}
static uint64 gpu_bytes()
{
 ssize_t cookie=0;area_info area;uint64 bytes=0;
 while(get_next_area_info(B_SYSTEM_TEAM,&cookie,&area)==B_OK)
  if(!std::strcmp(area.name,"v3d buffer"))bytes+=area.size;
 return bytes;
}
static void memory(const char* phase)
{
 ssize_t cookie=0;area_info area;uint64 bitmaps=0;
 while(get_next_area_info(B_CURRENT_TEAM,&cookie,&area)==B_OK)
  if(!std::strcmp(area.name,"server_memory"))bitmaps+=area.ram_size;
 // Count GPU storage at its kernel owner, not its duplicate user mappings.
 std::printf("MEM phase=%s gpu_bytes=%llu server_bitmap_ram=%llu shared_areas=%u\n",
  phase,(unsigned long long)gpu_bytes(),(unsigned long long)bitmaps,shared_areas());
}
static unsigned setting(const char* name,unsigned fallback,unsigned maximum)
{
 const char* text=std::getenv(name);if(!text)return fallback;
 int value=std::atoi(text);require(value>0&&unsigned(value)<=maximum,"probe setting range");
 return unsigned(value);
}
static bigtime_t cpu_time(team_id team)
{
 team_usage_info usage={};
 require(get_team_usage_info(team,B_TEAM_USAGE_SELF,&usage)==B_OK,"team CPU time");
 return usage.user_time+usage.kernel_time;
}
static team_id app_server_team()
{
 team_id team=be_roster->TeamFor("application/x-vnd.vos-app_server");
 require(team>=B_OK,"app_server team");return team;
}
class FrameView : public BView, public BitmapHook {
public:
 FrameView() : BView(BRect(0,0,63,63), "EGL pixels", B_FOLLOW_NONE,
   B_WILL_DRAW | B_FRAME_EVENTS), fLock("EGL pixels"), fBitmap(nullptr),
   fWidth(64), fHeight(64), fPublished(0), fDrawn(0), fDrawCalls(0) {}
 void GetSize(uint32_t& w, uint32_t& h) override {
  BAutolock lock(&fLock); w=fWidth; h=fHeight;
 }
 BBitmap* SetBitmap(BBitmap* bitmap) override {
  BBitmap* old;
  { BAutolock lock(&fLock); old=fBitmap; fBitmap=bitmap; ++fPublished;
    if (bitmap) fAreas.insert(bitmap->Area()); }
  BMessenger(this).SendMessage(B_INVALIDATE);
  return old;
 }
 void Draw(BRect) override {
  BAutolock lock(&fLock);
  if (fBitmap) { SetDrawingMode(B_OP_COPY); DrawBitmap(fBitmap,B_ORIGIN); }
  Sync(); fDrawn=fPublished; ++fDrawCalls;
 }
 void FrameResized(float w,float h) override {
  BAutolock lock(&fLock); fWidth=uint32_t(w)+1; fHeight=uint32_t(h)+1;
 }
 void Resize(unsigned w,unsigned h) {
  require(Window()->Lock(),"window lock");
  Window()->ResizeTo(w-1,h-1); ResizeTo(w-1,h-1); Window()->Unlock();
  bigtime_t end=system_time()+5000000;
  do { uint32_t a,b; GetSize(a,b); if(a==w&&b==h)return; snooze(1000); } while(system_time()<end);
  require(false,"resize callback");
 }
 void WaitDrawn() {
  bigtime_t end=system_time()+5000000;
  do { { BAutolock lock(&fLock); if(fBitmap&&fPublished==fDrawn)return; } snooze(1000); } while(system_time()<end);
  require(false,"draw completion");
 }
 static bool VerifyPixels(BBitmap* bitmap,unsigned frame,unsigned w,unsigned h,bool padding,bool quiet=false) {
  require(bitmap&&bitmap->InitCheck()==B_OK,"valid bitmap");
  require(bitmap->Bounds().IntegerWidth()+1==int(w)&&bitmap->Bounds().IntegerHeight()+1==int(h),"bitmap dimensions");
  const uint8_t* bits=static_cast<const uint8_t*>(bitmap->Bits());
  for(unsigned y=0;y<h;y++) {
   const uint8_t* row=bits+size_t(y)*bitmap->BytesPerRow();
   for(unsigned x=0;x<w;x++) {
    unsigned glY=h-1-y;
    unsigned colour=(x>=5&&x<5+w/3&&glY>=7&&glY<7+h/2)?((frame+3)&7):(frame&7);
    for(unsigned c=0;c<3;c++) {
     if(row[4*x+c]!=((colour&(1u<<(2-c)))?255:0)) {
      if(quiet)return false;
      const char* dump=std::getenv("PROBE_FAILURE_PREFIX");
      if(dump) {
       char path[1024];std::snprintf(path,sizeof(path),"%s-%s.ppm",dump,padding?"bitmap":"screen");
       FILE* file=std::fopen(path,"wb");
       if(file) {
        std::fprintf(file,"P6\n%u %u\n255\n",w,h);
        for(unsigned yy=0;yy<h;yy++)for(unsigned xx=0;xx<w;xx++) {
         const uint8_t* pixel=bits+size_t(yy)*bitmap->BytesPerRow()+4*xx;
         uint8_t rgb[]={pixel[2],pixel[1],pixel[0]};std::fwrite(rgb,1,3,file);
        }
        std::fclose(file);
       }
      }
      std::fprintf(stderr,"pixel kind=%s frame=%u size=%ux%u x=%u y=%u channel=%u got=%u\n",padding?"bitmap":"screen",frame,w,h,x,y,c,row[4*x+c]);
      require(false,"pixel colours");
     }
    }
    if(padding) require(row[4*x+3]==255,"alpha");
   }
   if(padding) for(size_t x=w*4;x<size_t(bitmap->BytesPerRow());x++) require(row[x]==0,"row padding");
  }
  return true;
 }
 void Verify(unsigned frame,unsigned w,unsigned h,bool screen) {
  WaitDrawn();
  { BAutolock lock(&fLock); VerifyPixels(fBitmap,frame,w,h,true);
    if(std::getenv("PROBE_EXPECT_SHARED")) {
     area_info info; require(get_area_info(fBitmap->Area(),&info)==B_OK,"bitmap area");
     bool expected=w>=128&&h>=64&&uint64_t(w)*h>=128*1024;
     require((!std::strcmp(info.name,"Haiku EGL bitmap"))==expected,"shared/copy bitmap path");
    }
  }
  if(screen) {
   require(Window()->Lock(),"window lock for screen bounds");
   BRect bounds=Bounds(); ConvertToScreen(&bounds); Window()->Unlock();
   BScreen output(Window()); BBitmap pixels(BRect(0,0,w-1,h-1),0,B_RGB32);
   // DrawBitmap synchronizes bitmap access; the compositor may update the
   // front buffer later. Wait for that independently of the strict bitmap check.
   bigtime_t deadline=system_time()+1000000;
   for(;;) {
    require(output.ReadBitmap(&pixels,false,&bounds)==B_OK,"screen readback");
    if(VerifyPixels(&pixels,frame,w,h,false,true))break;
    if(system_time()>=deadline) { VerifyPixels(&pixels,frame,w,h,false); break; }
    snooze(1000);
   }
  }
 }
 void Stats() { BAutolock lock(&fLock); std::printf("published=%llu drawn=%llu draw_calls=%llu bitmap_areas=%zu\n",(unsigned long long)fPublished,(unsigned long long)fDrawn,(unsigned long long)fDrawCalls,fAreas.size()); }
 void Empty() { BAutolock lock(&fLock); require(!fBitmap,"surface retires bitmap"); }
private:
 BLocker fLock; BBitmap* fBitmap; uint32_t fWidth,fHeight;
 uint64_t fPublished,fDrawn,fDrawCalls; std::set<area_id> fAreas;
};
struct Arguments { bool benchmark; unsigned width,height,frames; };
static int32 run(void* opaque)
{
 auto args=*static_cast<Arguments*>(opaque);
 uint64 baselineGPU=gpu_bytes();
 const char* offset=std::getenv("PROBE_OFFSET_X");int x=offset?std::atoi(offset):0;
 require(x>=0&&x<=1200,"window offset");
 for(unsigned cycle=0;cycle<(args.benchmark?1u:setting("PROBE_CYCLES",2,64));cycle++) {
  BWindow* window=new BWindow(BRect(x,60,x+63,123),"air/OS EGL window probe",B_NO_BORDER_WINDOW_LOOK,B_NORMAL_WINDOW_FEEL,B_NOT_CLOSABLE|B_NOT_MINIMIZABLE|B_NOT_ZOOMABLE);
  FrameView* view=new FrameView; window->AddChild(view); window->Show();
  EGLDisplay display=eglGetDisplay(EGL_DEFAULT_DISPLAY);
  require(display!=EGL_NO_DISPLAY&&eglInitialize(display,nullptr,nullptr),"display");
  require(eglBindAPI(EGL_OPENGL_ES_API),"API");
  EGLint attributes[]={EGL_SURFACE_TYPE,EGL_WINDOW_BIT,EGL_RENDERABLE_TYPE,EGL_OPENGL_ES3_BIT,EGL_RED_SIZE,8,EGL_GREEN_SIZE,8,EGL_BLUE_SIZE,8,EGL_ALPHA_SIZE,8,EGL_SAMPLE_BUFFERS,0,EGL_NONE};
  EGLConfig config; EGLint count=0;
  require(eglChooseConfig(display,attributes,&config,1,&count)&&count==1,"config");
  EGLint contextAttributes[]={EGL_CONTEXT_CLIENT_VERSION,3,EGL_NONE};
  EGLContext context=eglCreateContext(display,config,EGL_NO_CONTEXT,contextAttributes);
  require(context!=EGL_NO_CONTEXT,"context");
  EGLSurface surface=eglCreateWindowSurface(display,config,reinterpret_cast<EGLNativeWindowType>(static_cast<BitmapHook*>(view)),nullptr);
  require(surface!=EGL_NO_SURFACE&&eglMakeCurrent(display,surface,surface,context),"window surface");
  std::printf("renderer=%s\n",glGetString(GL_RENDERER));
  unsigned sizes[][2]={{63,65},{257,131},{1280,720},{1281,721},{79,47},{1920,908},{513,257},{1023,257},{640,480}};
  if(args.benchmark) { sizes[0][0]=args.width; sizes[0][1]=args.height; }
  for(unsigned index=0;index<(args.benchmark?1u:sizeof(sizes)/sizeof(sizes[0]));index++) {
   unsigned w=sizes[index][0],h=sizes[index][1],frames=args.benchmark?args.frames:setting("PROBE_FRAMES",8,512);
   view->Resize(w,h);
   EGLint actualW=0,actualH=0;
   require(eglQuerySurface(display,surface,EGL_WIDTH,&actualW)&&eglQuerySurface(display,surface,EGL_HEIGHT,&actualH)&&actualW==int(w)&&actualH==int(h),"surface resize");
   glViewport(0,0,w,h); glDisable(GL_DITHER);
   if(args.benchmark) {
    for(unsigned warm=0;warm<4;warm++) {
     glClearColor(0,0,0,1); glClear(GL_COLOR_BUFFER_BIT);
     require(eglSwapBuffers(display,surface),"warm swap"); view->WaitDrawn();
    }
   }
   if(args.benchmark)memory("warm");
   team_id server=app_server_team();
   bigtime_t clientStart=cpu_time(B_CURRENT_TEAM),serverStart=cpu_time(server);
   bigtime_t start=system_time();
   for(unsigned frame=0;frame<frames;frame++) {
    unsigned colour=frame&7;
    glDisable(GL_SCISSOR_TEST); glClearColor(!!(colour&1),!!(colour&2),!!(colour&4),1); glClear(GL_COLOR_BUFFER_BIT);
    colour=(frame+3)&7; glEnable(GL_SCISSOR_TEST); glScissor(5,7,w/3,h/2);
    glClearColor(!!(colour&1),!!(colour&2),!!(colour&4),1); glClear(GL_COLOR_BUFFER_BIT); glDisable(GL_SCISSOR_TEST);
    require(eglSwapBuffers(display,surface),"swap");
    if(!args.benchmark) view->Verify(frame,w,h,true);
    else view->WaitDrawn();
   }
   view->WaitDrawn();
   double seconds=(system_time()-start)/1e6;
   bigtime_t clientCPU=cpu_time(B_CURRENT_TEAM)-clientStart;
   bigtime_t serverCPU=cpu_time(server)-serverStart;
   std::printf("CPU client_us=%lld app_server_us=%lld shared_areas=%u\n",(long long)clientCPU,(long long)serverCPU,shared_areas());
   view->Verify(frames-1,w,h,true);
   std::printf("PASS cycle=%u size=%ux%u frames=%u seconds=%.6f swaps_per_second=%.2f\n",cycle,w,h,frames,seconds,frames/seconds); view->Stats();
  }
  require(eglMakeCurrent(display,EGL_NO_SURFACE,EGL_NO_SURFACE,EGL_NO_CONTEXT),"unbind");
  require(eglDestroySurface(display,surface),"destroy surface"); view->Empty();
  require(eglDestroyContext(display,context)&&eglTerminate(display)&&eglReleaseThread(),"close EGL");
  require(shared_areas()==0,"shared bitmap areas released");
  memory("closed");
  uint64 finalGPU=gpu_bytes();
  std::printf("LIFETIME baseline_gpu=%llu final_gpu=%llu shared_areas=%u\n",(unsigned long long)baselineGPU,(unsigned long long)finalGPU,shared_areas());
  if(std::getenv("PROBE_EXCLUSIVE"))require(finalGPU==baselineGPU,"GPU allocations released");
  require(window->Lock(),"close window"); window->Quit();
 }
 puts("PASS EGL window pixels, resize and retirement");
 be_app->PostMessage(B_QUIT_REQUESTED); return B_OK;
}
int main(int argc,char** argv)
{
 setvbuf(stdout,nullptr,_IOLBF,0);
 Arguments args={false,0,0,0};
 if(argc==5&&!std::strcmp(argv[1],"--benchmark")) {
  args={true,unsigned(std::atoi(argv[2])),unsigned(std::atoi(argv[3])),unsigned(std::atoi(argv[4]))};
  if(args.width<32||args.width>1920||args.height<32||args.height>908||args.frames<1||args.frames>10000)return 2;
 } else if(argc!=1)return 2;
 BApplication app("application/x-vnd.airOS-EGL-window-probe"); require(app.InitCheck()==B_OK,"application");
 app.HideCursor();
 thread_id worker=spawn_thread(run,"EGL window probe",B_NORMAL_PRIORITY,&args);
 require(worker>=0&&resume_thread(worker)==B_OK,"worker"); app.Run(); status_t result;
 status_t waited=wait_for_thread(worker,&result); app.ShowCursor();
 return waited==B_OK&&result==B_OK?0:1;
}
